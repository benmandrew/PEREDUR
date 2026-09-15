#include <algorithm>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "requirement.hpp"
#include "runner/formaliser.hpp"
#include "runner/ltlfilt.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

// Every scope kind, paired with the FRETish clause Requirement::to_string emits
// for it, so a rejected sentence names the scope that produced it.
struct ScopeCase {
    ScopeKind m_kind = ScopeKind::Global;
    const char* m_name = "";
};

const std::vector<ScopeCase>& scope_cases() {
    static const std::vector<ScopeCase> cases = {
        {ScopeKind::Global, "Global"},
        {ScopeKind::In, "In"},
        {ScopeKind::NotIn, "NotIn"},
        {ScopeKind::Before, "Before"},
        {ScopeKind::After, "After"},
        {ScopeKind::OnlyIn, "OnlyIn"},
        {ScopeKind::OnlyBefore, "OnlyBefore"},
        {ScopeKind::OnlyAfter, "OnlyAfter"}};
    return cases;
}

// Tick counts stay at 2. At zero ticks FRET's own output contains the
// degenerate `F[0,-1] ...`, which SPOT will not parse, so those rows cannot be
// cross-checked against the CLI at all; test_scope_zero_ticks_is_unrelaxed
// pins them directly instead.
const std::vector<Timing>& timing_cases() {
    static const std::vector<Timing> timings = {timing::immediately(),
                                                timing::next_timepoint(),
                                                timing::within_ticks(2),
                                                timing::for_ticks(2),
                                                timing::after_ticks(2),
                                                timing::eventually(),
                                                timing::always(),
                                                timing::until(Formula("s")),
                                                timing::before(Formula("s"))};
    return timings;
}

Requirement scoped(const Scope& scope, const Timing& tim,
                   ConditionType condition_type) {
    return Requirement(Formula("c"), Formula("r"), tim, condition_type,
                       /*weakenable=*/true, /*removed=*/false, scope);
}

Scope make_scope(ScopeKind kind) {
    return kind == ScopeKind::Global ? Scope{} : Scope{kind, "m"};
}

// True when `lhs` implies `rhs`, expressed through ltl_equivalent so this needs
// no second ltlfilt wrapper: a -> b is valid exactly when a & b is equivalent
// to a.
bool ltl_implies(const std::string& lhs, const std::string& rhs) {
    return ltl_equivalent("(" + lhs + ") & (" + rhs + ")", "(" + lhs + ")");
}

// --- The lowering against the vendored FRET formaliser --------------------

// The load-bearing test for scopes. requirement_to_ltl reimplements FRET's
// scope semantics rather than shelling out to the formaliser per requirement,
// so nothing but a differential check establishes that the reimplementation is
// right. Equivalence rather than string equality, because FRET special-cases
// several rows -- `in m ... always` prints as G(m -> r) where the general form
// expands far larger.
void test_scope_agrees_with_formaliser() {
    RequirementFormaliser formaliser(formaliser_command());
    for (const ScopeCase& scope_case : scope_cases()) {
        for (const ConditionType ctype :
             {ConditionType::Trigger, ConditionType::Continual}) {
            for (const Timing& tim : timing_cases()) {
                const Requirement req =
                    scoped(make_scope(scope_case.m_kind), tim, ctype);
                const std::string fretish = req.to_string();
                const std::string cli = formaliser.formalise(fretish);
                std::string where(scope_case.m_name);
                where += " / ";
                where +=
                    ctype == ConditionType::Trigger ? "trigger" : "continual";
                where += " / ";
                where += to_string(tim);
                if (cli.empty()) {
                    std::string rejected = "scope: formaliser CLI rejected \"";
                    rejected += fretish;
                    rejected += "\" (";
                    rejected += where;
                    rejected += ")";
                    fail(rejected);
                }
                std::string message =
                    "scope: lowering disagrees with the FRET formaliser at ";
                message += where;
                message += "\n  FRETish: " + fretish;
                message += "\n  ours:    " + req.m_ltl;
                message += "\n  CLI:     " + cli;
                expect(ltl_equivalent(req.m_ltl, cli), message);
            }
        }
    }
}

