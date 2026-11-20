#include "psp/fitness.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "filter/well_separation.hpp"
#include "fitness/factory.hpp"
#include "fitness/function.hpp"
#include "fitness/semantic_similarity.hpp"
#include "fitness/status.hpp"
#include "prop_formula.hpp"
#include "psp/filter.hpp"
#include "psp/order_moves.hpp"
#include "psp/ordered_language.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"

namespace psp {

namespace {

std::array<const std::optional<Formula>*, 4> formula_fields(
    const Requirement& req) {
    return {&req.m_p, &req.m_s, &req.m_q, &req.m_r};
}

std::vector<std::size_t> live_slots(const std::vector<Requirement>& reqs) {
    std::vector<std::size_t> slots;
    for (std::size_t slot = 0; slot < reqs.size(); ++slot) {
        if (!reqs[slot].m_removed) {
            slots.push_back(slot);
        }
    }
    return slots;
}

void collect_atoms(const Formula& formula, std::set<std::string>& atoms) {
    (void)formula.rewrite_post_order(
        [&atoms](const Formula& sub) -> std::optional<Formula> {
            if (const auto name = sub.atom_name()) {
                if (*name != "true" && *name != "false") {
                    atoms.insert(*name);
                }
            }
            return std::nullopt;
        });
}

std::size_t count_joint_atoms(const Requirement& lhs, const Requirement& rhs) {
    std::set<std::string> atoms;
    for (const Requirement* req : {&lhs, &rhs}) {
        if (req->m_fixed.has_value()) {
            collect_atoms(*req->m_fixed, atoms);
        }
        for (const std::optional<Formula>* field : formula_fields(*req)) {
            if (field->has_value()) {
                collect_atoms(**field, atoms);
            }
        }
    }
    return atoms.size();
}

// The step count both lowerings are counted at: past the largest count, so a
// bounded pattern's window has closed (see effective_step_count in
// src/fitness/semantic_similarity.cpp), and no further than a Count holds.
std::size_t effective_step_count(const Requirement& lhs, const Requirement& rhs,
                                 std::size_t step_count, std::size_t n_atoms) {
    std::size_t horizon = 0;
    for (const Requirement* req : {&lhs, &rhs}) {
        if (!req->is_fixed()) {
            horizon = std::max(horizon,
                               pattern_count(req->m_pattern).value_or(0) + 2);
        }
    }
    return std::min(std::max(step_count, horizon),
                    max_representable_step_count(n_atoms));
}

double semantic_similarity(const Requirement& lhs, const Requirement& rhs,
                           std::size_t step_count) {
    try {
        const std::size_t n_atoms = count_joint_atoms(lhs, rhs);
        const std::size_t steps =
            effective_step_count(lhs, rhs, step_count, n_atoms);
        const std::string conjunction =
            "(" + lhs.m_ltl + ") & (" + rhs.m_ltl + ")";
        return semantic_similarity_from_counts(
            {cached_count_traces(lhs.m_ltl, n_atoms, steps),
             cached_count_traces(rhs.m_ltl, n_atoms, steps),
             cached_count_traces(conjunction, n_atoms, steps)});
    } catch (const Ltl2tgbaAbandoned&) {
        // As on FRETISH: an abandoned count scores the worst.
        return 0.0;
    }
}

// The pair's similarity, a tombstone on one side alone scoring 0 and on both
// sides 1.
double slot_similarity(const Requirement& lhs, const Requirement& rhs) {
    if (lhs.m_removed || rhs.m_removed) {
        return lhs.m_removed && rhs.m_removed ? 1.0 : 0.0;
    }
    return syntactic_similarity(lhs, rhs);
}

std::string side_conjunction(const std::vector<Requirement>& reqs) {
    std::string conjunction;
    for (const Requirement& req : reqs) {
        if (req.m_removed) {
            continue;
        }
        if (!conjunction.empty()) {
            conjunction += " & ";
        }
        conjunction += "(" + req.m_ltl + ")";
    }
    return conjunction.empty() ? "true" : conjunction;
}

Specification with_guarantee_subset(const Specification& spec,
                                    const std::vector<std::size_t>& slots,
                                    const std::vector<std::size_t>& indices) {
    std::vector<Requirement> guarantees;
    guarantees.reserve(indices.size());
    for (const std::size_t index : indices) {
        assert(index < slots.size());
        guarantees.push_back(spec.m_guarantees[slots[index]]);
    }
    Specification subset = spec;
    subset.m_guarantees = std::move(guarantees);
    return subset;
}

bool subset_realizable(const Specification& subset,
                       RealizabilityChecker& real) {
    // Undecided resolves as unrealizable, as on FRETISH.
    return real.check_realizability_ltl(subset.to_ltl(), subset.m_in_atoms,
                                        subset.m_out_atoms,
                                        specification_sides(subset))
               .value_or(false) &&
           !specification_is_not_well_separated(subset, real);
}

struct PspScorers {
    static double syntactic(const Specification& spec,
                            const Specification& original,
                            const Config& /*cfg*/) {
        return syntactic_similarity(spec, original);
    }

