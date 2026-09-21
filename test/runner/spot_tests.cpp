#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "requirement.hpp"
#include "runner/atom_names.hpp"
#include "runner/spot.hpp"
#include "runner/tool_paths.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

Requirement make_req(const std::string& trigger, const std::string& response,
                     Timing timing, std::string spec) {
    Requirement req{Formula(trigger), Formula(response), std::move(timing)};
    req.m_ltl = std::move(spec);
    return req;
}

// No timeout is configured in the tests, so every query below must come back
// decided; an undecided answer is a failure rather than a third case to fold
// into the expectation.
bool realizable(RealizabilityChecker& checker, const Specification& spec) {
    const std::optional<bool> decided = checker.check_realizability(spec);
    if (!decided.has_value()) {
        fail("spot-runner: ltlsynt reported undecided with no timeout set");
    }
    return *decided;
}

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

// As above, for the counting path's budget.
class ScopedLtl2tgbaTimeout {
   public:
    explicit ScopedLtl2tgbaTimeout(std::chrono::milliseconds timeout) {
        set_ltl2tgba_timeout(timeout);
    }
    ScopedLtl2tgbaTimeout(const ScopedLtl2tgbaTimeout&) = delete;
    ScopedLtl2tgbaTimeout& operator=(const ScopedLtl2tgbaTimeout&) = delete;
    ScopedLtl2tgbaTimeout(ScopedLtl2tgbaTimeout&&) = delete;
    ScopedLtl2tgbaTimeout& operator=(ScopedLtl2tgbaTimeout&&) = delete;
    ~ScopedLtl2tgbaTimeout() {
        set_ltl2tgba_timeout(std::chrono::milliseconds(0));
    }
};

