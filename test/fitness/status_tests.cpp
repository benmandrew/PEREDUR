#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "fitness/status.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "status";

// Builds a one-requirement spec with an immediate trigger/response. The LTL
// is derived automatically from trigger/response/timing; these tests only
// exercise specification_status's propositional pre-checks (trigger/response
// satisfiability), which never look at the LTL string.
Specification make_spec(const std::string& trigger, const std::string& response,
                        const std::vector<std::string>& in_atoms = {},
                        const std::vector<std::string>& out_atoms = {}) {
    return Specification({},
                         {Requirement(Formula(trigger), Formula(response),
                                      timing::immediately())},
                         in_atoms, out_atoms);
}

// --- specification_status ---

TEST(test_status_unsat_trigger_returns_zero) {
    // Condition p & !p is unsatisfiable, so is `condition & response`.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("p & !p", "q");
    expect(specification_status(spec, sat, real) ==
               k_status_component_unsatisfiable,
           "status: unsatisfiable condition should score the component tier");
}

TEST(test_status_unsat_response_returns_zero) {
    // An unsatisfiable response makes `condition & response` unsatisfiable, so
    // the single component tier catches it without a check of its own.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("p", "q & !q");
    expect(specification_status(spec, sat, real) ==
               k_status_component_unsatisfiable,
           "status: unsatisfiable response should score the component tier");
}

TEST(test_status_unsat_conjunction_returns_zero) {
    // Condition p and response !p are individually satisfiable but cannot hold
    // together, which is what makes the requirement incoherent.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("p", "!p");
    expect(specification_status(spec, sat, real) ==
               k_status_component_unsatisfiable,
           "status: a condition and response that cannot hold together should "
           "score the component tier");
}

TEST(test_status_jointly_unsat_responses_pass_individual_checks) {
    // Two requirements whose responses are individually satisfiable but
    // jointly contradictory. Components are per-requirement, so both pass and
    // the realizability call decides.
    // G(a -> b) & G(!a -> !b) = G(a <-> b) is realizable (set b := a).
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("b"), timing::immediately()),
         Requirement(Formula("!a"), Formula("!b"), timing::immediately())},
        {"a"}, {"b"});
    expect(specification_status(spec, sat, real) == k_status_realizable,
           "status: jointly unsat responses that are individually coherent "
           "should still reach the realizability tier");
}

TEST(test_status_unrealizable_returns_point_five) {
    // Two guarantees whose propositional projections are all satisfiable but
    // whose combined LTL is unrealizable:
    //   G(true -> F o)  — output must eventually be true, infinitely often
    //   G(o -> i)       — whenever output is true, input must already be true
    // The environment plays i=false forever, so by G(o -> i) the controller
    // can never set o=true, yet the first guarantee demands it does so
    // infinitely often.
    //
    // Propositional checks pass:
    //   conj_a  = (true) & (o)       — SAT
    //   conj_g  = (o) & (i)          — SAT
    //   conj_ag = (true & o) & (o & i) = (o & i) — SAT
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {},
        {Requirement(Formula("true"), Formula("o"), timing::eventually()),
         Requirement(Formula("o"), Formula("i"), timing::immediately())},
        {"i"}, {"o"});
    expect(specification_status(spec, sat, real) == k_status_unrealizable,
           "status: satisfiable but unrealizable spec should score the "
           "unrealizable tier");
}

TEST(test_status_realizable_returns_one) {
    // G(i -> o): controller mirrors the input. Strategy o := i always works.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("i", "o", {"i"}, {"o"});
    expect(specification_status(spec, sat, real) == k_status_realizable,
           "status: satisfiable and realizable spec should score 1.0");
}

// `G o` as an assumption over the system's own output: the system satisfies
// the specification by holding o false, which breaks the assumption and makes
// the implication hold for nothing. Realizable, so the old three-point scale
// scored it 1.0 -- level with a genuine repair. It now scores level with
// unrealizable instead, which is the whole of the change: the search is paid
// for repairing, not for cheating.
TEST(test_status_ill_separated_scores_level_with_unrealizable) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {Requirement(Formula("true"), Formula("o"), timing::always())},
        {Requirement(Formula("i"), Formula("o"), timing::immediately())}, {"i"},
        {"o"});
    expect(specification_status(spec, sat, real) == k_status_unrealizable,
           "status: a spec realizable only by defeating its own assumptions "
           "should score level with unrealizable, not with a genuine repair");
}