// A bounded obligation falling due at the very timepoint the scope is entered
// has no room to be pre-empted by the boundary, so it is not relaxed. FRET
// agrees but prints the empty range as `F[0,-1] ...`, which SPOT rejects, so
// this is the one family the formaliser cannot arbitrate and it is pinned by
// hand. The fuzzer folds the empty range to false and compares these rows.
void test_scope_zero_ticks_is_unrelaxed() {
    const Requirement within =
        scoped(Scope{ScopeKind::In, "m"}, timing::within_ticks(0),
               ConditionType::Continual);
    const Requirement global_within =
        scoped(Scope{}, timing::within_ticks(0), ConditionType::Continual);
    expect(within.m_ltl.find("R (r)") == std::string::npos,
           "scope: `within 0` under a scope must carry no boundary disjunct");
    expect(global_within.m_ltl == "G((c) -> ((r)))",
           "scope: Global `within 0` must be unchanged, got " +
               global_within.m_ltl);
}

// --- Global scope is byte-identical to the pre-scope lowering -------------

// Not merely equivalent: every archived run, every memoisation key and the
// determinism goldens were recorded against these exact strings, so a
// reformatting of the Global path would invalidate all of them at once.
void test_global_scope_lowering_is_unchanged() {
    struct Row {
        Timing m_timing = timing::immediately();
        ConditionType m_type = ConditionType::Continual;
        const char* m_expected = "";
    };
    const std::vector<Row> rows = {
        {timing::immediately(), ConditionType::Continual, "G((c) -> (r))"},
        {timing::next_timepoint(), ConditionType::Continual, "G((c) -> X(r))"},
        {timing::eventually(), ConditionType::Continual, "G((c) -> F(r))"},
        {timing::always(), ConditionType::Continual, "G((c) -> G(r))"},
        {timing::within_ticks(1), ConditionType::Continual,
         "G((c) -> ((r) | X((r))))"},
        {timing::for_ticks(1), ConditionType::Continual,
         "G((c) -> ((r) & X((r))))"},
        {timing::after_ticks(0), ConditionType::Continual,
         "G((c) -> (!(r) & X((r))))"},
        {timing::always(), ConditionType::Trigger,
         "G((!(c) & X(c)) -> X(G(r))) & ((c) -> G(r))"},
        {timing::until(Formula("s")), ConditionType::Continual,
         "G((c) -> ((r) W (s)))"},
        {timing::before(Formula("s")), ConditionType::Continual,
         "G((c) -> ((r) R (!(s))))"},
    };
    for (const Row& row : rows) {
        const Requirement req = scoped(Scope{}, row.m_timing, row.m_type);
        expect(req.m_ltl == row.m_expected,
               std::string("scope: Global lowering moved at ") +
                   to_string(row.m_timing) + "\n  expected: " + row.m_expected +
                   "\n  got:      " + req.m_ltl);
    }
}

void test_global_scope_fretish_is_unchanged() {
    const Requirement req =
        scoped(Scope{}, timing::always(), ConditionType::Continual);
    expect(req.to_string() == "whenever c C shall always satisfy r",
           "scope: Global FRETish moved, got \"" + req.to_string() + "\"");
}

// --- The mutation order table --------------------------------------------

// Re-derives every edge scope_order claims, so a wrong entry fails here rather
// than surfacing later as a directional mutation that moved the wrong way. The
// non-edges are checked too: a missing edge that actually holds costs the
// search reach, and an edge that does not hold is unsound.
struct Edge {
    ScopeKind m_stronger;
    ScopeKind m_weaker;
};

// The table in src/genetic/mutation.cpp, restated independently so that a
// single edit cannot move both.
std::vector<Edge> expected_edges(const Timing& tim, ConditionType ctype) {
    const Edge notin_before{ScopeKind::NotIn, ScopeKind::Before};
    std::vector<Edge> from_global{{ScopeKind::Global, ScopeKind::In},
                                  {ScopeKind::Global, ScopeKind::NotIn},
                                  {ScopeKind::Global, ScopeKind::Before},
                                  {ScopeKind::Global, ScopeKind::After},
                                  notin_before};
    if (std::holds_alternative<timing::Eventually>(tim)) {
        if (ctype == ConditionType::Continual) {
            return {{ScopeKind::Global, ScopeKind::After}, notin_before};
        }
        return {notin_before};
    }
    if (ctype == ConditionType::Continual ||
        std::holds_alternative<timing::Always>(tim)) {
        return from_global;
    }
    return {{ScopeKind::Global, ScopeKind::Before}, notin_before};
}

