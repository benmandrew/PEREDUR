#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/implication.hpp"
#include "genetic/generation.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/crossover.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/mutation.hpp"
#include "tlsf/operators.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_genetic";

tlsf::Specification parse(const std::string& main_body,
                          const std::string& semantics = "Mealy") {
    return tlsf::parse("INFO { SEMANTICS: " + semantics + "; }\nMAIN {\n" +
                       main_body + "\n}\n");
}

bool is_temporal(Formula::Kind kind) {
    switch (kind) {
        case Formula::Kind::Next:
        case Formula::Kind::Eventually:
        case Formula::Kind::Globally:
        case Formula::Kind::Until:
        case Formula::Kind::Release:
        case Formula::Kind::WeakUntil:
            return true;
        default:
            return false;
    }
}

// Multiset of temporal operator kinds encountered in a formula, used to assert
// the temporal skeleton is preserved under mutation.
void collect_temporal(const Formula& formula, std::multiset<int>& out) {
    if (is_temporal(formula.kind())) {
        out.insert(static_cast<int>(formula.kind()));
    }
    switch (formula.kind()) {
        case Formula::Kind::Atom:
            break;
        case Formula::Kind::Not:
        case Formula::Kind::Next:
        case Formula::Kind::Eventually:
        case Formula::Kind::Globally: {
            const auto child = formula.unary_child();
            if (child.has_value()) {
                collect_temporal(*child, out);
            }
            break;
        }
        default: {
            const auto children = formula.binary_children();
            if (children.has_value()) {
                collect_temporal(children->first, out);
                collect_temporal(children->second, out);
            }
            break;
        }
    }
}

std::multiset<int> temporal_kinds(const Formula& formula) {
    std::multiset<int> kinds;
    collect_temporal(formula, kinds);
    return kinds;
}

bool contains_kind(const Formula& formula, Formula::Kind kind) {
    if (formula.kind() == kind) {
        return true;
    }
    if (const auto child = formula.unary_child(); child.has_value()) {
        return contains_kind(*child, kind);
    }
    if (const auto children = formula.binary_children(); children.has_value()) {
        return contains_kind(children->first, kind) ||
               contains_kind(children->second, kind);
    }
    return false;
}

TEST(test_mutation_preserves_temporal_skeleton) {
    Config cfg;
    cfg.tlsf_p_temporal = 0.0;  // isolate the skeleton-preserving rewrite
    cfg.p_monotone = 0.0;  // ... which the monotone arm is offered ahead of
    const tlsf::Specification original = parse(
        "INPUTS { req; } OUTPUTS { grant; } GUARANTEE { G(req -> F "
        "grant); }");
    const std::multiset<int> skeleton =
        temporal_kinds(original.m_guarantee.front().m_formula);
    expect(!original.m_guarantee.front().m_formula.is_propositional(),
           "mutation: the seed formula is genuinely temporal");

    for (std::size_t seed = 0; seed < 40; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(original, rng, cfg);
        expect(mutated.m_guarantee.size() == 1,
               "mutation: guarantee section shape is preserved");
        const Formula& formula = mutated.m_guarantee.front().m_formula;
        expect(!formula.is_propositional(),
               "mutation: the temporal structure survives mutation");
        expect(temporal_kinds(formula) == skeleton,
               "mutation: the multiset of temporal operators is unchanged");
        expect(!formula.to_string().empty(),
               "mutation: mutated formula has a well-formed string form");
    }
}

