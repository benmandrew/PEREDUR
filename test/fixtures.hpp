#pragma once

// Builders shared by the FRETISH-side suites. A suite whose builder differs
// keeps its own in its anonymous namespace, which hides these.

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fitness/function.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"

/// A source that replays `values`, each reduced modulo the bound it is asked
/// for, then answers `fallback` modulo the bound for ever.
inline RandomSource make_source(std::vector<std::size_t> values,
                                std::size_t fallback) {
    return RandomSource(
        [values = std::move(values), fallback,
         index = std::size_t{0}](std::size_t upper_bound) mutable {
            if (index >= values.size()) {
                return fallback % upper_bound;
            }
            const std::size_t value = values[index];
            ++index;
            return value % upper_bound;
        });
}

inline Requirement make_req(const std::string& trigger,
                            const std::string& response,
                            Timing timing = timing::immediately()) {
    return Requirement{Formula(trigger), Formula(response), std::move(timing)};
}

/// One immediate `trigger -> response` guarantee and no assumptions.
inline Specification make_spec(const std::string& trigger,
                               const std::string& response,
                               const std::vector<std::string>& in_atoms = {},
                               const std::vector<std::string>& out_atoms = {}) {
    return Specification({}, {make_req(trigger, response)}, in_atoms,
                         out_atoms);
}

/// Four pairwise-distinct specifications over inputs a, b and outputs x, y.
inline std::vector<Specification> distinct_specs() {
    const std::vector<std::string> in_atoms = {"a", "b"};
    const std::vector<std::string> out_atoms = {"x", "y"};
    return {make_spec("a", "x", in_atoms, out_atoms),
            make_spec("b", "y", in_atoms, out_atoms),
            make_spec("a", "y", in_atoms, out_atoms),
            make_spec("b", "x", in_atoms, out_atoms)};
}

/// Scores every specification 0.5, so selection has nothing to prefer.
inline AggregateWeightedFitnessFunction constant_fitness() {
    return AggregateWeightedFitnessFunction(
        {{[](const Specification&) { return 0.5; }, 1.0, "constant"}});
}

/// `G response` under `tim`: no condition.
inline Requirement continual(const std::string& response, const Timing& tim) {
    return Requirement(Formula("true"), Formula(response), tim);
}

/// The given assumptions over one input `req`, guaranteeing `G grant`. Inputs
/// are environment-controlled and outputs system-controlled, so the system can
/// force an assumption to fail only when it constrains an output atom.
inline Specification with_assumptions(std::vector<Requirement> assumptions) {
    return Specification(std::move(assumptions),
                         {continual("grant", timing::immediately())}, {"req"},
                         {"grant"});
}

/// The atoms a monotone rewrite may draw from.
inline const std::vector<std::string>& atom_pool() {
    static const std::vector<std::string> pool = {"a", "b", "c"};
    return pool;
}

/// Whether `from` implies `dest`, asked as the unsatisfiability of
/// `from & !dest`. nullopt means the query went unanswered; callers count those
/// rather than fold them into a verdict, since a monotonicity assertion that
/// passes on an unanswered query asserts nothing.
inline std::optional<bool> implies(const std::string& from,
                                   const std::string& dest) {
    const std::optional<bool> sat = global_sat_checker().check_satisfiability(
        "(" + from + ") & !(" + dest + ")", QueryPolarity::ExpectUnsat);
    if (!sat.has_value()) {
        return std::nullopt;
    }
    return !*sat;
}