// Checks every ordered pair of scopes at one (timing, condition type) cell
// against what scope_order claims for it. The non-edges are checked too: a
// missing edge that actually holds costs the search reach, and a claimed edge
// that does not hold is unsound.
void check_order_cell(const Timing& tim, ConditionType ctype) {
    const std::vector<Edge> edges = expected_edges(tim, ctype);
    const auto claimed = [&edges](ScopeKind from, ScopeKind target) {
        return std::any_of(
            edges.begin(), edges.end(), [from, target](const Edge& edge) {
                return edge.m_stronger == from && edge.m_weaker == target;
            });
    };
    for (const ScopeCase& lhs : scope_cases()) {
        for (const ScopeCase& rhs : scope_cases()) {
            if (lhs.m_kind == rhs.m_kind) {
                continue;
            }
            const bool holds =
                ltl_implies(scoped(make_scope(lhs.m_kind), tim, ctype).m_ltl,
                            scoped(make_scope(rhs.m_kind), tim, ctype).m_ltl);
            std::string where(lhs.m_name);
            where += " => ";
            where += rhs.m_name;
            where += " at ";
            where += to_string(tim);
            where +=
                ctype == ConditionType::Trigger ? " / trigger" : " / continual";
            if (claimed(lhs.m_kind, rhs.m_kind)) {
                expect(
                    holds,
                    "scope_order claims an edge that does not hold: " + where);
            } else {
                expect(!holds,
                       "scope_order is missing an edge that holds: " + where);
            }
        }
    }
}

void test_scope_order_is_pinned() {
    // A conjunction stop alongside timing_cases()' atom, since scope_order
    // reads only the timing's kind and so claims its cells for every stop.
    std::vector<Timing> timings = timing_cases();
    timings.push_back(timing::until(Formula("s & t")));
    timings.push_back(timing::before(Formula("s & t")));
    for (const ConditionType ctype :
         {ConditionType::Trigger, ConditionType::Continual}) {
        for (const Timing& tim : timings) {
            check_order_cell(tim, ctype);
        }
    }
}

// Where the stop timings sit in the timing order, which the timing arm and the
// timing similarity term both rely on. Over the Global continual lowering,
// `until s` has one edge into the other kinds, from Always above it, and
// `before s` has none. Each moves only along its stop, positively in `until`
// and negatively in `before`, and FRET's pairing of the two is not an order
// between them.
void test_stop_timing_order_is_pinned() {
    const auto ltl = [](const Timing& tim) {
        return scoped(Scope{}, tim, ConditionType::Continual).m_ltl;
    };
    const Timing until_s = timing::until(Formula("s"));
    const Timing before_s = timing::before(Formula("s"));
    for (const Timing& tim : timing_cases()) {
        if (timing_stop(tim) != nullptr) {
            continue;
        }
        const std::string where = to_string(tim);
        const bool is_always = std::holds_alternative<timing::Always>(tim);
        expect(ltl_implies(ltl(tim), ltl(until_s)) == is_always,
               "stop order: " + where + (is_always ? " must" : " must not") +
                   " imply until s");
        expect(!ltl_implies(ltl(until_s), ltl(tim)),
               "stop order: until s must not imply " + where);
        expect(!ltl_implies(ltl(tim), ltl(before_s)) &&
                   !ltl_implies(ltl(before_s), ltl(tim)),
               "stop order: before s must be incomparable with " + where);
    }
    const Timing until_either = timing::until(Formula("s | t"));
    const Timing before_either = timing::before(Formula("s | t"));
    expect(ltl_implies(ltl(until_s), ltl(until_either)) &&
               !ltl_implies(ltl(until_either), ltl(until_s)),
           "stop order: until must weaken strictly as its stop weakens");
    expect(ltl_implies(ltl(before_either), ltl(before_s)) &&
               !ltl_implies(ltl(before_s), ltl(before_either)),
           "stop order: before must strengthen strictly as its stop weakens");
    expect(!ltl_implies(ltl(until_s), ltl(before_s)) &&
               !ltl_implies(ltl(before_s), ltl(until_s)),
           "stop order: until s and before s must be incomparable");
    expect(ltl_equivalent(ltl(timing::until(Formula("false"))),
                          ltl(timing::always())),
           "stop order: until false must be equivalent to always");
    expect(ltl_equivalent(ltl(timing::before(Formula("false"))), "true"),
           "stop order: before false must be valid");
}