// tlsf_p_assumption is the probability of picking the assumption side; the
// guarantee side takes the complement. Both sides hold formulae here, so
// neither can be reached by the empty-side fallback and the probability alone
// decides.
TEST(test_mutation_side_probability_selects_side) {
    tlsf::Specification spec;
    spec.m_inputs = {"a"};
    spec.m_outputs = {"b"};
    spec.m_assume = {Formula("a")};
    spec.m_guarantee = {Formula("b")};

    // A rewrite can land back on the formula it started from, so "this side was
    // selected" is not observable directly. "This side was never selected" is:
    // an untouched side is bit-identical across every seed. Count the seeds on
    // which each side actually changed and read the zeroes as exclusions.
    constexpr std::size_t k_seeds = 200;
    auto changed_counts = [&spec](double p_assumption) {
        Config cfg;
        cfg.p_add_assumption = 0.0;  // isolate the rewrite path
        cfg.p_remove_guarantee = 0.0;
        cfg.tlsf_p_assumption = p_assumption;
        std::pair<std::size_t, std::size_t> counts{0, 0};
        for (std::size_t seed = 0; seed < k_seeds; ++seed) {
            const RandomSource rng = make_random_source_from_seed(seed);
            const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
            if (mutated.m_assume != spec.m_assume) {
                ++counts.first;
            }
            if (mutated.m_guarantee != spec.m_guarantee) {
                ++counts.second;
            }
        }
        return counts;
    };

    expect(changed_counts(0.0).first == 0,
           "mutation: p_assumption = 0 never touches the assumption side");
    expect(changed_counts(1.0).second == 0,
           "mutation: p_assumption = 1 never touches the guarantee side");

    const auto [assume_changed, guarantee_changed] = changed_counts(0.5);
    expect(assume_changed > 0 && guarantee_changed > 0,
           "mutation: p_assumption = 0.5 reaches both sides");
    // The two sections are single atoms, so their no-op rates match and the
    // observed counts stay comparable at an even split.
    expect(assume_changed * 2 > guarantee_changed &&
               guarantee_changed * 2 > assume_changed,
           "mutation: p_assumption = 0.5 splits evenly");
}

TEST(test_temporal_mutation_changes_skeleton) {
    // With tlsf_p_temporal forced to 1, the chosen formula is rewritten by the
    // Brizzio-style operator, which is allowed to insert/drop/swap temporal
    // operators. Over a range of seeds the temporal skeleton must actually
    // change at least once, and every result must stay well-formed.
    Config cfg;
    cfg.tlsf_p_temporal = 1.0;
    cfg.p_monotone = 0.0;        // the monotone arm is offered ahead of it
    cfg.p_add_assumption = 0.0;  // isolate the rewrite path
    cfg.p_remove_guarantee = 0.0;
    const tlsf::Specification original = parse(
        "INPUTS { req; } OUTPUTS { grant; } GUARANTEE { G(req -> F "
        "grant); }");
    const std::multiset<int> skeleton =
        temporal_kinds(original.m_guarantee.front().m_formula);

    bool skeleton_changed = false;
    for (std::size_t seed = 0; seed < 40; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(original, rng, cfg);
        expect(mutated.m_guarantee.size() == 1,
               "temporal mutation: guarantee section shape is preserved");
        const Formula& formula = mutated.m_guarantee.front().m_formula;
        expect(!formula.to_string().empty(),
               "temporal mutation: mutated formula has a well-formed string "
               "form");
        if (temporal_kinds(formula) != skeleton) {
            skeleton_changed = true;
        }
    }
    expect(skeleton_changed,
           "temporal mutation: the temporal skeleton is altered for at least "
           "one seed");
}

TEST(test_temporal_mutation_can_emit_an_implication) {
    // pick_binary_kind draws Implies since 2026-08-21. Case (3) of
    // mutate_temporal is the default branch over any binary node, Iff
    // included, so widening that draw is what puts `<->` → `->` in reach —
    // the sole ideal for ltl2dba-r-2, ltl2dba-theta-2 and ltl2dba27. Starting
    // from a biconditional with no implication anywhere in it, some seed must
    // produce one.
    Config cfg;
    cfg.tlsf_p_temporal = 1.0;
    cfg.p_monotone = 0.0;  // the monotone arm is offered ahead of it
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    const tlsf::Specification original =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(g <-> X r); }");
    expect(!contains_kind(original.m_guarantee.front().m_formula,
                          Formula::Kind::Implies),
           "implication draw: the input carries no implication to start with");

    bool emitted = false;
    for (std::size_t seed = 0; seed < 40 && !emitted; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(original, rng, cfg);
        emitted = contains_kind(mutated.m_guarantee.front().m_formula,
                                Formula::Kind::Implies);
    }
    expect(emitted,
           "implication draw: the temporal mutation emits an implication for "
           "at least one seed");
}

// Case (2d) of mutate_temporal is the arm firing at an atom or a unary node.
// It grafts a drawn anchor onto the mutated child under a connective, and
// that menu draws an implication, so a guarded response -- the shape of every
// minimal guarantee weakening -- is one draw away at exactly the nodes where a
// guard has to be introduced.
//
// `G g` is the subject that isolates the graft: its child is an atom, and
// case (1) emits only an atom or a unary node, so pick_connective_kind is the
// sole source of a binary node anywhere in the result.
tlsf::Specification connective_subject() {
    return parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G g; }");
}

