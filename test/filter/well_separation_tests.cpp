#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "filter/well_separation.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "well_separation_filter";

// Restores the process-global ltlsynt timeout, which expect() would otherwise
// leave set when it throws.
class ScopedLtlsyntTimeout {
   public:
    explicit ScopedLtlsyntTimeout(std::chrono::milliseconds timeout) {
        RealizabilityChecker::set_timeout(timeout);
    }
    ScopedLtlsyntTimeout(const ScopedLtlsyntTimeout&) = delete;
    ScopedLtlsyntTimeout& operator=(const ScopedLtlsyntTimeout&) = delete;
    ScopedLtlsyntTimeout(ScopedLtlsyntTimeout&&) = delete;
    ScopedLtlsyntTimeout& operator=(ScopedLtlsyntTimeout&&) = delete;
    ~ScopedLtlsyntTimeout() {
        RealizabilityChecker::set_timeout(std::chrono::milliseconds(0));
    }
};

Requirement continual(const std::string& response, const Timing& tim) {
    return Requirement(Formula("true"), Formula(response), tim);
}

Requirement continual_when(const std::string& condition,
                           const std::string& response, const Timing& tim) {
    return Requirement(Formula(condition), Formula(response), tim);
}

// Inputs are environment-controlled, outputs system-controlled. The system can
// force an assumption to fail only when it constrains an output atom.
Specification with_assumptions(std::vector<Requirement> assumptions) {
    return Specification(std::move(assumptions),
                         {continual("grant", timing::immediately())}, {"req"},
                         {"grant"});
}

TEST(test_no_assumptions_is_well_separated) {
    RealizabilityChecker checker;
    const Specification spec = with_assumptions({});
    expect(!specification_is_not_well_separated(spec, checker),
           "well-separation: a spec with no assumptions is well-separated");
}

// `G req` over the input atom req: the environment can keep req true forever,
// so the system cannot force it to fail. `(G req) -> false` is unrealizable.
TEST(test_assumption_over_input_is_well_separated) {
    RealizabilityChecker checker;
    const Specification spec =
        with_assumptions({continual("req", timing::always())});
    expect(!specification_is_not_well_separated(spec, checker),
           "well-separation: an assumption over an input atom cannot be forced "
           "to fail by the system");
}

// `G grant` over the output atom grant: the system controls grant, so it can
// simply never assert it. `(G grant) -> false` is realizable, so the spec is
// vacuously satisfiable and not well-separated.
TEST(test_assumption_over_output_is_not_well_separated) {
    RealizabilityChecker checker;
    const Specification spec =
        with_assumptions({continual("grant", timing::always())});
    expect(specification_is_not_well_separated(spec, checker),
           "well-separation: an assumption over an output atom the system can "
           "force to fail is not well-separated");
}

// Two assumptions, `G req` over the input and `G grant` over the output. Their
// conjunction is not well-separated because the system can force `G grant` to
// fail on its own. Exercises the multi-assumption conjunction path the single-
// assumption cases never reach.
TEST(test_conjunction_with_a_forcible_conjunct_is_not_well_separated) {
    RealizabilityChecker checker;
    const Specification spec =
        with_assumptions({continual("req", timing::always()),
                          continual("grant", timing::always())});
    expect(specification_is_not_well_separated(spec, checker),
           "well-separation: a conjunction is not well-separated when the "
           "system can force any one conjunct to fail");
}

// `G(req -> grant)` mentions the output atom grant, yet the system cannot force
// it to fail: the negation `F(req & !grant)` requires req, which the
// environment controls and can hold false forever. Well-separation is a game
// property, not atom membership -- an assumption referencing an output atom is
// not automatically droppable.
TEST(test_output_atom_the_system_cannot_force_is_well_separated) {
    RealizabilityChecker checker;
    const Specification spec = with_assumptions(
        {continual_when("req", "grant", timing::immediately())});
    expect(!specification_is_not_well_separated(spec, checker),
           "well-separation: an assumption mentioning an output atom is still "
           "well-separated when the environment can keep it satisfied");
}

// Arbiter atom partition: inputs r0,r1 (environment); outputs g0,g1 (system).
// The guarantees are illustrative only -- the well-separation check ignores
// them, deciding purely on the assumptions.
Specification with_arbiter_assumptions(std::vector<Requirement> assumptions) {
    return Specification(std::move(assumptions),
                         {continual("g0", timing::immediately())}, {"r0", "r1"},
                         {"g0", "g1"});
}

