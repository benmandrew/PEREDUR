#include "tlsf/specification.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "live_slots.hpp"
#include "prop_formula.hpp"

namespace tlsf {

bool operator==(const SectionEntry& lhs, const SectionEntry& rhs) {
    return lhs.m_removed == rhs.m_removed && lhs.m_formula == rhs.m_formula;
}

bool operator<(const SectionEntry& lhs, const SectionEntry& rhs) {
    if (lhs.m_formula < rhs.m_formula) {
        return true;
    }
    if (rhs.m_formula < lhs.m_formula) {
        return false;
    }
    return !lhs.m_removed && rhs.m_removed;
}

std::vector<Formula> live_formulae(const Section& section) {
    std::vector<Formula> formulae;
    formulae.reserve(section.size());
    for (const SectionEntry& entry : section) {
        if (!entry.m_removed) {
            formulae.push_back(entry.m_formula);
        }
    }
    return formulae;
}

std::size_t count_live(const Section& section) {
    return count_live_slots(section);
}

std::vector<std::size_t> live_indices(const Section& section) {
    return live_slot_indices(section);
}

std::array<const Section*, 6> sections_of(const Specification& spec) {
    return {&spec.m_initially, &spec.m_preset, &spec.m_require,
            &spec.m_assume,    &spec.m_assert, &spec.m_guarantee};
}

std::array<Section*, 6> mutable_sections_of(Specification& spec) {
    return {&spec.m_initially, &spec.m_preset, &spec.m_require,
            &spec.m_assume,    &spec.m_assert, &spec.m_guarantee};
}

std::array<const Section*, 3> guarantee_sections_of(const Specification& spec) {
    return {&spec.m_preset, &spec.m_assert, &spec.m_guarantee};
}

std::array<Section*, 3> mutable_guarantee_sections_of(Specification& spec) {
    return {&spec.m_preset, &spec.m_assert, &spec.m_guarantee};
}

std::array<const Section*, 3> assumption_sections_of(
    const Specification& spec) {
    return {&spec.m_initially, &spec.m_require, &spec.m_assume};
}

std::array<Section*, 3> mutable_assumption_sections_of(Specification& spec) {
    return {&spec.m_initially, &spec.m_require, &spec.m_assume};
}

SpecificationSides specification_sides(const Specification& spec) {
    // The tags are positional rather than spelled out: they only have to
    // distinguish the six sections from one another, and assumption_sections_of
    // and guarantee_sections_of fix the order.
    auto collect = [](const std::array<const Section*, 3>& sections,
                      char side) {
        std::vector<std::string> conjuncts;
        for (std::size_t index = 0; index < sections.size(); ++index) {
            for (const SectionEntry& entry : *sections[index]) {
                if (entry.m_removed) {
                    continue;
                }
                std::string tagged(1, side);
                tagged += std::to_string(index);
                tagged += '\x1f';
                tagged += entry.m_formula.to_string();
                conjuncts.push_back(std::move(tagged));
            }
        }
        return conjuncts;
    };
    SpecificationSides sides;
    sides.m_assumptions = collect(assumption_sections_of(spec), 'a');
    sides.m_guarantees = collect(guarantee_sections_of(spec), 'g');
    sides.m_scope = "tlsf" + std::to_string(static_cast<int>(spec.m_semantics));
    return sides;
}

std::size_t count_live_guarantees(const Specification& spec) {
    std::size_t total = 0;
    for (const Section* section : guarantee_sections_of(spec)) {
        total += count_live(*section);
    }
    return total;
}

namespace {

/// Folds a section into a single Formula with left-associative And. An empty
/// section is the constant `true`; a singleton is returned unchanged.
Formula conj(const std::vector<Formula>& section) {
    if (section.empty()) {
        return Formula("true");
    }
    Formula result = section.front();
    for (std::size_t idx = 1; idx < section.size(); ++idx) {
        result = Formula::make_binary(Formula::Kind::And, result, section[idx]);
    }
    return result;
}

/// G(conj(section)); the caller guarantees the section is non-empty.
Formula globally_of(const std::vector<Formula>& section) {
    return Formula::make_unary(Formula::Kind::Globally, conj(section));
}

/// `(⋀ antecedent) → (⋀ consequent)` with the paper's `true` simplifications:
/// an empty consequent leaves the implication trivially true (⇒ nullopt, so the
/// caller omits it); an empty antecedent collapses it to the consequent alone.
std::optional<Formula> side_implication(
    const std::vector<Formula>& antecedent,
    const std::vector<Formula>& consequent) {
    if (consequent.empty()) {
        return std::nullopt;
    }
    if (antecedent.empty()) {
        return conj(consequent);
    }
    return Formula::make_binary(Formula::Kind::Implies, conj(antecedent),
                                conj(consequent));
}

/// Collects the non-empty side terms, G-wrapping the invariant section, and
/// conjoins them. Reports whether any term was contributed at all.
Formula collect_side(const std::vector<Formula>& verbatim_first,
                     const std::vector<Formula>& invariant,
                     const std::vector<Formula>& verbatim_last,
                     bool& any_term) {
    std::vector<Formula> terms;
    if (!verbatim_first.empty()) {
        terms.push_back(conj(verbatim_first));
    }
    if (!invariant.empty()) {
        terms.push_back(
            Formula::make_unary(Formula::Kind::Globally, conj(invariant)));
    }
    if (!verbatim_last.empty()) {
        terms.push_back(conj(verbatim_last));
    }
    any_term = !terms.empty();
    return conj(terms);
}

}  // namespace

Formula Specification::to_ltl_formula() const {
    // TLSF v1.1 standard lowering (paper §3.2):
    //   θ_e → ( θ_s ∧ ( (G ψ_e ∧ φ_e) → (G ψ_s ∧ φ_s) ) )
    // Strict pulls the system invariant ψ_s into a weak-until guard and drops
    // it from the consequent (it must hold at least as long as ψ_e does):
    //   θ_e → ( θ_s ∧ (ψ_s W ¬ψ_e) ∧ ( (G ψ_e ∧ φ_e) → φ_s ) )
    // θ_e=INITIALLY, θ_s=PRESET, ψ_e=REQUIRE, ψ_s=ASSERT, φ_e=ASSUME,
    // φ_s=GUARANTEE; an absent section is `true` and drops out.
    const bool strict = m_semantics == Semantics::MealyStrict ||
                        m_semantics == Semantics::MooreStrict;

    // Deleted conjuncts are dropped once, here, so the lowering below reads
    // exactly as the paper states it. A section whose every conjunct has been
    // deleted lowers as an absent section, which is what deleting them means.
    const std::vector<Formula> initially = live_formulae(m_initially);
    const std::vector<Formula> preset = live_formulae(m_preset);
    const std::vector<Formula> require = live_formulae(m_require);
    const std::vector<Formula> assume = live_formulae(m_assume);
    const std::vector<Formula> assert_ = live_formulae(m_assert);
    const std::vector<Formula> guarantee = live_formulae(m_guarantee);

    std::vector<Formula> antecedent;  // G ψ_e ∧ φ_e
    if (!require.empty()) {
        antecedent.push_back(globally_of(require));
    }
    if (!assume.empty()) {
        antecedent.push_back(conj(assume));
    }

    std::vector<Formula> consequent;  // G ψ_s ∧ φ_s (ψ_s omitted when strict)
    if (!strict && !assert_.empty()) {
        consequent.push_back(globally_of(assert_));
    }
    if (!guarantee.empty()) {
        consequent.push_back(conj(guarantee));
    }

    std::vector<Formula>
        body;  // θ_s ∧ (ψ_s W ¬ψ_e) ∧ (antecedent → consequent)
    if (!preset.empty()) {
        body.push_back(conj(preset));
    }
    if (strict && !assert_.empty()) {
        body.push_back(Formula::make_binary(
            Formula::Kind::WeakUntil, conj(assert_),
            Formula::make_unary(Formula::Kind::Not, conj(require))));
    }
    if (std::optional<Formula> inner =
            side_implication(antecedent, consequent)) {
        body.push_back(*inner);
    }

    Formula body_formula = conj(body);  // empty body ⇒ true
    if (initially.empty()) {
        return body_formula;
    }
    return Formula::make_binary(Formula::Kind::Implies, conj(initially),
                                body_formula);
}

std::string Specification::to_ltl() const {
    return to_ltl_formula().to_string();
}

std::string Specification::assumption_ltl() const {
    bool any_term = false;
    return collect_side(live_formulae(m_initially), live_formulae(m_require),
                        live_formulae(m_assume), any_term)
        .to_string();
}

std::string Specification::guarantee_ltl() const {
    bool any_term = false;
    return collect_side(live_formulae(m_preset), live_formulae(m_assert),
                        live_formulae(m_guarantee), any_term)
        .to_string();
}

namespace {

auto tie_sections(const Specification& spec) {
    return std::tie(spec.m_semantics, spec.m_inputs, spec.m_outputs,
                    spec.m_initially, spec.m_preset, spec.m_require,
                    spec.m_assume, spec.m_assert, spec.m_guarantee);
}

}  // namespace

bool operator==(const Specification& lhs, const Specification& rhs) {
    return tie_sections(lhs) == tie_sections(rhs);
}

bool operator<(const Specification& lhs, const Specification& rhs) {
    return tie_sections(lhs) < tie_sections(rhs);
}

}  // namespace tlsf