Config connective_config() {
    Config cfg;
    cfg.tlsf_p_temporal = 1.0;
    cfg.p_monotone = 0.0;  // the monotone arm is offered ahead of it
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    return cfg;
}

TEST(test_connective_graft_can_emit_an_implication) {
    const Config cfg = connective_config();
    const tlsf::Specification original = connective_subject();
    bool grafted = false;
    for (std::size_t seed = 0; seed < 200 && !grafted; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(original, rng, cfg);
        const Formula& formula = mutated.m_guarantee.front().m_formula;
        const auto children = formula.binary_children();
        grafted = formula.kind() == Formula::Kind::Implies &&
                  children.has_value() &&
                  children->first.kind() == Formula::Kind::Atom;
    }
    expect(grafted,
           "connective draw: the case (2d) graft emits `anchor -> inner` for "
           "at least one seed");
}

// An appended assumption is a fairness property `G F <input>`, or, under
// p_conditional_assumption, a guarded `G(<guard> -> o <input>)` whose
// consequent carries F, X or no modality at all. The obliged literal is always
// an input.
TEST(test_add_assumption_forms) {
    tlsf::Specification spec;
    spec.m_inputs = {"req"};
    spec.m_outputs = {"grant"};
    spec.m_guarantee = {parse("INPUTS { req; } OUTPUTS { grant; } "
                              "GUARANTEE { G (req -> F grant); }")
                            .m_guarantee.front()};
    Config cfg;
    cfg.p_add_assumption = 1.0;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        expect(mutated.m_assume.size() == 1,
               "add-assumption: exactly one assumption is appended");
        expect(mutated.m_guarantee == spec.m_guarantee,
               "add-assumption: guarantees are left untouched");
        const std::string text = mutated.m_assume.front().m_formula.to_string();
        const bool fairness = text == "G(F(req))" || text == "G(F(!(req)))";
        const bool guarded =
            text.rfind("G((", 0) == 0 && text.find("->") != std::string::npos;
        expect(fairness || guarded,
               "add-assumption: appended a fairness or guarded assumption");
    }
}

// The obliged literal is an input even though the guard may be an output:
// `G(<output> -> F <input>)` stays reachable and `G(<input> -> F <output>)`
// does not.
TEST(test_add_assumption_never_obliges_an_output) {
    tlsf::Specification spec;
    spec.m_inputs = {"req"};
    spec.m_outputs = {"grant"};
    spec.m_guarantee = {parse("INPUTS { req; } OUTPUTS { grant; } "
                              "GUARANTEE { G (req -> F grant); }")
                            .m_guarantee.front()};
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 1.0;
    for (std::size_t seed = 0; seed < 40; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        const std::string text = mutated.m_assume.front().m_formula.to_string();
        const std::size_t arrow = text.find("->");
        expect(arrow != std::string::npos,
               "add-assumption: p_conditional_assumption 1 draws the guarded "
               "form");
        expect(text.substr(arrow).find("grant") == std::string::npos,
               "add-assumption: the consequent is drawn from the inputs");
    }
}

// Clone-and-perturb: rather than the template's at-most-seven nodes, the
// appended assumption may be a copy of one the specification already holds,
// which ordinary mutation then edits. gyro-var2's single ideal is roughly a
// 29-node assumption mirroring the specification's own third one, and no
// template reaches that.
TEST(test_add_assumption_can_clone_an_existing_one) {
    const tlsf::Specification spec = parse(
        "INPUTS { req; } OUTPUTS { grant; } "
        "ASSUME { G (req -> F (!(req))); } "
        "GUARANTEE { G (req -> F grant); }");
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.tlsf_p_clone_assumption = 1.0;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        expect(mutated.m_assume.size() == 2,
               "clone-assumption: exactly one assumption is appended");
        expect(mutated.m_assume[1] == spec.m_assume[0],
               "clone-assumption: the appended assumption copies a live one");
        expect(mutated.m_guarantee == spec.m_guarantee,
               "clone-assumption: guarantees are left untouched");
    }
}

