#include "filter/well_separation.hpp"

#include <atomic>
#include <exception>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "prop_formula.hpp"

namespace {

// Collects the atom names appearing in a propositional formula. Conditions and
// responses are guaranteed propositional (the temporal structure lives in the
// timing), so only Atom/Not/binary kinds are reachable; the temporal kinds are
// walked defensively for completeness.
void collect_atoms(const Formula& formula,
                   std::unordered_set<std::string>& out) {
    switch (formula.kind()) {
        case Formula::Kind::Atom:
            if (const std::optional<std::string> name = formula.atom_name()) {
                out.insert(*name);
            }
            return;
        case Formula::Kind::Not:
        case Formula::Kind::Next:
        case Formula::Kind::Eventually:
        case Formula::Kind::Globally:
            if (const std::optional<Formula> child = formula.unary_child()) {
                collect_atoms(*child, out);
            }
            return;
        case Formula::Kind::And:
        case Formula::Kind::Or:
        case Formula::Kind::Implies:
        case Formula::Kind::Iff:
        case Formula::Kind::Until:
        case Formula::Kind::Release:
        case Formula::Kind::WeakUntil:
            if (const std::optional<std::pair<Formula, Formula>> children =
                    formula.binary_children()) {
                collect_atoms(children->first, out);
                collect_atoms(children->second, out);
            }
            return;
    }
}

// True if any assumption's condition or response references an output atom.
// Only then can the system possibly force the assumptions to fail, so only then
// is the ltlsynt query worth running.
bool assumptions_reference_output(const Specification& specification) {
    const std::unordered_set<std::string> outputs(
        specification.m_out_atoms.begin(), specification.m_out_atoms.end());
    for (const Requirement& req : specification.m_assumptions) {
        if (req.m_removed) {
            continue;
        }
        std::unordered_set<std::string> atoms;
        collect_atoms(req.m_condition, atoms);
        collect_atoms(req.m_response, atoms);
        if (const Formula* stop = timing_stop(req.m_timing)) {
            collect_atoms(*stop, atoms);
        }
        for (const std::string& atom : atoms) {
            if (outputs.count(atom) != 0) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

bool specification_is_not_well_separated(const Specification& specification,
                                         RealizabilityChecker& checker) {
    if (count_live(specification.m_assumptions) == 0) {
        return false;
    }
    if (!assumptions_reference_output(specification)) {
        return false;
    }
    std::string conjunction;
    for (const Requirement& req : specification.m_assumptions) {
        if (req.m_removed) {
            continue;
        }
        if (!conjunction.empty()) {
            conjunction += " & ";
        }
        conjunction += "(" + req.m_ltl + ")";
    }
    // Guarantees replaced with false: the spec becomes (assumptions) -> false,
    // i.e. !(assumptions). It is realizable exactly when the system has a
    // strategy that forces the assumptions to fail against every environment --
    // the definition of not being well-separated. The input/output partition is
    // the original spec's.
    const std::string formula = "(" + conjunction + ") -> (false)";
    // A query that raises is undecided in exactly the sense a timeout is: no
    // verdict came back. It is caught here rather than left to propagate
    // because filters run outside the scoring pool's failure tolerance, so a
    // single unparseable ltlsynt result -- SPOT 2.15.1 aborts with "Too many
    // acceptance sets used" on specifications the search reaches routinely --
    // would abort the whole run instead of costing one candidate.
    std::optional<bool> realizable;
    try {
        realizable = checker.check_realizability_ltl(
            formula, environment_signals(specification),
            specification.m_out_atoms);
    } catch (const std::exception&) {
        WellSeparationStats::n_errors.fetch_add(1, std::memory_order_relaxed);
    }
    // An undecided query reads as not-well-separated, so the candidate is
    // rejected. This check inverts the usual reading of a failed synthesis:
    // "unrealizable" is what passes a candidate here, so defaulting a timeout
    // to it would admit specifications nobody checked.
    return realizable.value_or(true);
}

FilterFunction make_well_separation_filter(RealizabilityChecker& checker,
                                           std::size_t max_in_flight) {
    return make_predicate_filter(
        "not-well-separated",
        [&checker](const Specification& spec) {
            return !specification_is_not_well_separated(spec, checker);
        },
        max_in_flight);
}