bool counting_succeeds(const std::string& formula) {
    try {
        run_ltl2tgba_for_counting(formula);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void test_realizable_immediately() {
    RealizabilityChecker checker;
    Requirement req =
        make_req("p", "q", timing::immediately(), "G((p) -> (q))");
    expect(realizable(checker, Specification({}, {req}, {"p"}, {"q"})),
           "spot-runner: G(p -> q) should be realisable");
}

void test_unrealizable_immediately() {
    RealizabilityChecker checker;
    Requirement req = make_req("true", "false", timing::immediately(),
                               "G((true) -> (false))");
    expect(!realizable(checker, Specification({}, {req}, {}, {})),
           "spot-runner: G(true -> false) should be unrealisable");
}

void test_realizable_next_timepoint() {
    RealizabilityChecker checker;
    Requirement req =
        make_req("p", "q", timing::next_timepoint(), "G((p) -> (X(q)))");
    expect(realizable(checker, Specification({}, {req}, {"p"}, {"q"})),
           "spot-runner: G(p -> Xq) should be realisable");
}

void test_realizable_within_ticks() {
    RealizabilityChecker checker;
    Requirement req =
        make_req("p", "q", timing::within_ticks(2), "G((p) -> ((q) | X(q)))");
    expect(realizable(checker, Specification({}, {req}, {"p"}, {"q"})),
           "spot-runner: G(p -> q | Xq) should be realisable");
}

void test_realizable_for_ticks() {
    RealizabilityChecker checker;
    Requirement req =
        make_req("p", "q", timing::for_ticks(2), "G((p) -> ((q) & X(q)))");
    expect(realizable(checker, Specification({}, {req}, {"p"}, {"q"})),
           "spot-runner: G(p -> q & Xq) should be realisable");
}

void test_realizable_eventually() {
    RealizabilityChecker checker;
    Requirement req =
        make_req("p", "q", timing::eventually(), "G((p) -> (F(q)))");
    expect(realizable(checker, Specification({}, {req}, {"p"}, {"q"})),
           "spot-runner: G(p -> Fq) should be realisable");
}

void test_realizable_with_assumption() {
    RealizabilityChecker checker;
    // Assumption: p persists once set (environment constraint)
    Requirement assumption =
        make_req("p", "p", timing::next_timepoint(), "G((p) -> (X(p)))");
    // Guarantee: when p holds, q must hold
    Requirement guarantee =
        make_req("p", "q", timing::immediately(), "G((p) -> (q))");
    expect(realizable(checker,
                      Specification({assumption}, {guarantee}, {"p"}, {"q"})),
           "spot-runner: G(p->Xp) -> G(p->q) should be realizable");
}

void test_assumption_enables_joint_realizability() {
    RealizabilityChecker checker;
    // These two guarantees are jointly unrealizable on their own
    Requirement guarantee1 =
        make_req("a", "b", timing::next_timepoint(), "G((a) -> (X(b)))");
    Requirement guarantee2 =
        make_req("a", "!b", timing::next_timepoint(), "G((a) -> (X(!b)))");
    expect(!realizable(checker, Specification({}, {guarantee1, guarantee2},
                                              {"a"}, {"b"})),
           "spot-runner: G(a->Xb) & G(a->X!b) should be unrealizable without "
           "assumption");
    // Assumption G(!a): environment never sets a — makes both guarantees
    // vacuously true
    Requirement assum =
        make_req("true", "!a", timing::immediately(), "G((true) -> (!a))");
    expect(realizable(checker, Specification({assum}, {guarantee1, guarantee2},
                                             {"a"}, {"b"})),
           "spot-runner: G(!a) -> (G(a->Xb) & G(a->X!b)) should be realizable");
}

void test_individually_realizable_but_jointly_unrealizable() {
    RealizabilityChecker checker;
    // Req1: G(a -> X b)
    Requirement req1 =
        make_req("a", "b", timing::next_timepoint(), "G((a) -> (X(b)))");
    // Req2: G(a -> X !b)
    Requirement req2 =
        make_req("a", "!b", timing::next_timepoint(), "G((a) -> (X(!b)))");

    // Each is realizable alone
    expect(realizable(checker, Specification({}, {req1}, {"a"}, {"b"})),
           "spot-runner: G(a -> X b) should be realizable");
    expect(realizable(checker, Specification({}, {req2}, {"a"}, {"b"})),
           "spot-runner: G(a -> X !b) should be realizable");
    // Together, not realizable
    expect(!realizable(checker, Specification({}, {req1, req2}, {"a"}, {"b"})),
           "spot-runner: G(a -> X b) & G(a -> X !b) should be unrealizable");
}

// A timeout is undecided, not unrealizable, and the undecided outcome is
// memoised: ltlsynt is deterministic, so a formula that blows the budget blows
// it every time and the exec is worth paying once. A 1 ms budget cannot
// outlast even ltlsynt's process startup, so the first call times out whatever
// the host; the second is served from the cache, which is why lifting the
// budget afterwards does not change the answer.
void test_timeout_is_undecided_and_cached() {
    RealizabilityChecker checker;
    const Requirement req =
        make_req("p", "q", timing::immediately(), "G((p) -> (q))");
    const Specification spec({}, {req}, {"p"}, {"q"});
    const std::size_t before = RealizabilityChecker::n_timeouts;
    {
        const ScopedLtlsyntTimeout timeout(std::chrono::milliseconds(1));
        expect(!checker.check_realizability(spec).has_value(),
               "spot-runner: a timed-out query should report undecided rather "
               "than unrealizable");
    }
    expect(RealizabilityChecker::n_timeouts == before + 1,
           "spot-runner: a timed-out query should be tallied");
    expect(!checker.check_realizability(spec).has_value(),
           "spot-runner: the undecided outcome should be memoised");
    expect(RealizabilityChecker::n_timeouts == before + 1,
           "spot-runner: a memoised timeout should not re-run ltlsynt");
    // A fresh checker has its own cache, so the query is decided there: what
    // is cached is one checker's non-answer, not a fact about the formula.
    RealizabilityChecker uncached;
    expect(realizable(uncached, spec),
           "spot-runner: the formula itself is realizable once a workable "
           "budget is allowed");
}

// The counting path's abandonment is memoised the same way, and re-raised as
// the identical error, so a formula whose determinization blows up costs one
// exec per run rather than one per occurrence.
void test_ltl2tgba_timeout_is_cached() {
    // Unique to this test: the cache is process-wide and this poisons the key.
    const std::string formula = "G((a1 & b1) -> X(c1 | d1))";
    const std::size_t before = Ltl2tgbaStats::n_timeouts;
    {
        const ScopedLtl2tgbaTimeout timeout(std::chrono::milliseconds(1));
        expect(!counting_succeeds(formula),
               "spot-runner: a timed-out determinization should raise");
    }
    expect(Ltl2tgbaStats::n_timeouts == before + 1,
           "spot-runner: a timed-out determinization should be tallied");
    expect(!counting_succeeds(formula),
           "spot-runner: the abandonment should be memoised and re-raised");
    expect(Ltl2tgbaStats::n_timeouts == before + 1,
           "spot-runner: a memoised abandonment should not re-run ltl2tgba");
}

// SPOT 2.15.1's ltl2tgba exits 2 with "print_hoa(): automaton is complete but
// prop_complete()==false" on formulae that reduce to a tautology. The wrapper
// must treat that as the trivially-true (universal) automaton rather than
// re-raising it as a scoring error, so a genuinely-true formula never counts
// against the run's scoring-failure tolerance.
void test_tautology_exit2_yields_universal_automaton() {
    // Minimal known trigger; ltlfilt simplifies it to 1.
    const std::string tautology = "G((a & !((b & !c) -> d)) -> b)";
    const std::size_t before = Ltl2tgbaStats::n_tautology_substitutions;
    std::string hoa;
    try {
        hoa = run_ltl2tgba_for_counting(tautology);
    } catch (const std::exception& e) {
        fail(std::string("run_ltl2tgba_for_counting threw on a tautology "
                         "instead of substituting the universal automaton: ") +
             e.what());
        return;
    }
    expect(
        hoa.find("acc-name: all") != std::string::npos &&
            hoa.find("AP: 0") != std::string::npos &&
            hoa.find("[t] 0") != std::string::npos,
        "spot-runner: exit-2 tautology should yield the universal automaton");
    expect(Ltl2tgbaStats::n_tautology_substitutions == before + 1,
           "spot-runner: a tautology substitution should be tallied");
    // The substituted result is cached, so a second call is a hit and does not
    // re-trigger the bug or tally again.
    const std::string hoa2 = run_ltl2tgba_for_counting(tautology);
    expect(
        hoa2 == hoa && Ltl2tgbaStats::n_tautology_substitutions == before + 1,
        "spot-runner: a substituted tautology should be memoised");
}

// Clears the variable however the test leaves, expect() throwing past the
// end of a case that fails.
class ScopedEnvVar {
   public:
    explicit ScopedEnvVar(const char* name) : m_name(name) {}
    ~ScopedEnvVar() { unsetenv(m_name); }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
    ScopedEnvVar(ScopedEnvVar&&) = delete;
    ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

    void set(const char* value) const { setenv(m_name, value, 1); }
    void clear() const { unsetenv(m_name); }

   private:
    const char* m_name;
};

// spot_bin_dir() and its four siblings each memoise their answer in a
// function-local static, so calling one of them twice under two environments
// cannot distinguish these cases. The helper underneath them is where the
// logic is, and is tested here rather than through any one caller.
void test_tool_path_env_wins_over_compiled_default() {
    constexpr const char* k_var = "PEREDUR_TOOL_PATH_TEST";
    const ScopedEnvVar env(k_var);

    env.set("/override/bin");
    const ToolPath overridden = tool_path_from_env(k_var, "/compiled/bin");
    expect(overridden.m_path == "/override/bin",
           "tool-paths: a set, non-empty variable should win over the "
           "compiled-in default");
    expect(overridden.m_from_env,
           "tool-paths: an overridden path should report the environment as "
           "its source");
}

void test_tool_path_empty_env_falls_back() {
    constexpr const char* k_var = "PEREDUR_TOOL_PATH_TEST";
    const ScopedEnvVar env(k_var);

    env.set("");
    const ToolPath resolved = tool_path_from_env(k_var, "/compiled/bin");
    expect(resolved.m_path == "/compiled/bin",
           "tool-paths: an empty variable should fall back to the compiled-in "
           "default");
    expect(!resolved.m_from_env,
           "tool-paths: a fallback path should not report the environment as "
           "its source");
}

void test_tool_path_unset_env_falls_back() {
    constexpr const char* k_var = "PEREDUR_TOOL_PATH_TEST";
    const ScopedEnvVar env(k_var);

    env.clear();
    const ToolPath resolved = tool_path_from_env(k_var, "/compiled/bin");
    expect(resolved.m_path == "/compiled/bin",
           "tool-paths: an unset variable should fall back to the compiled-in "
           "default");
    expect(!resolved.m_from_env,
           "tool-paths: a fallback path should not report the environment as "
           "its source");
}

// Realizability is monotone in both sides, and the subsumption table answers
// from that rather than from an exec. A wrong answer here is silent, so every
// verdict it produces is checked against the one ltlsynt gives for the same
// query on a fresh checker.
void test_subsumption_agrees_with_ltlsynt() {
    const std::vector<std::string> inputs = {"req"};
    const std::vector<std::string> outputs = {"grant"};
    // `responds` alone is realizable; with `silent` it is not, `silent`
    // demanding grant be withheld forever while `responds` demands it
    // eventually follow a request.
    const std::string responds = "G((req) -> (F(grant)))";
    const std::string silent = "G(!(grant))";
    const std::string always_asks = "G(req)";

    struct Query {
        std::vector<std::string> m_assumptions;
        std::vector<std::string> m_guarantees;
    };
    // Ordered so the later queries stand in a subsuming relation to the
    // earlier ones: a realizable {g0} makes {g0} under more assumptions
    // realizable, and an unrealizable {g0, g1} makes it unrealizable with
    // fewer assumptions.
    const std::vector<Query> queries = {
        {{}, {responds}}, {{always_asks}, {responds}}, {{}, {responds, silent}},
        {{}, {silent}},   {{always_asks}, {silent}},   {{}, {responds}},
    };

    RealizabilityChecker subsuming;
    for (const Query& query : queries) {
        SpecificationSides sides;
        sides.m_assumptions = query.m_assumptions;
        sides.m_guarantees = query.m_guarantees;
        std::string formula;
        for (const std::string& guarantee : query.m_guarantees) {
            formula += formula.empty() ? "(" + guarantee + ")"
                                       : " & (" + guarantee + ")";
        }
        if (!query.m_assumptions.empty()) {
            std::string implication = "(";
            for (const std::string& assumption : query.m_assumptions) {
                if (implication.size() > 1) {
                    implication += " & ";
                }
                implication += "(" + assumption + ")";
            }
            implication += ") -> (";
            implication += formula;
            implication += ")";
            formula = implication;
        }
        const std::optional<bool> with_table =
            subsuming.check_realizability_ltl(formula, inputs, outputs, sides);
        // A fresh checker shares no table and no memo, so this is the tool.
        RealizabilityChecker fresh;
        const std::optional<bool> from_tool =
            fresh.check_realizability_ltl(formula, inputs, outputs);
        expect(with_table == from_tool,
               "spot-runner: the subsumption table disagreed with ltlsynt on " +
                   formula);
    }
    expect(RealizabilityChecker::n_subsumed > 0,
           "spot-runner: the subsumption table should have answered at least "
           "one of these queries without an exec");
}

// The name rules are asymmetric because the two SPOT failures are. Uppercase
// breaks `ltlsynt --ins` matching alone, and the engine passes `--ins` only,
// so it costs an input its side of the partition and leaves outputs untouched.
// The leading-operator rule holds on both sides, since it is a parse.
void test_atom_names_accept_safe_names() {
    const std::optional<std::string> unsafe =
        runner::first_unsafe_atom_name({"r0", "iap_state_nominal", "f_a", "x1"},
                                       {"STATE_FAULT", "System", "G0", "F1"});
    expect(!unsafe.has_value(),
           "atom-names: uppercase outputs and digit-suffixed operator letters "
           "are safe");
}

void test_atom_names_reject_uppercase_input() {
    const std::optional<std::string> unsafe =
        runner::first_unsafe_atom_name({"a_in", "A_in"}, {"b"});
    expect(unsafe.has_value(), "atom-names: an uppercase input is rejected");
    expect(unsafe.value_or("").find("A_in") != std::string::npos,
           "atom-names: the message names the offending input");
}

void test_atom_names_reject_operator_lead() {
    const std::optional<std::string> as_input =
        runner::first_unsafe_atom_name({"fail", "Gate"}, {"b"});
    expect(as_input.has_value(),
           "atom-names: an input reading as G(ate) is rejected");
    const std::optional<std::string> as_output =
        runner::first_unsafe_atom_name({"a"}, {"X_state"});
    expect(as_output.has_value(),
           "atom-names: an output reading as X(_state) is rejected");
}

void test_atom_names_reject_empty_name() {
    const std::optional<std::string> unsafe =
        runner::first_unsafe_atom_name({"a", ""}, {"b"});
    expect(unsafe.has_value(), "atom-names: an empty name is rejected");
}

}  // namespace

void run_spot_runner_tests() {
    test_tool_path_env_wins_over_compiled_default();
    test_tool_path_empty_env_falls_back();
    test_tool_path_unset_env_falls_back();
    test_subsumption_agrees_with_ltlsynt();
    test_tautology_exit2_yields_universal_automaton();
    test_realizable_eventually();
    test_realizable_immediately();
    test_unrealizable_immediately();
    test_realizable_next_timepoint();
    test_realizable_within_ticks();
    test_realizable_for_ticks();
    test_realizable_with_assumption();
    test_assumption_enables_joint_realizability();
    test_individually_realizable_but_jointly_unrealizable();
    test_timeout_is_undecided_and_cached();
    test_ltl2tgba_timeout_is_cached();
    test_atom_names_accept_safe_names();
    test_atom_names_reject_uppercase_input();
    test_atom_names_reject_operator_lead();
    test_atom_names_reject_empty_name();
}