// Nothing live to copy, so the template stands in rather than the operator
// becoming a no-op.
TEST(test_clone_assumption_falls_back_to_the_template) {
    tlsf::Specification spec;
    spec.m_inputs = {"req"};
    spec.m_outputs = {"grant"};
    spec.m_guarantee = {parse("INPUTS { req; } OUTPUTS { grant; } "
                              "GUARANTEE { G (req -> F grant); }")
                            .m_guarantee.front()};
    spec.m_assume = {tlsf::SectionEntry(Formula("req"), /*removed=*/true)};
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.tlsf_p_clone_assumption = 1.0;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        expect(mutated.m_assume.size() == 2,
               "clone-assumption: the template appends when nothing is live");
        expect(!mutated.m_assume[1].m_removed,
               "clone-assumption: a tombstone is never resurrected as a copy");
        expect(mutated.m_assume[1].m_formula.to_string() != "req",
               "clone-assumption: the appended assumption is the template's, "
               "not the tombstoned conjunct");
    }
}

// The mirror of add-assumption, and what makes the eight drop-* ideals
// reachable. The deleted conjunct keeps its slot: the similarity objectives
// pair conjuncts by position, so erasing it would start comparing the
// candidate's remaining conjuncts against unrelated ones of the original.
TEST(test_remove_guarantee_tombstones_in_place) {
    const tlsf::Specification spec = parse(
        "INPUTS { req; } OUTPUTS { grant; } "
        "ASSERT { !(grant); } "
        "GUARANTEE { G (req -> F grant); }");
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 1.0;
    bool saw_assert_deleted = false;
    bool saw_guarantee_deleted = false;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        expect(mutated.m_assert.size() == spec.m_assert.size() &&
                   mutated.m_guarantee.size() == spec.m_guarantee.size(),
               "remove-guarantee: sections keep their size");
        expect(tlsf::count_live_guarantees(mutated) == 1,
               "remove-guarantee: exactly one guarantee-side conjunct goes");
        saw_assert_deleted =
            saw_assert_deleted || mutated.m_assert.front().m_removed;
        saw_guarantee_deleted =
            saw_guarantee_deleted || mutated.m_guarantee.front().m_removed;
        expect(mutated.to_ltl().find("!(grant)") == std::string::npos ||
                   !mutated.m_assert.front().m_removed,
               "remove-guarantee: a deleted conjunct leaves the lowering");
    }
    expect(saw_assert_deleted && saw_guarantee_deleted,
           "remove-guarantee: the draw reaches both guarantee-side sections");
}

TEST(test_remove_guarantee_keeps_the_last_live_conjunct) {
    const tlsf::Specification spec = parse(
        "INPUTS { req; } OUTPUTS { grant; } "
        "GUARANTEE { G (req -> F grant); }");
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 1.0;
    for (std::size_t seed = 0; seed < 10; ++seed) {
        const tlsf::Specification mutated =
            tlsf_mutate(spec, make_random_source_from_seed(seed), cfg);
        expect(tlsf::count_live_guarantees(mutated) == 1,
               "remove-guarantee: the only guarantee conjunct is never "
               "deleted");
    }
}

TEST(test_add_assumption_can_reference_output) {
    // The appended assumption's guard draws from inputs ∪ outputs, so the
    // output atom is reachable over a range of seeds.
    // The well-separation check at the final gate, not a syntactic ban, is
    // what then rejects any not-well-separated result.
    tlsf::Specification spec;
    spec.m_inputs = {"req"};
    spec.m_outputs = {"grant"};
    spec.m_guarantee = {parse("INPUTS { req; } OUTPUTS { grant; } "
                              "GUARANTEE { G (req -> F grant); }")
                            .m_guarantee.front()};
    Config cfg;
    cfg.p_add_assumption = 1.0;
    bool saw_output = false;
    for (std::size_t seed = 0; seed < 60; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        expect(mutated.m_assume.size() == 1,
               "add-assumption(output): exactly one assumption is appended");
        if (mutated.m_assume.front().m_formula.to_string().find("grant") !=
            std::string::npos) {
            saw_output = true;
        }
    }
    expect(saw_output,
           "add-assumption(output): the output atom is reachable in an "
           "assumption");
}

