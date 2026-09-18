#include "tlsf/filter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "filter/antichain.hpp"
#include "filter/implication.hpp"
#include "filter/well_separation.hpp"
#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/specification.hpp"

namespace {

// True if any assumption-side formula (INITIALLY, REQUIRE, ASSUME) references
// an output atom. Only then can the system force the assumptions to fail, so
// only then is the well-separation ltlsynt query worth running; an assumption
// over inputs alone is well-separated by construction.
bool assumptions_reference_output(const tlsf::Specification& spec) {
    const std::unordered_set<std::string> outputs(spec.m_outputs.begin(),
                                                  spec.m_outputs.end());
    if (outputs.empty()) {
        return false;
    }
    for (const tlsf::Section* section : tlsf::assumption_sections_of(spec)) {
        for (const tlsf::SectionEntry& entry : *section) {
            if (entry.m_removed) {
                continue;
            }
            std::unordered_set<std::string> atoms;
            collect_atoms(entry.m_formula, atoms);
            for (const std::string& atom : atoms) {
                if (outputs.count(atom) != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Deleted conjuncts are excluded from both scans: the cap is on the size of the
// specification, and a deleted conjunct is not in it. Counting one would keep
// charging a candidate for a formula it has already dropped.
std::size_t max_formula_size(const tlsf::Specification& spec) {
    std::size_t max = 0;
    for (const tlsf::Section* section : tlsf::sections_of(spec)) {
        for (const tlsf::SectionEntry& entry : *section) {
            if (entry.m_removed) {
                continue;
            }
            max = std::max(max, entry.m_formula.n_subformulae());
        }
    }
    return max;
}

bool any_formula_exceeds(const tlsf::Specification& spec, std::size_t cap) {
    for (const tlsf::Section* section : tlsf::sections_of(spec)) {
        for (const tlsf::SectionEntry& entry : *section) {
            if (!entry.m_removed && entry.m_formula.n_subformulae() > cap) {
                return true;
            }
        }
    }
    return false;
}

// FRETISH routes every element-wise filter through make_predicate_filter, so
// parallelising that one function covered all of them. TLSF filters each
// hand-roll their loop, so the same index-collect pattern lives here for the
// two whose predicate is an external solver call. Verdicts are collected by
// index and the survivors rebuilt in population order, so the result matches a
// serial sweep exactly.
std::vector<tlsf::Specification> filter_in_parallel(
    std::vector<tlsf::Specification> pop, std::size_t max_in_flight,
    const std::function<bool(const tlsf::Specification&)>& predicate) {
    std::vector<char> keep(pop.size(), 0);
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < pop.size(); ++idx) {
            keep[idx] = predicate(pop[idx]) ? 1 : 0;
        }
    } else {
        run_bounded_async(
            pop.size(), max_in_flight,
            [&predicate, &pop](std::size_t idx) {
                return
                    [&predicate, &spec = pop[idx]] { return predicate(spec); };
            },
            [&keep](std::size_t idx, bool verdict) {
                keep[idx] = verdict ? 1 : 0;
            });
    }
    std::vector<tlsf::Specification> survivors;
    survivors.reserve(pop.size());
    for (std::size_t idx = 0; idx < pop.size(); ++idx) {
        if (keep[idx] != 0) {
            survivors.push_back(std::move(pop[idx]));
        }
    }
    return survivors;
}

}  // namespace

FilterFunctionT<tlsf::Specification> tlsf_make_dedup_filter() {
    return {"dedup",
            [](std::vector<tlsf::Specification> pop) {
                std::unordered_set<tlsf::Specification> seen;
                seen.reserve(pop.size());
                std::vector<tlsf::Specification> survivors;
                survivors.reserve(pop.size());
                for (tlsf::Specification& spec : pop) {
                    // The set has to own a copy to key on; the survivor is
                    // then moved, so a kept candidate costs one copy rather
                    // than two.
                    if (seen.insert(spec).second) {
                        survivors.push_back(std::move(spec));
                    }
                }
                return survivors;
            },
            FilterKind::Preference};
}

bool tlsf_is_trivially_vacuous(const tlsf::Specification& spec) {
    // A deleted conjunct is exempt: its residual content is not part of the
    // specification, so it must not make one read as vacuous.
    auto any_atom = [](const tlsf::Section& section, const char* atom) {
        return std::any_of(section.begin(), section.end(),
                           [atom](const tlsf::SectionEntry& entry) {
                               return !entry.m_removed &&
                                      entry.m_formula.atom_name() == atom;
                           });
    };
    return any_atom(spec.m_initially, "false") ||
           any_atom(spec.m_require, "false") ||
           any_atom(spec.m_assume, "false") ||
           any_atom(spec.m_preset, "true") || any_atom(spec.m_assert, "true") ||
           any_atom(spec.m_guarantee, "true");
}

bool tlsf_has_unsatisfiable_assumptions(const tlsf::Specification& spec,
                                        SatisfiabilityChecker& checker) {
    const bool no_assumptions = tlsf::count_live(spec.m_initially) == 0 &&
                                tlsf::count_live(spec.m_require) == 0 &&
                                tlsf::count_live(spec.m_assume) == 0;
    if (no_assumptions) {
        return false;
    }
    // Timeout: treat as satisfiable. Dropping on an unknown answer would make
    // the verdict depend on machine load.
    return !checker.check_satisfiability(spec.assumption_ltl()).value_or(true);
}

bool tlsf_has_valid_guarantee(const tlsf::Specification& spec,
                              SatisfiabilityChecker& checker) {
    auto any_valid = [&checker](const tlsf::Section& section) {
        for (const tlsf::SectionEntry& entry : section) {
            // A deleted conjunct is not a guarantee: it must not be able to
            // make the specification read as vacuously satisfied.
            if (entry.m_removed) {
                continue;
            }
            // Keyed on the negated formula alone, so the cache hits across
            // candidates and generations rather than once per guarantee side.
            const std::optional<bool> falsifiable =
                checker.check_satisfiability("!(" +
                                             entry.m_formula.to_string() + ")");
            // Timeout: treat as falsifiable, as the assumption check treats an
            // unknown answer as satisfiable. A non-answer never drops a
            // candidate.
            if (!falsifiable.value_or(true)) {
                return true;
            }
        }
        return false;
    };
    // ASSERT is G-wrapped by the lowering, but `G psi` is valid exactly when
    // psi is, so the raw formula is the query either way -- and the smaller
    // one.
    return any_valid(spec.m_preset) || any_valid(spec.m_assert) ||
           any_valid(spec.m_guarantee);
}

bool tlsf_is_vacuous(const tlsf::Specification& spec,
                     SatisfiabilityChecker& checker) {
    return tlsf_is_trivially_vacuous(spec) ||
           tlsf_has_valid_guarantee(spec, checker) ||
           tlsf_has_unsatisfiable_assumptions(spec, checker);
}

FilterFunctionT<tlsf::Specification> tlsf_make_vacuity_filter(
    std::size_t max_in_flight) {
    return {"vacuity", [max_in_flight](std::vector<tlsf::Specification> pop) {
                return filter_in_parallel(std::move(pop), max_in_flight,
                                          [](const tlsf::Specification& spec) {
                                              return !tlsf_is_vacuous(
                                                  spec, global_sat_checker());
                                          });
            }};
}

bool tlsf_is_not_well_separated(const tlsf::Specification& spec,
                                RealizabilityChecker& checker) {
    if (!assumptions_reference_output(spec)) {
        return false;
    }
    // Not well-separated exactly when (assumptions) -> false is realizable: the
    // system has a strategy forcing its own assumptions to fail. An undecided
    // query reads as realizable and so drops the candidate, for the reason
    // given in filter/well_separation.cpp -- which is also where the catch
    // below is justified, and whose counter this shares, so the two paths
    // report one figure for one property.
    const std::string formula = "(" + spec.assumption_ltl() + ") -> (false)";
    std::optional<bool> realizable;
    try {
        realizable = checker.check_realizability_ltl(formula, spec.m_inputs,
                                                     spec.m_outputs);
    } catch (const std::exception&) {
        WellSeparationStats::n_errors.fetch_add(1, std::memory_order_relaxed);
    }
    return realizable.value_or(true);
}

FilterFunctionT<tlsf::Specification> tlsf_make_well_separation_filter(
    RealizabilityChecker& checker, std::size_t max_in_flight) {
    return {"not-well-separated",
            [&checker, max_in_flight](std::vector<tlsf::Specification> pop) {
                return filter_in_parallel(
                    std::move(pop), max_in_flight,
                    [&checker](const tlsf::Specification& candidate) {
                        return !tlsf_is_not_well_separated(candidate, checker);
                    });
            }};
}

FilterFunctionT<tlsf::Specification> tlsf_make_predicate_filter(
    std::string name, std::function<bool(const tlsf::Specification&)> predicate,
    std::size_t max_in_flight, FilterKind kind) {
    return {std::move(name),
            [predicate = std::move(predicate),
             max_in_flight](std::vector<tlsf::Specification> pop) {
                return filter_in_parallel(std::move(pop), max_in_flight,
                                          predicate);
            },
            kind};
}

std::vector<CorrectnessCheckT<tlsf::Specification>> tlsf_correctness_checks(
    SatisfiabilityChecker& sat, RealizabilityChecker& real) {
    std::vector<CorrectnessCheckT<tlsf::Specification>> checks;
    checks.push_back({"vacuity",
                      [&sat](const tlsf::Specification& spec) {
                          return !tlsf_is_vacuous(spec, sat);
                      },
                      true});
    checks.push_back({"not-well-separated",
                      [&real](const tlsf::Specification& spec) {
                          return !tlsf_is_not_well_separated(spec, real);
                      },
                      false});
    return checks;
}

std::optional<bool> tlsf_spec_implies(const tlsf::Specification& from,
                                      const tlsf::Specification& dest,
                                      SatisfiabilityChecker& checker) {
    if (from == dest) {
        return true;
    }
    const std::optional<bool> sat = checker.check_satisfiability(
        "(" + from.to_ltl() + ") & !(" + dest.to_ltl() + ")",
        QueryPolarity::ExpectUnsat);
    if (!sat.has_value()) {
        return std::nullopt;
    }
    return !sat.value();
}

FilterFunctionT<tlsf::Specification> tlsf_make_bloat_cap_filter(
    const tlsf::Specification& original, double max_ratio) {
    const std::size_t original_max = max_formula_size(original);
    return {"bloat-cap",
            [original_max, max_ratio](std::vector<tlsf::Specification> pop) {
                if (original_max == 0) {
                    return pop;
                }
                const auto cap = static_cast<std::size_t>(
                    max_ratio * static_cast<double>(original_max));
                std::vector<tlsf::Specification> survivors;
                survivors.reserve(pop.size());
                for (tlsf::Specification& spec : pop) {
                    if (!any_formula_exceeds(spec, cap)) {
                        survivors.push_back(std::move(spec));
                    }
                }
                return survivors;
            },
            FilterKind::Preference};
}

TlsfSimilarityKey tlsf_syntactic_similarity_key(tlsf::Specification original,
                                                const Config& cfg) {
    // Both captured by value: the key outlives this call.
    return [original = std::move(original),
            cfg](const tlsf::Specification& spec) mutable {
        return tlsf_syntactic_similarity(spec, original, cfg);
    };
}

FilterFunctionT<tlsf::Specification> tlsf_make_implication_filter(
    SatisfiabilityChecker& checker, TlsfSimilarityKey similarity,
    const GenerationProgressCallback& on_progress) {
    return {
        "implication",
        [&checker, similarity = std::move(similarity),
         on_progress](std::vector<tlsf::Specification> pop) {
            antichain::reset_stats();
            if (pop.size() <= 1) {
                return pop;
            }
            const std::vector<uint8_t> subsumed = antichain::subsumed_in(
                pop,
                [&checker](const tlsf::Specification& lhs,
                           const tlsf::Specification& rhs) {
                    return tlsf_spec_implies(lhs, rhs, checker).value_or(false);
                },
                similarity, on_progress);
            std::vector<tlsf::Specification> maximal;
            for (std::size_t i = 0; i < pop.size(); ++i) {
                if (subsumed[i] == 0U) {
                    maximal.push_back(std::move(pop[i]));
                }
            }
            return maximal;
        },
        FilterKind::Preference};
}
