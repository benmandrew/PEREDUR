#include "tlsf/filter.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "genetic/generation.hpp"
#include "prop_formula.hpp"
#include "runner/black.hpp"
#include "thread_pool.hpp"
#include "tlsf/specification.hpp"

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
    return make_predicate_filter<tlsf::Specification>(
        "vacuity",
        [](const tlsf::Specification& spec) {
            return !tlsf_is_vacuous(spec, global_sat_checker());
        },
        max_in_flight);
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