// `G F g0` (grant liveness) over the output g0: the system controls g0, so it
// can simply never grant, making `F G !g0` realizable. Not well-separated.
TEST(test_grant_liveness_assumption_is_not_well_separated) {
    RealizabilityChecker checker;
    const Specification spec =
        with_arbiter_assumptions({continual("g0", timing::eventually())});
    expect(specification_is_not_well_separated(spec, checker),
           "well-separation: a grant-liveness assumption G F g0 is forcible by "
           "the system and so not well-separated");
}

// `G(r0 -> F g0)` mentions the output g0, yet falsifying it needs the input r0,
// which the environment can withhold forever. Well-separated despite the output
// reference -- exercises the output-atom fast path taking the solver route and
// still returning well-separated.
TEST(test_request_response_assumption_is_well_separated) {
    RealizabilityChecker checker;
    const Specification spec = with_arbiter_assumptions(
        {continual_when("r0", "g0", timing::eventually())});
    expect(
        !specification_is_not_well_separated(spec, checker),
        "well-separation: G(r0 -> F g0) is well-separated because falsifying "
        "it needs an input the environment controls");
}

TEST(test_filter_drops_only_the_non_well_separated_spec) {
    RealizabilityChecker checker;
    FilterFunction filter = make_well_separation_filter(checker);
    const Specification good =
        with_assumptions({continual("req", timing::always())});
    const Specification bad =
        with_assumptions({continual("grant", timing::always())});
    const auto survivors = filter({good, bad});
    expect(survivors.size() == 1,
           "well-separation filter: exactly one of the two specs should "
           "survive");
    expect(!specification_is_not_well_separated(survivors[0], checker),
           "well-separation filter: the survivor should be the well-separated "
           "spec");
}

// The filter asks whether `(assumptions) -> false` is *realizable*, so an
// undecided query must not fall back on "unrealizable" the way an
// admit-the-repair caller does: that would keep a candidate nobody checked,
// and the filter's own ltlsynt load is what makes timeouts likely. G(req ->
// grant) is genuinely well-separated (see above) and reaches the solver, so a
// budget too short to decide it must flip the verdict to not-well-separated.
TEST(test_undecided_query_reads_as_not_well_separated) {
    RealizabilityChecker checker;
    const Specification spec = with_assumptions(
        {continual_when("req", "grant", timing::immediately())});
    const std::size_t before = RealizabilityChecker::n_timeouts;
    {
        const ScopedLtlsyntTimeout timeout(std::chrono::milliseconds(1));
        expect(specification_is_not_well_separated(spec, checker),
               "well-separation: an undecided query should drop the candidate "
               "rather than keep it unchecked");
    }
    // The undecided outcome is memoised, so this checker keeps dropping the
    // candidate without re-running ltlsynt. That is the intended trade: the
    // budget is paid once, and the direction it resolves to is the safe one.
    expect(specification_is_not_well_separated(spec, checker),
           "well-separation: a memoised undecided query should keep dropping "
           "the candidate");
    expect(RealizabilityChecker::n_timeouts == before + 1,
           "well-separation: the memoised query should not re-run ltlsynt");
}

// ltlsynt does not only time out: SPOT 2.15.1 aborts with "Too many acceptance
// sets used" on specifications the search reaches on its own, and the runner
// raises on any output it cannot read as a verdict. Filters run outside the
// scoring pool's failure tolerance, so that throw used to end the run rather
// than cost one candidate. The throw is provoked here by handing ltlsynt an
// LTL string it cannot parse: the same unreadable output on the same code
// path, without the minutes of synthesis the acceptance-set abort costs.
TEST(test_raising_query_reads_as_not_well_separated) {
    RealizabilityChecker checker;
    Requirement assumption = continual("grant", timing::always());
    assumption.m_ltl = "grant &";
    const Specification spec = with_assumptions({assumption});
    const std::size_t before = WellSeparationStats::n_errors.load();
    expect(specification_is_not_well_separated(spec, checker),
           "well-separation: a query that raises should drop the candidate "
           "rather than escape the filter");
    expect(WellSeparationStats::n_errors.load() == before + 1,
           "well-separation: a raising query should be counted, so the drop "
           "was the error policy and not a verdict");
}

}  // namespace
