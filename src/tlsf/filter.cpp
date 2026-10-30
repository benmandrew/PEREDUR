#include "tlsf/filter.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "filter/implication.hpp"
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

namespace {

std::vector<std::string> sorted_live(const tlsf::Section& section) {
    std::vector<std::string> rendered;
    for (const Formula& formula : tlsf::live_formulae(section)) {
        rendered.push_back(formula.to_string());
    }
    std::sort(rendered.begin(), rendered.end());
    return rendered;
}

// Marks removed each live conjunct of @p dest that @p from also carries, and
// reports whether it marked any.
bool drop_shared(const tlsf::Section& from, tlsf::Section& dest) {
    const std::vector<std::string> carried = sorted_live(from);
    bool dropped = false;
    for (tlsf::SectionEntry& entry : dest) {
        if (!entry.m_removed &&
            std::binary_search(carried.begin(), carried.end(),
                               entry.m_formula.to_string())) {
            entry.m_removed = true;
            dropped = true;
        }
    }
    return dropped;
}

}  // namespace

std::optional<tlsf::Specification> tlsf_residual_destination(
    const tlsf::Specification& from, const tlsf::Specification& dest) {
    if (from.m_semantics != dest.m_semantics ||
        sorted_live(from.m_initially) != sorted_live(dest.m_initially) ||
        sorted_live(from.m_require) != sorted_live(dest.m_require) ||
        sorted_live(from.m_assume) != sorted_live(dest.m_assume)) {
        return std::nullopt;
    }
    tlsf::Specification residual = dest;
    // Each call runs, not just the first to drop something.
    const bool dropped_preset = drop_shared(from.m_preset, residual.m_preset);
    const bool dropped_assert = drop_shared(from.m_assert, residual.m_assert);
    const bool dropped_guarantee =
        drop_shared(from.m_guarantee, residual.m_guarantee);
    if (!dropped_preset && !dropped_assert && !dropped_guarantee) {
        return std::nullopt;
    }
    return residual;
}

std::optional<bool> tlsf_spec_implies(const tlsf::Specification& from,
                                      const tlsf::Specification& dest,
                                      SatisfiabilityChecker& checker) {
    if (from == dest) {
        return true;
    }
    const std::optional<tlsf::Specification> residual =
        tlsf_residual_destination(from, dest);
    if (residual.has_value() && tlsf::count_live(residual->m_preset) == 0 &&
        tlsf::count_live(residual->m_assert) == 0 &&
        tlsf::count_live(residual->m_guarantee) == 0) {
        ImplicationFilterStats::n_residual_settled.fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }
    if (residual.has_value()) {
        ImplicationFilterStats::n_residual_queries.fetch_add(
            1, std::memory_order_relaxed);
    }
    const tlsf::Specification& target = residual.has_value() ? *residual : dest;
    const std::optional<bool> sat = checker.check_satisfiability(
        "(" + from.to_ltl() + ") & !(" + target.to_ltl() + ")",
        QueryPolarity::ExpectUnsat);
    if (!sat.has_value()) {
        // Counted as the FRETISH spec_implies counts it, so the implication
        // report's timeout figure covers both paths.
        ImplicationFilterStats::n_timeouts.fetch_add(1,
                                                     std::memory_order_relaxed);
        return std::nullopt;
    }
    return !sat.value();
}
