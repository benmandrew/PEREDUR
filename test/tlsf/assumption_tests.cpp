// Tests over the 2026-08-25 assumption-reach operators: the compositional body
// grammar and the bare-F form in tlsf_add_assumption.
//
// Each operator has a value at which it is a no-op, and every one of them is
// asserted to cost no RandomSource draw there. That is what lets an archived
// campaign reproduce against a binary that has these keys, and it is not free:
// a probability read after the draw rather than before shifts the stream even
// when the operator never fires.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/mutation.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_assumption";

tlsf::Specification three_input_spec() {
    return tlsf::parse(
        "INFO { SEMANTICS: Mealy; }\nMAIN {\nINPUTS { b1; b2; b3; } "
        "OUTPUTS { f1; }\nGUARANTEE { G (b1 -> F f1); }\n}\n");
}

// Counts draws so a "costs no draw" claim is measured rather than assumed.
std::size_t draws_for(const tlsf::Specification& spec, const Config& cfg,
                      std::size_t seeds) {
    std::size_t total = 0;
    for (std::size_t seed = 0; seed < seeds; ++seed) {
        const auto counter = std::make_shared<std::size_t>(0);
        std::mt19937 engine(static_cast<std::uint32_t>(seed));
        RandomSource source([engine, counter](std::size_t bound) mutable {
            ++*counter;
            return static_cast<std::size_t>(
                bounded_uniform(engine, static_cast<std::uint32_t>(bound)));
        });
        (void)tlsf_mutate(spec, source, cfg);
        total += *counter;
    }
    return total;
}

bool mentions(const Formula& formula, Formula::Kind kind) {
    if (formula.kind() == kind) {
        return true;
    }
    if (const auto child = formula.unary_child()) {
        return mentions(*child, kind);
    }
    if (const auto pair = formula.binary_children()) {
        return mentions(pair->first, kind) || mentions(pair->second, kind);
    }
    return false;
}

void collect_atoms(const Formula& formula, std::vector<std::string>& into) {
    if (const auto name = formula.atom_name()) {
        into.push_back(*name);
        return;
    }
    if (const auto child = formula.unary_child()) {
        collect_atoms(*child, into);
        return;
    }
    if (const auto pair = formula.binary_children()) {
        collect_atoms(pair->first, into);
        collect_atoms(pair->second, into);
    }
}

// -- the body grammar ---------------------------------------------------------

// At width 1 the body is a single literal, which is what the operator emitted
// before the grammar existed.
TEST(test_width_one_draws_a_single_literal) {
    const tlsf::Specification spec = three_input_spec();
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.tlsf_max_assumption_width = 1;
    cfg.p_conditional_assumption = 0.0;  // the unconditional G F form
    for (std::size_t seed = 0; seed < 40; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        expect(mutated.m_assume.size() == 1,
               "assumption: width 1 appends exactly one assumption");
        const Formula& body = mutated.m_assume.front().m_formula;
        expect(!mentions(body, Formula::Kind::Or) &&
                   !mentions(body, Formula::Kind::And),
               "assumption: width 1 draws a single literal, no connective");
    }
}

// Above width 1 the grammar must actually reach both connectives, or it is the
// old template with extra draws. lift's ideal is a disjunction and
// humanoid-503's is a conjunction, so neither alone would do.
TEST(test_wide_bodies_reach_both_connectives) {
    const tlsf::Specification spec = three_input_spec();
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.tlsf_max_assumption_width = 3;
    cfg.p_conditional_assumption = 0.0;
    bool saw_or = false;
    bool saw_and = false;
    first_seed(200, [&](std::size_t seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        const Formula& body = mutated.m_assume.front().m_formula;
        saw_or = saw_or || mentions(body, Formula::Kind::Or);
        saw_and = saw_and || mentions(body, Formula::Kind::And);
        return saw_or && saw_and;
    });
    expect(saw_or, "assumption: a wide body reaches a disjunction");
    expect(saw_and, "assumption: a wide body reaches a conjunction");
}

// Every atom of an appended body is an input, whatever the width: an assumption
// obliging an output is one the system defeats by withholding its own signal.
// The guard may be an output, so the unconditional form is forced here.
TEST(test_wide_bodies_use_inputs_only) {
    const tlsf::Specification spec = three_input_spec();
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 0.0;
    cfg.tlsf_max_assumption_width = 3;
    for (std::size_t seed = 0; seed < 60; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        const Formula& body = mutated.m_assume.front().m_formula;
        std::vector<std::string> atoms;
        collect_atoms(body, atoms);
        for (const std::string& atom : atoms) {
            expect(atom != "f1",
                   "assumption: a wide body never obliges an output");
        }
    }
}

// -- the bare form ------------------------------------------------------------

// lily11's whole ideal is `F req`. Wrapped in G it is strictly stronger, so the
// bare form has to be drawn rather than rewritten into.
TEST(test_bare_assumption_is_reachable) {
    const tlsf::Specification spec = three_input_spec();
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 0.0;
    cfg.tlsf_p_bare_assumption = 1.0;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        const Formula& body = mutated.m_assume.front().m_formula;
        expect(body.kind() == Formula::Kind::Eventually,
               "assumption: p_bare = 1 emits F body, not G F body");
    }
    Config wrapped = cfg;
    wrapped.tlsf_p_bare_assumption = 0.0;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, wrapped);
        const Formula& body = mutated.m_assume.front().m_formula;
        expect(body.kind() == Formula::Kind::Globally,
               "assumption: p_bare = 0 keeps the G F form");
    }
}

// -- the no-draw discipline ---------------------------------------------------

// Each key must draw only above its no-op value. The assertion is that the
// stream *differs* rather than that it lengthens: a structural operator returns
// as soon as it fires, skipping the side draw, the slot draw and the rewrite
// draws an ordinary mutation makes, so enabling one can shorten the stream as
// easily as lengthen it. Direction is not the property; conditionality is.
//
// That the count at the no-op values is what it was before these keys existed
// is pinned separately, by the absolute golden in
// test_zero_probability_costs_no_draw over in monotone_tests.cpp, which holds
// both of them at no-op.
TEST(test_each_key_draws_only_when_armed) {
    const tlsf::Specification bare_spec = three_input_spec();

    // The width and the bare form are reached only through an appended
    // assumption, so the append has to be certain for either to draw at all.
    Config appending;
    appending.p_add_assumption = 1.0;
    appending.tlsf_max_assumption_width = 1;
    appending.tlsf_p_bare_assumption = 0.0;
    const std::size_t append_baseline = draws_for(bare_spec, appending, 16);

    Config wide = appending;
    wide.tlsf_max_assumption_width = 3;
    expect(draws_for(bare_spec, wide, 16) != append_baseline,
           "no-op: max_assumption_width draws only above 1");

    Config bare = appending;
    bare.p_conditional_assumption = 0.0;
    const std::size_t bare_baseline = draws_for(bare_spec, bare, 16);
    Config bare_armed = bare;
    bare_armed.tlsf_p_bare_assumption = 0.5;
    expect(draws_for(bare_spec, bare_armed, 16) != bare_baseline,
           "no-op: p_bare_assumption draws only above 0");
}

}  // namespace