// The counterpart, so the tier above is not passing for the wrong reason: an
// assumption over an input atom alone cannot be defeated by the system, and
// still scores as a genuine repair.
TEST(test_status_input_only_assumption_still_scores_one) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {Requirement(Formula("true"), Formula("i"), timing::always())},
        {Requirement(Formula("i"), Formula("o"), timing::immediately())}, {"i"},
        {"o"});
    expect(specification_status(spec, sat, real) == k_status_realizable,
           "status: a realizable spec whose assumption is over inputs alone "
           "should still score 1.0");
}

TEST(test_status_no_guarantees_skips_the_solver) {
    // An empty guarantee side leaves a `true` consequent, so the score is
    // realizable without a solver call. RealizabilityChecker is left
    // default-constructed and unused, which is the point.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {Requirement(Formula("i"), Formula("i"), timing::immediately())}, {},
        {"i"}, {"o"});
    expect(specification_status(spec, sat, real) == k_status_realizable,
           "status: no guarantees should score realizable");
}

// --- status_score_mrs, against a stub oracle ---

// Records every subset the walk asked about, so the tests can pin the query
// sequence rather than only the resulting score.
struct RecordingOracle {
    std::vector<std::vector<std::size_t>> queries;
    std::function<bool(const std::vector<std::size_t>&)> admissible;

    bool operator()(const std::vector<std::size_t>& indices) {
        queries.push_back(indices);
        return admissible(indices);
    }
};

TEST(test_mrs_keeps_everything_when_all_parts_are_admissible) {
    SatisfiabilityChecker sat;
    RecordingOracle oracle{
        {}, [](const std::vector<std::size_t>&) { return true; }};
    const double score = status_score_mrs(
        {"p"}, 4, sat,
        [&oracle](const std::vector<std::size_t>& idx) { return oracle(idx); });
    expect(score == k_status_realizable,
           "mrs: a fully realizable guarantee side should score exactly 1.0");
    expect(oracle.queries.size() == 4,
           "mrs: the walk should ask once per part");
}

TEST(test_mrs_scores_the_kept_fraction) {
    // Part 2 conflicts with part 0, so the walk keeps 0, 1 and 3.
    SatisfiabilityChecker sat;
    const double score = status_score_mrs(
        {"p"}, 4, sat, [](const std::vector<std::size_t>& indices) {
            const bool has_zero =
                std::find(indices.begin(), indices.end(), 0) != indices.end();
            const bool has_two =
                std::find(indices.begin(), indices.end(), 2) != indices.end();
            return !(has_zero && has_two);
        });
    expect(score == 0.75, "mrs: three parts of four kept should score 0.75");
}

TEST(test_mrs_walk_carries_only_the_accepted_prefix) {
    // A rejected part is dropped rather than carried, so every later query is
    // about a subset the walk has actually accepted. That is what makes the
    // queries recur across near-identical candidates and hit the cache.
    SatisfiabilityChecker sat;
    RecordingOracle oracle{{}, [](const std::vector<std::size_t>& indices) {
                               return std::find(indices.begin(), indices.end(),
                                                1) == indices.end();
                           }};
    const double score = status_score_mrs(
        {"p"}, 3, sat,
        [&oracle](const std::vector<std::size_t>& idx) { return oracle(idx); });
    expect(score == 2.0 / 3.0, "mrs: rejecting one part of three scores 2/3");
    const std::vector<std::vector<std::size_t>> expected = {
        {0}, {0, 1}, {0, 2}};
    expect(oracle.queries == expected,
           "mrs: a rejected part should not appear in any later query");
}

