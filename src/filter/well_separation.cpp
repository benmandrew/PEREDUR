#include "filter/well_separation.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "prop_formula/atoms.hpp"
#include "requirement.hpp"
#include "tlsf/specification.hpp"

namespace {

// The per-front-end accessors the check below is written against. A FRETISH
// assumption contributes its condition and response; a TLSF one is a formula
// of an assumption-side section (INITIALLY, REQUIRE, ASSUME).
std::unordered_set<std::string> assumption_atoms(const Specification& spec) {
    std::unordered_set<std::string> atoms;
    for (const Requirement& req : spec.m_assumptions) {
        if (req.m_removed) {
            continue;
        }
        collect_atoms(req.m_condition, atoms);
        collect_atoms(req.m_response, atoms);
        if (const Formula* stop = timing_stop(req.m_timing)) {
            collect_atoms(*stop, atoms);
        }
    }
    return atoms;
}

std::unordered_set<std::string> assumption_atoms(
    const tlsf::Specification& spec) {
    std::unordered_set<std::string> atoms;
    for (const tlsf::Section* section : tlsf::assumption_sections_of(spec)) {
        for (const tlsf::SectionEntry& entry : *section) {
            if (!entry.m_removed) {
                collect_atoms(entry.m_formula, atoms);
            }
        }
    }
    return atoms;
}

std::string assumption_conjunction(const Specification& spec) {
    std::string conjunction;
    for (const Requirement& req : spec.m_assumptions) {
        if (req.m_removed) {
            continue;
        }
        if (!conjunction.empty()) {
            conjunction += " & ";
        }
        conjunction += "(" + req.m_ltl + ")";
    }
    return conjunction;
}

std::string assumption_conjunction(const tlsf::Specification& spec) {
    return spec.assumption_ltl();
}

std::vector<std::string> input_signals(const Specification& spec) {
    return environment_signals(spec);
}

std::vector<std::string> input_signals(const tlsf::Specification& spec) {
    return spec.m_inputs;
}

const std::vector<std::string>& output_signals(const Specification& spec) {
    return spec.m_out_atoms;
}

const std::vector<std::string>& output_signals(
    const tlsf::Specification& spec) {
    return spec.m_outputs;
}

// True if any live assumption references an output atom. Only then can the
// system possibly force the assumptions to fail, so only then is the ltlsynt
// query worth running; an assumption over inputs alone is well-separated by
// construction, and so is a specification with no live assumptions.
template <typename Spec>
bool assumptions_reference_output(const Spec& spec) {
    const std::vector<std::string>& outputs = output_signals(spec);
    const std::unordered_set<std::string> atoms = assumption_atoms(spec);
    return std::any_of(outputs.begin(), outputs.end(),
                       [&atoms](const std::string& output) {
                           return atoms.count(output) != 0;
                       });
}

}  // namespace

template <typename Spec>
bool specification_is_not_well_separated(const Spec& specification,
                                         RealizabilityChecker& checker) {
    if (!assumptions_reference_output(specification)) {
        return false;
    }
    // Guarantees replaced with false: the spec becomes (assumptions) -> false,
    // i.e. !(assumptions). It is realizable exactly when the system has a
    // strategy that forces the assumptions to fail against every environment --
    // the definition of not being well-separated. The input/output partition is
    // the original spec's.
    const std::string formula =
        "(" + assumption_conjunction(specification) + ") -> (false)";
    // A query that raises is undecided in exactly the sense a timeout is: no
    // verdict came back. It is caught here rather than left to propagate
    // because filters run outside the scoring pool's failure tolerance, so a
    // single unparseable ltlsynt result -- SPOT 2.15.1 aborts with "Too many
    // acceptance sets used" on specifications the search reaches routinely --
    // would abort the whole run instead of costing one candidate.
    std::optional<bool> realizable;
    try {
        realizable = checker.check_realizability_ltl(
            formula, input_signals(specification),
            output_signals(specification));
    } catch (const std::exception&) {
        WellSeparationStats::n_errors.fetch_add(1, std::memory_order_relaxed);
    }
    // An undecided query reads as not-well-separated, so the candidate is
    // rejected. This check inverts the usual reading of a failed synthesis:
    // "unrealizable" is what passes a candidate here, so defaulting a timeout
    // to it would admit specifications nobody checked.
    return realizable.value_or(true);
}

template <typename Spec>
FilterFunctionT<Spec> make_well_separation_filter(RealizabilityChecker& checker,
                                                  std::size_t max_in_flight) {
    return make_predicate_filter<Spec>(
        "not-well-separated",
        [&checker](const Spec& spec) {
            return !specification_is_not_well_separated(spec, checker);
        },
        max_in_flight);
}

template bool specification_is_not_well_separated(const Specification&,
                                                  RealizabilityChecker&);
template bool specification_is_not_well_separated(const tlsf::Specification&,
                                                  RealizabilityChecker&);
template FilterFunctionT<Specification>
make_well_separation_filter<Specification>(RealizabilityChecker&, std::size_t);
template FilterFunctionT<tlsf::Specification>
make_well_separation_filter<tlsf::Specification>(RealizabilityChecker&,
                                                 std::size_t);