    static std::vector<std::function<double()>> semantic_terms(
        const Specification& spec, const Specification& original,
        const Config& cfg) {
        return semantic_similarity_terms(spec, original,
                                         cfg.default_model_counting_bound);
    }

    static double status(const Specification& spec, const Config& cfg,
                         const std::vector<std::size_t>& slot_order,
                         ComponentCheck component_check,
                         std::vector<std::size_t>* refused) {
        return specification_status(spec, global_sat_checker(),
                                    global_real_checker(), cfg.status_grading,
                                    slot_order, component_check,
                                    cfg.mrs_speculation_depth, refused);
    }

    static std::vector<std::string> status_components(
        const Specification& spec) {
        return specification_status_components(spec);
    }

    static double status_walk_cost(const Specification& spec) {
        return k_part_cost_synthesis *
               static_cast<double>(live_slots(spec.m_guarantees).size());
    }

    static std::vector<std::size_t> admission_order(
        const Specification& original) {
        const std::vector<std::size_t> slots =
            live_slots(original.m_guarantees);
        RealizabilityChecker& real = global_real_checker();
        const std::vector<std::size_t> positions = conflict_degree_order(
            slots.size(), [&original, &slots,
                           &real](const std::vector<std::size_t>& indices) {
                return subset_realizable(
                    with_guarantee_subset(original, slots, indices), real);
            });
        std::vector<std::size_t> slot_order;
        slot_order.reserve(positions.size());
        for (const std::size_t position : positions) {
            slot_order.push_back(slots[position]);
        }
        return slot_order;
    }
};

}  // namespace

double syntactic_similarity(const Requirement& lhs, const Requirement& rhs) {
    if (lhs.is_fixed() || rhs.is_fixed()) {
        if (lhs.m_fixed.has_value() && rhs.m_fixed.has_value()) {
            return lhs.m_fixed->syntactic_similarity(*rhs.m_fixed);
        }
        return 0.0;
    }
    double sum = keyword_token_similarity(lhs, rhs);
    std::size_t n_terms = 1;
    const auto lhs_fields = formula_fields(lhs);
    const auto rhs_fields = formula_fields(rhs);
    for (std::size_t idx = 0; idx < lhs_fields.size(); ++idx) {
        const std::optional<Formula>& left = *lhs_fields[idx];
        const std::optional<Formula>& right = *rhs_fields[idx];
        if (!left.has_value() && !right.has_value()) {
            continue;
        }
        ++n_terms;
        if (left.has_value() && right.has_value()) {
            sum += left->syntactic_similarity(*right);
        }
    }
    return sum / static_cast<double>(n_terms);
}

double syntactic_similarity(const Specification& spec,
                            const Specification& original) {
    const std::size_t total =
        std::max(spec.m_assumptions.size(), original.m_assumptions.size()) +
        std::max(spec.m_guarantees.size(), original.m_guarantees.size());
    if (total == 0) {
        return 0.0;
    }
    double sum = 0.0;
    const auto add = [&sum](const std::vector<Requirement>& lhs,
                            const std::vector<Requirement>& rhs) {
        const std::size_t common = std::min(lhs.size(), rhs.size());
        for (std::size_t slot = 0; slot < common; ++slot) {
            sum += slot_similarity(lhs[slot], rhs[slot]);
        }
    };
    add(spec.m_assumptions, original.m_assumptions);
    add(spec.m_guarantees, original.m_guarantees);
    return sum / static_cast<double>(total);
}

std::vector<std::function<double()>> semantic_similarity_terms(
    const Specification& spec, const Specification& original,
    std::size_t step_count) {
    std::vector<std::function<double()>> terms;
    const auto collect = [&terms, step_count](
                             const std::vector<Requirement>& lhs,
                             const std::vector<Requirement>& rhs) {
        const std::size_t common = std::min(lhs.size(), rhs.size());
        for (std::size_t slot = 0; slot < common; ++slot) {
            if (lhs[slot] == rhs[slot]) {
                continue;
            }
            if (lhs[slot].m_removed || rhs[slot].m_removed) {
                terms.emplace_back([] { return 0.0; });
                continue;
            }
            terms.emplace_back(
                [&first = lhs[slot], &second = rhs[slot], step_count] {
                    return (semantic_similarity(first, second, step_count) +
                            keyword_similarity(first, second)) /
                           2.0;
                });
        }
    };
    collect(spec.m_assumptions, original.m_assumptions);
    collect(spec.m_guarantees, original.m_guarantees);
    return terms;
}

std::vector<std::string> specification_status_components(
    const Specification& spec) {
    std::vector<std::string> components;
    for (const std::vector<Requirement>* reqs :
         {&spec.m_assumptions, &spec.m_guarantees}) {
        for (const Requirement& req : *reqs) {
            if (!req.m_removed) {
                components.push_back(req.m_ltl);
            }
        }
    }
    return components;
}

double specification_status(const Specification& spec,
                            SatisfiabilityChecker& sat,
                            RealizabilityChecker& real, StatusGrading grading,
                            const std::vector<std::size_t>& slot_order,
                            ComponentCheck component_check,
                            std::size_t speculation_depth,
                            std::vector<std::size_t>* refused_slots) {
    if (refused_slots != nullptr) {
        refused_slots->clear();
    }
    const std::vector<std::size_t> slots = live_slots(spec.m_guarantees);
    if (grading == StatusGrading::Aurus) {
        return status_score_aurus(
            side_conjunction(spec.m_assumptions),
            side_conjunction(spec.m_guarantees), sat, [&spec, &slots, &real] {
                return slots.empty() ||
                       real.check_realizability_ltl(
                               spec.to_ltl(), spec.m_in_atoms, spec.m_out_atoms,
                               specification_sides(spec))
                           .value_or(false);
            });
    }
    const std::vector<std::string> components =
        component_check == ComponentCheck::Included
            ? specification_status_components(spec)
            : std::vector<std::string>{};
    std::vector<std::size_t> refused_positions;
    const double score = status_score_mrs(
        components, slots.size(), sat,
        [&spec, &slots, &real](const std::vector<std::size_t>& indices) {
            return subset_realizable(
                with_guarantee_subset(spec, slots, indices), real);
        },
        position_order_from_slots(slot_order, slots),
        "(" + side_conjunction(spec.m_assumptions) + ") & (" +
            side_conjunction(spec.m_guarantees) + ")",
        speculation_depth,
        refused_slots != nullptr ? &refused_positions : nullptr);
    if (refused_slots != nullptr) {
        refused_slots->reserve(refused_positions.size());
        for (const std::size_t position : refused_positions) {
            refused_slots->push_back(slots[position]);
        }
    }
    return score;
}

bool specification_is_vacuous(const Specification& spec,
                              SatisfiabilityChecker& sat) {
    if (has_valid_guarantee(spec, sat)) {
        return true;
    }
    if (live_slots(spec.m_assumptions).empty()) {
        return false;
    }
    // A timed-out query reads as satisfiable, so a slow check never discards
    // a candidate.
    return !sat.check_satisfiability(side_conjunction(spec.m_assumptions))
                .value_or(true);
}

AggregateWeightedFitnessFunctionT<Specification> get_fitness_function(
    const Specification& original, const Config& cfg) {
    return make_fitness_function<Specification, PspScorers>(original, cfg);
}

}  // namespace psp