TEST(test_mrs_admission_order_changes_which_maximal_set_is_reached) {
    // Part 0 conflicts with every other part, which is the structure that
    // biases index order: admitting 0 first rejects the three it blocks.
    // Deferring it keeps those three instead. Greedy returns a maximal subset
    // rather than a maximum one, and this is the whole of what the order buys.
    SatisfiabilityChecker sat;
    const auto blocked = [](const std::vector<std::size_t>& indices) {
        return indices.size() < 2 ||
               std::find(indices.begin(), indices.end(), 0) == indices.end();
    };
    expect(status_score_mrs({"p"}, 4, sat, blocked) == 0.25,
           "mrs: index order should keep only the blocking part");
    expect(status_score_mrs({"p"}, 4, sat, blocked, {1, 2, 3, 0}) == 0.75,
           "mrs: deferring the blocking part should keep the three it blocks");
}

TEST(test_mrs_admission_order_queries_the_sorted_set) {
    // The oracle sees a set of parts in one order whatever sequence admitted
    // them, since both front ends build their subset in the order they are
    // handed and the cache keys on the resulting formula string.
    SatisfiabilityChecker sat;
    RecordingOracle oracle{
        {}, [](const std::vector<std::size_t>&) { return true; }};
    status_score_mrs(
        {"p"}, 3, sat,
        [&oracle](const std::vector<std::size_t>& idx) { return oracle(idx); },
        {2, 1, 0});
    const std::vector<std::vector<std::size_t>> expected = {
        {2}, {1, 2}, {0, 1, 2}};
    expect(oracle.queries == expected,
           "mrs: a reversed walk should still query ascending index sets");
}

TEST(test_mrs_admission_order_is_projected_onto_the_part_count) {
    // An order is computed once on the input specification and replayed on
    // mutants whose part count has moved either way, so it is projected rather
    // than trusted: parts it no longer addresses are dropped, parts it does not
    // cover are appended, and every part is still walked exactly once.
    expect(project_admission_order({}, 3) == std::vector<std::size_t>{0, 1, 2},
           "mrs: an empty reference should project to index order");
    expect(
        project_admission_order({2, 0, 1}, 2) == std::vector<std::size_t>{0, 1},
        "mrs: a reference naming a part that is gone should drop it");
    expect(project_admission_order({2, 0}, 4) ==
               std::vector<std::size_t>{2, 0, 1, 3},
           "mrs: parts the reference misses should follow in index order");
    expect(
        project_admission_order({1, 1, 0}, 2) == std::vector<std::size_t>{1, 0},
        "mrs: a repeated part should be walked once");
}

TEST(test_conflict_degree_order_defers_the_blocking_part) {
    // detector's shape: part 0 cannot be held with any of the other three, and
    // those three are jointly fine. Min-degree ranks 0 last.
    const std::vector<std::size_t> order =
        conflict_degree_order(4, [](const std::vector<std::size_t>& indices) {
            return indices.size() < 2 ||
                   std::find(indices.begin(), indices.end(), 0) ==
                       indices.end();
        });
    expect(order == std::vector<std::size_t>{1, 2, 3, 0},
           "degree: the part conflicting with every other should sort last");
}

TEST(test_conflict_degree_order_ranks_a_solo_unrealizable_part_last) {
    // A part no subset can ever keep says nothing by its conflicts, so it is
    // ranked past every other rather than by a degree that means nothing.
    const std::vector<std::size_t> order =
        conflict_degree_order(3, [](const std::vector<std::size_t>& indices) {
            return std::find(indices.begin(), indices.end(), 1) ==
                   indices.end();
        });
    expect(order == std::vector<std::size_t>{0, 2, 1},
           "degree: a part unrealizable alone should sort last");
}

TEST(test_conflict_degree_order_keeps_index_order_on_a_tie) {
    // Equal degree keeps index order, so the result is a function of the
    // specification rather than of how the pairwise queries interleaved.
    const auto no_conflicts = [](const std::vector<std::size_t>&) {
        return true;
    };
    const std::vector<std::size_t> order =
        conflict_degree_order(5, no_conflicts);
    expect(order == std::vector<std::size_t>{0, 1, 2, 3, 4},
           "degree: parts of equal degree should keep index order");
    expect(conflict_degree_order(5, no_conflicts) == order,
           "degree: the order should be deterministic across calls");
}