// Continual implies Trigger everywhere, which is what makes the condition-type
// arm directional. The converse holds only where the two coincide: at `always`
// under a plain scope, and at `eventually` under an `only` scope, whose
// lowering applies the dual wrapper to the negated body and so moves the
// coinciding cell to the dual timing. The converse is checked too, since a
// cell that stopped being strict would make the arm a no-op there.
void test_condition_type_order_is_pinned() {
    for (const ScopeCase& scope_case : scope_cases()) {
        for (const Timing& tim : timing_cases()) {
            const Scope scope = make_scope(scope_case.m_kind);
            const std::string continual =
                scoped(scope, tim, ConditionType::Continual).m_ltl;
            const std::string trigger =
                scoped(scope, tim, ConditionType::Trigger).m_ltl;
            const std::string where =
                std::string(scope_case.m_name) + " / " + to_string(tim);
            expect(ltl_implies(continual, trigger),
                   "condition type: continual must imply trigger at " + where);
            const bool coincide =
                is_only_scope(scope_case.m_kind)
                    ? std::holds_alternative<timing::Eventually>(tim)
                    : std::holds_alternative<timing::Always>(tim);
            expect(ltl_implies(trigger, continual) == coincide,
                   std::string("condition type: trigger ") +
                       (coincide ? "must" : "must not") +
                       " imply continual at " + where);
        }
    }
}

// --- Identity ------------------------------------------------------------

void test_scope_is_part_of_requirement_identity() {
    const Requirement global =
        scoped(Scope{}, timing::always(), ConditionType::Continual);
    const Requirement in_mode = scoped(
        Scope{ScopeKind::In, "m"}, timing::always(), ConditionType::Continual);
    const Requirement other_mode = scoped(
        Scope{ScopeKind::In, "n"}, timing::always(), ConditionType::Continual);
    expect(!(global == in_mode),
           "scope: a scoped requirement must not equal an unscoped one");
    expect(!(in_mode == other_mode),
           "scope: two scopes over different modes must not be equal");
    // The fitness cache keys on the specification's hash, so two requirements
    // that differ only in scope must not collide and inherit each other's
    // score.
    expect(
        std::hash<Requirement>{}(global) != std::hash<Requirement>{}(in_mode),
        "scope: the hash must separate a scoped requirement from a global "
        "one");
    expect(std::hash<Requirement>{}(in_mode) !=
               std::hash<Requirement>{}(other_mode),
           "scope: the hash must separate two scopes over different modes");
}

void test_atom_prefix_tags_the_mode() {
    const Requirement req = scoped(Scope{ScopeKind::In, "m"}, timing::always(),
                                   ConditionType::Continual);
    const Requirement tagged = add_atom_prefix(req);
    expect(tagged.m_scope.m_mode == std::string(k_atom_prefix) + "m",
           "scope: add_atom_prefix must tag the mode, got " +
               tagged.m_scope.m_mode);
    expect(tagged.m_ltl.find(std::string(k_atom_prefix) + "m") !=
               std::string::npos,
           "scope: the tagged mode must reach the lowered formula");
    const Requirement stripped = strip_atom_prefix(tagged);
    expect(stripped == req, "scope: strip_atom_prefix must invert the tagging");
}

void test_environment_signals_appends_modes() {
    const Specification without({}, {}, {"a"}, {"b"});
    expect(environment_signals(without) == std::vector<std::string>{"a"},
           "scope: a specification with no modes must keep its input list");
    const Specification with({}, {}, {"a"}, {"b"}, {"m"});
    const std::vector<std::string> expected = {"a", "m"};
    expect(environment_signals(with) == expected,
           "scope: modes must join the environment side of the partition");
}

}  // namespace

void run_scope_tests() {
    test_global_scope_lowering_is_unchanged();
    test_global_scope_fretish_is_unchanged();
    test_scope_zero_ticks_is_unrelaxed();
    test_scope_is_part_of_requirement_identity();
    test_atom_prefix_tags_the_mode();
    test_environment_signals_appends_modes();
    test_scope_agrees_with_formaliser();
    test_condition_type_order_is_pinned();
    test_scope_order_is_pinned();
    test_stop_timing_order_is_pinned();
}