TEST(test_assumption_rewrite_can_reference_output) {
    // An assumption-side *rewrite* (not just the add-assumption action) may
    // draw an output atom, so an output-referencing
    // assumption can be reshaped instead of having its output overwritten. This
    // is what lets a G F <output> or G(c -> F <output>) grow a weak-until
    // hold-until form over successive generations.
    Config cfg;
    cfg.p_add_assumption = 0.0;  // isolate the rewrite path
    cfg.p_remove_guarantee = 0.0;
    tlsf::Specification spec;
    spec.m_inputs = {"a", "c"};
    spec.m_outputs = {"bout"};
    spec.m_assume = {Formula("a")};
    // No guarantee-side formulae, so every mutation falls to the assumption
    // side, whose atom pool includes the outputs.
    bool saw_output = false;
    for (std::size_t seed = 0; seed < 60; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification mutated = tlsf_mutate(spec, rng, cfg);
        if (mutated.m_assume.front().m_formula.to_string().find("bout") !=
            std::string::npos) {
            saw_output = true;
        }
    }
    expect(saw_output,
           "mutation: an assumption-side rewrite can introduce an output "
           "atom");
}

TEST(test_weak_until_over_output_is_reachable) {
    // A weak-until (hold-until) assumption over an output does not need a
    // dedicated new operator: the temporal mutation already emits W, and the
    // assumption-side pool keeps the output atom
    // through a rewrite. Starting from the kind of fairness assumption
    // tlsf_add_assumption creates (G(r -> F g)), a temporal rewrite can yield
    // an assumption with a `... W ...` node referencing the output g.
    Config cfg;
    cfg.p_add_assumption = 0.0;  // isolate the rewrite path
    cfg.p_remove_guarantee = 0.0;
    cfg.tlsf_p_assumption = 1.0;  // always mutate the assumption side
    cfg.tlsf_p_temporal = 1.0;    // always the temporal (skeleton) rewrite
    cfg.p_monotone = 0.0;         // which the monotone arm is offered ahead of
    tlsf::Specification seed_spec;
    seed_spec.m_inputs = {"r"};
    seed_spec.m_outputs = {"g"};
    seed_spec.m_assume = {parse("INPUTS { r; } OUTPUTS { g; } "
                                "ASSUME { G (r -> F g); }")
                              .m_assume.front()};
    bool reached = false;
    for (std::size_t seed = 0; seed < 200 && !reached; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        tlsf::Specification current = seed_spec;
        for (int step = 0; step < 6 && !reached; ++step) {
            current = tlsf_mutate(current, rng, cfg);
            for (const tlsf::SectionEntry& entry : current.m_assume) {
                const std::string text = entry.m_formula.to_string();
                if (text.find(") W (") != std::string::npos &&
                    text.find('g') != std::string::npos) {
                    reached = true;
                }
            }
        }
    }
    expect(reached,
           "mutation: a weak-until assumption over an output is reachable by "
           "temporal mutation of a fairness assumption");
}

tlsf::Specification globally(const std::vector<std::string>& atoms) {
    tlsf::Specification spec;
    spec.m_inputs = {"r"};
    spec.m_outputs = {"g"};
    for (const std::string& atom : atoms) {
        spec.m_guarantee.emplace_back(
            Formula::make_unary(Formula::Kind::Globally, Formula(atom)));
    }
    return spec;
}

// The point of the operator: the donor conjunct is drawn from anywhere on
// parent B's side, so material can move between slots. Under the index-for-
// index crossover this replaced, slot 0 could only ever see parent B's slot 0.
TEST(test_crossover_grafts_across_slots) {
    const tlsf::Specification parent_a = globally({"r", "g"});
    const tlsf::Specification parent_b = globally({"!(r)", "!(g)"});

    bool saw_cross_slot = false;
    for (std::size_t seed = 0; seed < 60; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification child =
            tlsf_crossover(parent_a, parent_b, rng);
        expect(child.m_guarantee.size() == 2,
               "crossover: section sizes are preserved");
        expect(child.m_inputs == parent_a.m_inputs &&
                   child.m_outputs == parent_a.m_outputs,
               "crossover: signals are inherited from the first parent");
        std::size_t changed = 0;
        for (std::size_t i = 0; i < child.m_guarantee.size(); ++i) {
            if (!(child.m_guarantee[i] == parent_a.m_guarantee[i])) {
                ++changed;
            }
        }
        expect(changed <= 1, "crossover: at most one conjunct per side merges");
        // G(!(g)) is parent B's slot 1; finding it grafted into slot 0 is only
        // possible with a cross-slot donor.
        if (child.m_guarantee[0].m_formula.to_string().find("!(g)") !=
            std::string::npos) {
            saw_cross_slot = true;
        }
    }
    expect(saw_cross_slot,
           "crossover: a donor from a different slot reaches the target slot");
}