TEST(test_mrs_walk_is_deterministic) {
    // Parts 0 and 1 conflict, and whichever comes first is kept: greedy returns
    // a maximal subset, not a maximum one. Pinned so the property is not taken
    // for a bug later, and so the score stays a deterministic function of the
    // candidate, which seed reproducibility requires.
    SatisfiabilityChecker sat;
    const auto conflicting = [](const std::vector<std::size_t>& indices) {
        return indices.size() < 2;
    };
    const double first = status_score_mrs({"p"}, 2, sat, conflicting);
    const double second = status_score_mrs({"p"}, 2, sat, conflicting);
    expect(first == 0.5 && first == second,
           "mrs: the greedy walk should be deterministic across calls");
}

TEST(test_mrs_short_circuits_on_an_unsatisfiable_component) {
    // The component tier still runs first, and costs satisfiability queries
    // rather than the n realizability queries the walk would otherwise pay to
    // learn nothing.
    SatisfiabilityChecker sat;
    RecordingOracle oracle{
        {}, [](const std::vector<std::size_t>&) { return true; }};
    const double score = status_score_mrs(
        {"p & !p"}, 3, sat,
        [&oracle](const std::vector<std::size_t>& idx) { return oracle(idx); });
    expect(score == k_status_component_unsatisfiable,
           "mrs: an unsatisfiable component should score the component tier");
    expect(oracle.queries.empty(),
           "mrs: the component tier should short-circuit the walk entirely");
}

TEST(test_mrs_empty_guarantee_side_scores_realizable) {
    SatisfiabilityChecker sat;
    const double score =
        status_score_mrs({"p"}, 0, sat, [](const std::vector<std::size_t>&) {
            fail("mrs: an empty guarantee side should not query the oracle");
            return false;
        });
    expect(score == k_status_realizable,
           "mrs: no guarantee-side parts should score realizable");
}

// --- specification_status under StatusGrading::Mrs ---

TEST(test_mrs_grades_an_unrealizable_spec_between_the_tiers) {
    // Three guarantees, of which the first two are jointly realizable and the
    // third breaks them:
    //   G(i -> o)  -- mirror the input
    //   G F o      -- o must hold infinitely often
    //   G(o -> i)  -- o only where i already holds
    // The environment plays i false forever, so the third forbids o and the
    // second demands it. Greedy keeps the first two, scoring 2/3 -- a value the
    // tiered scale cannot express, on a spec it scores 0.5.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {},
        {Requirement(Formula("i"), Formula("o"), timing::immediately()),
         Requirement(Formula("true"), Formula("o"), timing::eventually()),
         Requirement(Formula("o"), Formula("i"), timing::immediately())},
        {"i"}, {"o"});
    expect(specification_status(spec, sat, real) == k_status_unrealizable,
           "mrs: the tiered scale should score this spec 0.5");
    expect(
        specification_status(spec, sat, real, StatusGrading::Mrs) == 2.0 / 3.0,
        "mrs: two of three guarantees kept should score 2/3");
}

TEST(test_mrs_realizable_spec_still_scores_one) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("i", "o", {"i"}, {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Mrs) ==
               k_status_realizable,
           "mrs: a realizable spec should still score exactly 1.0");
}

TEST(test_mrs_ill_separated_spec_does_not_score_one) {
    // Well-separation is folded into the subset oracle, so a guarantee only
    // reachable by defeating the specification's own assumption is rejected
    // rather than kept. 1.0 keeps meaning what it means on the tiered scale:
    // realizable for a real reason.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {Requirement(Formula("true"), Formula("o"), timing::always())},
        {Requirement(Formula("i"), Formula("o"), timing::immediately())}, {"i"},
        {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Mrs) <
               k_status_realizable,
           "mrs: a spec realizable only by defeating its own assumptions must "
           "not score 1.0");
}

TEST(test_mrs_input_only_assumption_still_scores_one) {
    // The counterpart, so the test above is not passing for the wrong reason.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {Requirement(Formula("true"), Formula("i"), timing::always())},
        {Requirement(Formula("i"), Formula("o"), timing::immediately())}, {"i"},
        {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Mrs) ==
               k_status_realizable,
           "mrs: an assumption over inputs alone should still score 1.0");
}

TEST(test_mrs_defaults_to_the_tiered_scale) {
    // The default argument matters: every existing caller that passes no
    // grading must keep the behaviour it had.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("i", "o", {"i"}, {"o"});
    expect(specification_status(spec, sat, real) ==
               specification_status(spec, sat, real, StatusGrading::Tiered),
           "mrs: the default grading should be the tiered scale");
}

// --- specification_status under StatusGrading::Aurus ---

// `G <response>`, the shape that makes a whole side unsatisfiable when paired
// with its own negation. The ladder grades sides rather than requirements, so
// every level below realizability needs a side that cannot hold.
Requirement always(const std::string& response) {
    return Requirement(Formula("true"), Formula(response), timing::always());
}

TEST(test_aurus_both_sides_unsatisfiable_score_zero) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec({always("i"), always("!i")},
                             {always("o"), always("!o")}, {"i"}, {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_component_unsatisfiable,
           "aurus: neither side satisfiable should score the bottom level");
}

TEST(test_aurus_unsatisfiable_assumptions_score_the_guarantees_level) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {always("i"), always("!i")},
        {Requirement(Formula("i"), Formula("o"), timing::immediately())}, {"i"},
        {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_aurus_guarantees_only,
           "aurus: an unsatisfiable assumption side with a satisfiable "
           "guarantee side should score 0.05");
}

TEST(test_aurus_unsatisfiable_guarantees_score_the_assumptions_level) {
    // An empty assumption side is `true`, so it is the satisfiable half here.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec({}, {always("o"), always("!o")}, {"i"}, {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_aurus_assumptions_only,
           "aurus: an unsatisfiable guarantee side with a satisfiable "
           "assumption side should score 0.1");
}

TEST(test_aurus_contradictory_sides_score_the_contradictory_level) {
    // `G i` and `G !i` are each satisfiable alone, so both side queries pass
    // and only the conjunction places the candidate.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec({always("i")}, {always("!i")}, {"i"}, {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_aurus_contradictory,
           "aurus: sides that are satisfiable alone but contradict each other "
           "should score 0.2");
}

TEST(test_aurus_unrealizable_scores_point_five) {
    // The spec of test_status_unrealizable_returns_point_five: both sides hold
    // together, and no strategy exists.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {},
        {Requirement(Formula("true"), Formula("o"), timing::eventually()),
         Requirement(Formula("o"), Formula("i"), timing::immediately())},
        {"i"}, {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_unrealizable,
           "aurus: a jointly satisfiable but unrealizable spec should score "
           "0.5");
}

TEST(test_aurus_realizable_scores_one) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const auto spec = make_spec("i", "o", {"i"}, {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_realizable,
           "aurus: a realizable spec should score 1.0");
}

// The divergence, asserted rather than only commented. The assumption `G o` is
// over the system's own output, so the system satisfies the specification by
// holding o false and defeating it. Tiered folds well-separation into its
// realizability query and scores that 0.5; the AuRUS ladder does not ask the
// question at all, because AuRUS does not -- WellSeparationAnalysis.java has no
// caller in its search -- and scores it full marks. An arm that graded it would
// not be the ladder it is named after.
TEST(test_aurus_does_not_penalise_an_ill_separated_candidate) {
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec(
        {Requirement(Formula("true"), Formula("o"), timing::always())},
        {Requirement(Formula("i"), Formula("o"), timing::immediately())}, {"i"},
        {"o"});
    expect(specification_status(spec, sat, real, StatusGrading::Tiered) ==
               k_status_unrealizable,
           "aurus: the tiered scale should still cap an ill-separated "
           "candidate at the unrealizable tier");
    expect(specification_status(spec, sat, real, StatusGrading::Aurus) ==
               k_status_realizable,
           "aurus: the AuRUS ladder must score an ill-separated but realizable "
           "candidate 1.0, since AuRUS never asks about well-separation");
}

}  // namespace