// Section sizes no longer have to match. The offspring keeps parent A's shape
// however long parent B's side is, so an individual that has gained an
// assumption can still breed -- which under index-for-index crossover it could
// not.
TEST(test_crossover_accepts_mismatched_shape) {
    const tlsf::Specification parent_a = globally({"r"});
    const tlsf::Specification parent_b = globally({"!(r)", "!(g)"});

    bool saw_change = false;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification child =
            tlsf_crossover(parent_a, parent_b, rng);
        expect(child.m_guarantee.size() == 1,
               "crossover: the offspring keeps the first parent's shape");
        saw_change = saw_change || !(child == parent_a);
    }
    expect(saw_change,
           "crossover: parents of different section sizes still breed");
}

TEST(test_crossover_mismatched_signals_returns_first) {
    const tlsf::Specification parent_a = globally({"r"});
    tlsf::Specification parent_b = globally({"!(r)"});
    parent_b.m_inputs = {"other"};

    const RandomSource rng = make_random_source_from_seed(1);
    const tlsf::Specification child = tlsf_crossover(parent_a, parent_b, rng);
    expect(child == parent_a,
           "crossover: mismatched signals return the first parent unchanged");
}

// Deletion is mutation's move alone, on both sides: a tombstoned slot of
// parent A is never a target, and a tombstoned conjunct of parent B is never a
// donor -- crossover would otherwise breed from content its parent threw away.
TEST(test_crossover_skips_deleted_conjuncts) {
    tlsf::Specification parent_a = globally({"r", "g"});
    parent_a.m_guarantee[0].m_removed = true;
    tlsf::Specification parent_b = globally({"!(r)", "!(g)"});
    parent_b.m_guarantee[1].m_removed = true;

    for (std::size_t seed = 0; seed < 40; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const tlsf::Specification child =
            tlsf_crossover(parent_a, parent_b, rng);
        expect(child.m_guarantee[0] == parent_a.m_guarantee[0],
               "crossover: a deleted slot is never the target of a merge");
        expect(
            child.m_guarantee[0].m_removed && !child.m_guarantee[1].m_removed,
            "crossover: the removal flags are the first parent's");
        expect(child.m_guarantee[1].m_formula.to_string().find("!(g)") ==
                   std::string::npos,
               "crossover: a deleted conjunct is never a donor");
    }
}

TEST(test_end_to_end_evolution) {
    Config cfg;
    cfg.population_size = 4;
    cfg.default_model_counting_bound = 3;
    cfg.parallel = 1;
    cfg.crossover_rate = 0.5;
    cfg.mutation_rate = 1.0;

    // A Mealy spec requiring the output to predict the next input:
    // unrealizable.
    const tlsf::Specification original =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(g <-> X r); }");
    const auto fitness = tlsf_get_fitness_function(original, cfg);

    const std::size_t target_size = 4;
    const std::vector<tlsf::Specification> seed_pop(target_size, original);
    std::vector<Scored<tlsf::Specification>> population =
        score_population(cfg, seed_pop, fitness);

    const std::vector<FilterFunctionT<tlsf::Specification>> filters = {
        make_dedup_filter<tlsf::Specification>(), tlsf_make_vacuity_filter()};

    for (std::size_t generation = 0; generation < 2; ++generation) {
        const RandomSource rng = make_random_source_from_seed(generation + 1);
        population = evolve_generation_generic(cfg, population, target_size,
                                               /*elitism_size=*/0, fitness,
                                               filters, tlsf_operators(), rng);
        expect(population.size() == target_size,
               "end-to-end: each generation returns target_size offspring");
        for (const Scored<tlsf::Specification>& scored : population) {
            expect(
                std::isfinite(scored.fitness) && scored.fitness >= 0.0 &&
                    scored.fitness <= 1.0,
                "end-to-end: every offspring has a finite fitness in [0, 1]");
        }
    }
}

}  // namespace
