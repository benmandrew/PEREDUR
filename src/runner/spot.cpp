#include "runner/spot.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "filter/implication_check.hpp"
#include "formula_key.hpp"
#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/formula_input.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "runner/tool_stats.hpp"
#include "tool_paths.hpp"

namespace {

// Per-call wall-clock budget for the ltlsynt exec, in milliseconds; 0 disables
// the timeout. Set once at startup from Config::ltlsynt_timeout, read by every
// worker, hence atomic. ltlsynt normally decides in milliseconds, but hard
// synthesis queries the genetic search stumbles onto can run for minutes with
// no upper bound, so an unbounded run stalls on the tail.
std::atomic<std::int64_t> g_ltlsynt_timeout_ms{0};

// Per-call wall-clock budget for the ltl2tgba counting exec, in milliseconds; 0
// disables the timeout. Set once at startup from Config::ltl2tgba_timeout, read
// by every scoring worker, hence atomic. Mirrors g_ltlsynt_timeout_ms: ltl2tgba
// has no internal timeout and its -D determinization can run unbounded on the
// deeply nested formulae the search builds, orphaning a multi-GB process when
// the run is torn down.
std::atomic<std::int64_t> g_ltl2tgba_timeout_ms{0};

std::string join_comma(const std::vector<std::string>& items) {
    std::string result;
    bool first = true;
    for (const auto& item : items) {
        if (!first) {
            result += ',';
        }
        result += item;
        first = false;
    }
    return result;
}

// The universal (trivially-true) automaton, in the exact HOA shape ltl2tgba
// itself emits for the constant `true`: one accepting state over zero atoms
// with a `[t]` self-loop. Substituted for the exit-2-on-tautology bug below;
// parses to the same TransferSystem a genuine tautology would.
constexpr const char* k_universal_hoa =
    "HOA: v1\n"
    "name: \"1\"\n"
    "States: 1\n"
    "Start: 0\n"
    "AP: 0\n"
    "acc-name: all\n"
    "Acceptance: 0 t\n"
    "properties: trans-labels explicit-labels state-acc complete\n"
    "properties: deterministic stutter-invariant weak\n"
    "--BODY--\n"
    "State: 0\n"
    "[t] 0\n"
    "--END--\n";

// SPOT 2.15.1's ltl2tgba aborts with exit 2 and this stderr line when a formula
// reduces to a tautology (the printed automaton is universal, hence complete,
// but its prop_complete() flag was left unset). The signature is stable across
// the invocation's binary-path prefix.
bool is_tautology_print_error(const ProcessResult& result) {
    return result.m_exit_code == 2 &&
           result.m_output.find("automaton is complete but prop_complete()") !=
               std::string::npos;
}

// ltlsynt refuses a formula needing more acceptance sets than SPOT's
// compile-time ceiling, 32 unless SPOT was configured with
// --enable-max-accsets=N. It is telling us it cannot answer, which is what a
// timeout says too, so it resolves to undecided rather than ending the run.
// Matched by signature rather than folded into the fallback for the reason
// parse_realizability_output throws on everything else: output nobody has
// recognised must never be read as a verdict.
bool is_acceptance_set_limit_error(const ProcessResult& result) {
    return result.m_output.find("Too many acceptance sets used") !=
           std::string::npos;
}

std::optional<bool> parse_realizability_output(const ProcessResult& result) {
    if (result.m_output.find("UNREALIZABLE") != std::string::npos) {
        return false;
    }
    if (result.m_output.find("REALIZABLE") != std::string::npos) {
        return true;
    }
    if (is_acceptance_set_limit_error(result)) {
        return std::nullopt;
    }
    // ltlsynt's output crossed a process boundary and didn't match either
    // expected form: don't let assert() (a no-op in release builds) treat
    // this as UNREALIZABLE and cache a fabricated result.
    throw std::runtime_error("unrecognized ltlsynt output: " + result.m_output);
}

}  // namespace

RealizabilityChecker& global_real_checker() {
    static RealizabilityChecker instance;
    return instance;
}

std::string spot_bin_dir() {
#ifdef SPOT_BIN_DIR
    static const ToolPath k_dir =
        tool_path_from_env("PEREDUR_SPOT_BIN_DIR", SPOT_BIN_DIR);
    return k_dir.m_path;
#else
    assert(false);
    return "";
#endif
}

std::string ltlsynt_path() { return spot_bin_dir() + "/ltlsynt"; }

void RealizabilityChecker::set_timeout(std::chrono::milliseconds timeout) {
    g_ltlsynt_timeout_ms.store(timeout.count());
}

void set_ltl2tgba_timeout(std::chrono::milliseconds timeout) {
    g_ltl2tgba_timeout_ms.store(timeout.count());
}

std::string ltl2tgba_path() { return spot_bin_dir() + "/ltl2tgba"; }

std::string run_ltl2tgba_for_counting(const std::string& formula) {
    // No `ltlfilt --simplify` pre-pass here, unlike the black caller.
    // ltl2tgba simplifies internally, so it is redundant -- and ltlfilt
    // --simplify blows up super-exponentially on the deeply nested-X
    // conjunctions this path builds for atom-rich, deep-horizon requirement
    // pairs (e.g. WithinTicks(20) over ~10 atoms): ~1s at 12 ticks, ~21s at
    // 15, unbounded by 20, where ltl2tgba yields the identical automaton from
    // the raw formula in milliseconds. Passing the formula straight through
    // avoids that cliff.
    // The canonical key, not the renamed one: the value is an automaton whose
    // transitions are labelled with the atoms themselves, so a caller reading
    // it back needs its own names on those labels (see formula_key.hpp).
    const std::string& key = formula_key::canonical(formula);
    static std::unordered_map<std::string, std::string> cache;
    // Formulae whose determinization was abandoned at the budget. Memoised
    // like the automata themselves: the blowup is a property of the formula,
    // not of the moment, so re-running it buys the same timeout at full price
    // and the individual is dropped either way.
    static std::unordered_set<std::string> timed_out;
    static std::mutex cache_mutex;
    {
        std::scoped_lock lock(cache_mutex);
        const auto found = cache.find(key);
        if (found != cache.end()) {
            Ltl2tgbaStats::n_cache_hits++;
            return found->second;
        }
        if (timed_out.count(key) != 0) {
            // Same error as the exec would have raised, so a caller sees one
            // behaviour whether or not this formula has been tried before.
            Ltl2tgbaStats::n_cache_hits++;
            throw std::runtime_error("ltl2tgba timed out for formula: " +
                                     formula);
        }
        Ltl2tgbaStats::n_cache_misses++;
    }
    const std::string binary = ltl2tgba_path();
    assert(access(binary.c_str(), F_OK) == 0);
    const auto timeout =
        std::chrono::milliseconds(g_ltl2tgba_timeout_ms.load());
    const ProcessResult result = execute_and_capture_with_input(
        {binary, "-D", "-S", "-H", "-F", "-"}, spot_formula_line(key), timeout);
    if (result.m_timed_out) {
        // The determinization did not finish within budget; drop the individual
        // (counted against max_scoring_failure_rate) rather than caching a
        // partial result or stalling the run. execute_and_capture has already
        // SIGKILLed and reaped the child, so no process is left behind.
        {
            std::scoped_lock lock(cache_mutex);
            record_exec<Ltl2tgbaStats>(result);
            timed_out.insert(key);
        }
        throw std::runtime_error("ltl2tgba timed out for formula: " + formula);
    }
    if (is_tautology_print_error(result)) {
        // The formula is a tautology: it accepts every trace, so the universal
        // automaton is the correct result, not a scoring failure. Substituting
        // it (rather than letting the throw drop the individual) keeps a
        // genuinely-true formula from counting against the run's
        // max_scoring_failure_rate tolerance.
        std::scoped_lock lock(cache_mutex);
        record_exec<Ltl2tgbaStats>(result);
        Ltl2tgbaStats::n_tautology_substitutions++;
        cache.emplace(key, k_universal_hoa);
        return k_universal_hoa;
    }
    if (result.m_exit_code != 0) {
        // A non-zero exit here (e.g. a subprocess spawn failure under heavy
        // concurrent forking) must not be cached as a successful result
        throw std::runtime_error("ltl2tgba exited with code " +
                                 std::to_string(result.m_exit_code) +
                                 " for formula: " + formula);
    }
    std::scoped_lock lock(cache_mutex);
    record_exec<Ltl2tgbaStats>(result);
    cache.emplace(key, result.m_output);
    return result.m_output;
}

namespace {

// A 64-bit signature of an id set. A superset test needs every bit of the
// smaller side present in the larger, so `(small & ~large) != 0` rejects an
// entry outright and the sorted-vector walk only runs on what survives.
std::uint64_t signature_of(const std::vector<int>& ids) {
    std::uint64_t signature = 0;
    for (const int conjunct_id : ids) {
        signature |= std::uint64_t{1}
                     << (static_cast<unsigned>(conjunct_id) % 64U);
    }
    return signature;
}

// Both vectors are sorted and duplicate-free, so this is one merge.
bool includes_sorted(const std::vector<int>& superset,
                     const std::vector<int>& subset) {
    return std::includes(superset.begin(), superset.end(), subset.begin(),
                         subset.end());
}

// A query replacing more conjuncts than this is not ordered against an entry:
// past a few, the rule calls cost more than the scan saves.
constexpr std::size_t k_rule_difference_cap = 4;

// The ids of @p lhs missing from @p rhs, into @p out, or false once there are
// more than k_rule_difference_cap. A bit of the signature difference is at
// least one missing id, so the popcount rejects most entries before the walk.
bool bounded_difference(const std::vector<int>& lhs,
                        std::uint64_t lhs_signature,
                        const std::vector<int>& rhs,
                        std::uint64_t rhs_signature, std::vector<int>& out) {
    if (std::bitset<64>(lhs_signature & ~rhs_signature).count() >
        k_rule_difference_cap) {
        return false;
    }
    out.clear();
    auto cursor = rhs.begin();
    for (const int conjunct_id : lhs) {
        while (cursor != rhs.end() && *cursor < conjunct_id) {
            ++cursor;
        }
        if (cursor != rhs.end() && *cursor == conjunct_id) {
            continue;
        }
        if (out.size() == k_rule_difference_cap) {
            return false;
        }
        out.push_back(conjunct_id);
    }
    return true;
}

// Whether ltlsynt would play @p ltl with no environment inputs. With no
// declared inputs it is told `--outs`, and any atom outside that list becomes
// an input, so every atom the formula mentions must be a declared output. A
// formula the parser cannot read is left to ltlsynt.
bool plays_without_environment(const std::string& ltl,
                               const std::vector<std::string>& inputs,
                               const std::vector<std::string>& outputs) {
    if (!inputs.empty()) {
        return false;
    }
    const std::optional<Formula> parsed = Formula::try_parse(ltl);
    if (!parsed.has_value()) {
        return false;
    }
    std::unordered_set<std::string> atoms;
    collect_atoms(*parsed, atoms);
    const std::unordered_set<std::string> output_set(outputs.begin(),
                                                     outputs.end());
    return std::all_of(
        atoms.begin(), atoms.end(), [&output_set](const std::string& name) {
            return name == "true" || name == "false" || name == "1" ||
                   name == "0" || output_set.count(name) != 0;
        });
}

}  // namespace

std::vector<int> RealizabilityChecker::intern_side(
    const std::vector<std::string>& conjuncts,
    const std::vector<const Requirement*>& requirements) {
    assert(requirements.empty() || requirements.size() == conjuncts.size());
    std::vector<int> ids;
    ids.reserve(conjuncts.size());
    for (std::size_t position = 0; position < conjuncts.size(); ++position) {
        const std::string& conjunct = conjuncts[position];
        const Requirement* requirement =
            requirements.empty() ? nullptr : requirements[position];
        // Canonically, so that a re-spelling of a conjunct is still the same
        // conjunct -- which is the whole reason two candidates differing in
        // one slot are comparable at all.
        // A tag before the unit separator is carried through verbatim: it
        // says which section the formula sits in, and the same formula in two
        // sections is two conjuncts.
        const std::size_t separator = conjunct.find('\x1f');
        const std::string key =
            separator == std::string::npos
                ? formula_key::canonical(conjunct)
                : conjunct.substr(0, separator + 1) +
                      formula_key::canonical(conjunct.substr(separator + 1));
        const auto found = m_conjunct_ids.find(key);
        const int conjunct_id = found != m_conjunct_ids.end()
                                    ? found->second
                                    : static_cast<int>(m_conjunct_ids.size());
        if (found == m_conjunct_ids.end()) {
            m_conjunct_ids.emplace(key, conjunct_id);
            m_conjunct_requirements.emplace_back();
        }
        // Any requirement lowering to this conjunct stands for all of them,
        // the rules being sound for the formula whichever one is asked.
        auto& stored = m_conjunct_requirements[conjunct_id];
        if (!stored.has_value() && requirement != nullptr) {
            stored = *requirement;
        }
        ids.push_back(conjunct_id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::optional<bool> RealizabilityChecker::subsumed_verdict(
    const std::string& scope, const std::vector<int>& assumptions,
    const std::vector<int>& guarantees, std::uint64_t assumption_signature,
    std::uint64_t guarantee_signature) const {
    const auto bucket = m_decided.find(scope);
    if (bucket == m_decided.end()) {
        return std::nullopt;
    }
    for (const Decided& entry : bucket->second) {
        if (entry.m_realizable) {
            // realizable(A, G) => realizable(A', G') for A' superset A and
            // G' subset G: the same strategy still wins when the system owes
            // less and the environment promises more.
            if ((entry.m_assumption_signature & ~assumption_signature) == 0 &&
                (guarantee_signature & ~entry.m_guarantee_signature) == 0 &&
                includes_sorted(assumptions, entry.m_assumptions) &&
                includes_sorted(entry.m_guarantees, guarantees)) {
                return true;
            }
            continue;
        }
        // unrealizable(A, G) => unrealizable(A', G') in the other direction.
        if ((assumption_signature & ~entry.m_assumption_signature) == 0 &&
            (entry.m_guarantee_signature & ~guarantee_signature) == 0 &&
            includes_sorted(entry.m_assumptions, assumptions) &&
            includes_sorted(guarantees, entry.m_guarantees)) {
            return false;
        }
    }
    return std::nullopt;
}

bool RealizabilityChecker::rule_implies(int stronger, int weaker) {
    const std::uint64_t key = (static_cast<std::uint64_t>(stronger) << 32U) |
                              static_cast<std::uint32_t>(weaker);
    const auto found = m_rule_verdicts.find(key);
    if (found != m_rule_verdicts.end()) {
        return found->second;
    }
    const std::optional<Requirement>& strong =
        m_conjunct_requirements[stronger];
    const std::optional<Requirement>& weak = m_conjunct_requirements[weaker];
    const bool holds = strong.has_value() && weak.has_value() &&
                       requirement_implies(*strong, *weak);
    m_rule_verdicts.emplace(key, holds);
    return holds;
}

// The monotone argument of subsumed_verdict with the inclusions relaxed: a
// conjunct one side lacks may be implied by one the other side replaced it
// with. A query differs from an earlier one mostly by a mutated requirement or
// two, which inclusion never matches and the rules often order.
std::optional<bool> RealizabilityChecker::rule_subsumed_verdict(
    const std::string& scope, const std::vector<int>& assumptions,
    const std::vector<int>& guarantees) {
    const auto bucket = m_decided.find(scope);
    if (bucket == m_decided.end()) {
        return std::nullopt;
    }
    const std::uint64_t assumption_signature = signature_of(assumptions);
    const std::uint64_t guarantee_signature = signature_of(guarantees);
    // Every id in @p needed implied by one in @p offered.
    const auto covered = [this](const std::vector<int>& needed,
                                const std::vector<int>& offered) {
        return std::all_of(needed.begin(), needed.end(), [&](int weaker) {
            return std::any_of(
                offered.begin(), offered.end(),
                [&](int stronger) { return rule_implies(stronger, weaker); });
        });
    };
    std::vector<int> entry_assumptions;
    std::vector<int> query_assumptions;
    std::vector<int> entry_guarantees;
    std::vector<int> query_guarantees;
    for (const Decided& entry : bucket->second) {
        if (!bounded_difference(entry.m_assumptions,
                                entry.m_assumption_signature, assumptions,
                                assumption_signature, entry_assumptions) ||
            !bounded_difference(
                assumptions, assumption_signature, entry.m_assumptions,
                entry.m_assumption_signature, query_assumptions) ||
            !bounded_difference(entry.m_guarantees, entry.m_guarantee_signature,
                                guarantees, guarantee_signature,
                                entry_guarantees) ||
            !bounded_difference(guarantees, guarantee_signature,
                                entry.m_guarantees, entry.m_guarantee_signature,
                                query_guarantees)) {
            continue;
        }
        if (entry.m_realizable) {
            // The query's premise implies the entry's, and the entry's
            // guarantees imply the query's.
            if (covered(entry_assumptions, query_assumptions) &&
                covered(query_guarantees, entry_guarantees)) {
                return true;
            }
            continue;
        }
        if (covered(query_assumptions, entry_assumptions) &&
            covered(entry_guarantees, query_guarantees)) {
            return false;
        }
    }
    return std::nullopt;
}

std::optional<bool> RealizabilityChecker::check_realizability(
    const Specification& specification) {
    const std::string conj_ltl = specification.to_ltl();
    SpecificationSides sides;
    for (const Requirement& requirement : specification.m_assumptions) {
        if (!requirement.m_removed) {
            sides.m_assumptions.push_back(requirement.m_ltl);
            sides.m_assumption_requirements.push_back(&requirement);
        }
    }
    for (const Requirement& requirement : specification.m_guarantees) {
        if (!requirement.m_removed) {
            sides.m_guarantees.push_back(requirement.m_ltl);
            sides.m_guarantee_requirements.push_back(&requirement);
        }
    }
    return check_realizability_ltl(conj_ltl, environment_signals(specification),
                                   specification.m_out_atoms, sides);
}

std::optional<bool> RealizabilityChecker::check_realizability_ltl(
    const std::string& ltl_formula, const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs) {
    // No sides: a caller holding only a formula gets the memo alone.
    return check_realizability_ltl(ltl_formula, inputs, outputs,
                                   SpecificationSides{});
}

std::optional<bool> RealizabilityChecker::check_realizability_ltl(
    const std::string& ltl_formula, const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs, const SpecificationSides& sides) {
    // No `ltlfilt --simplify` pre-pass, matching run_ltl2tgba_for_counting:
    // ltlsynt simplifies internally, and the specification formula is a
    // conjunction of the guarantees, which reproduces the deeply nested-X
    // shape that hangs ltlfilt --simplify for multi-guarantee deep-horizon
    // specs (e.g. two WithinTicks(20) guarantees with different responses --
    // reachable via mutate_timing, and re-checked here for every survivor).
    // ltlsynt decides the raw formula in milliseconds, so pass it straight
    // through. Unlike the black path, nothing here depends on the "0"/"1"
    // fold the pass would enable.
    const std::string& conj_ltl = ltl_formula;
    // The canonical key renames the atoms within each side of the partition,
    // which realizability is invariant under, and folds the declared lists
    // into it (see formula_key::realizability). It replaces the raw formula
    // and the two comma-joined lists this used to concatenate.
    const std::string& cache_key =
        formula_key::realizability(conj_ltl, inputs, outputs);
    const bool have_sides =
        !sides.m_assumptions.empty() || !sides.m_guarantees.empty();
    // Everything that has to match for a comparison to mean anything: the
    // caller's own discriminator and the alphabet the synthesiser plays over.
    const std::string scope = sides.m_scope + "\x1f" + join_comma(inputs) +
                              "\x1f" + join_comma(outputs);
    std::vector<int> assumption_ids;
    std::vector<int> guarantee_ids;
    {
        std::scoped_lock lock(m_cache_mutex);
        const auto found = m_cache.find(cache_key);
        if (found != m_cache.end()) {
            n_cache_hits++;
            return found->second;
        }
        if (have_sides) {
            assumption_ids = intern_side(sides.m_assumptions,
                                         sides.m_assumption_requirements);
            guarantee_ids =
                intern_side(sides.m_guarantees, sides.m_guarantee_requirements);
            if (const auto verdict =
                    subsumed_verdict(scope, assumption_ids, guarantee_ids,
                                     signature_of(assumption_ids),
                                     signature_of(guarantee_ids))) {
                n_subsumed++;
                // Memoised like any other answer, so the scan is paid once per
                // distinct query rather than once per occurrence.
                m_cache.emplace(cache_key, verdict);
                return verdict;
            }
            if (const auto verdict = rule_subsumed_verdict(
                    scope, assumption_ids, guarantee_ids)) {
                n_rule_subsumed++;
                m_cache.emplace(cache_key, verdict);
                return verdict;
            }
        }
    }
    // Recorded exactly as an ltlsynt verdict is, so the subsumption passes
    // read a routed answer like any other. Called under m_cache_mutex.
    const auto remember = [&](const std::optional<bool>& verdict) {
        m_cache.emplace(cache_key, verdict);
        // Only a decided verdict joins the table. An undecided one is memoised
        // like any other, but it carries no direction, so nothing follows
        // from it about any other query.
        if (have_sides && verdict.has_value()) {
            m_decided[scope].push_back(Decided{
                assumption_ids, guarantee_ids, signature_of(assumption_ids),
                signature_of(guarantee_ids), *verdict});
        }
    };
    // With no inputs the environment makes no choice, so the query is
    // satisfiability: a satisfying word is a strategy the system plays blind,
    // and Mealy and Moore coincide. On lift-plus-cruise-mini, 72 queries took
    // a median 0.538s in ltlsynt and 0.028s in black, with every verdict
    // agreeing. black goes first because SPOT's satisfiability stage is the
    // slow side on whole specifications. An undecided or failed answer falls
    // through to ltlsynt, which may still decide it within its own budget.
    if (m_route_input_free &&
        plays_without_environment(conj_ltl, inputs, outputs)) {
        std::optional<bool> satisfiable;
        try {
            satisfiable =
                global_sat_checker().check_satisfiability_by_black(conj_ltl);
        } catch (const std::exception&) {
            satisfiable.reset();
        }
        if (satisfiable.has_value()) {
            std::scoped_lock lock(m_cache_mutex);
            n_satisfiability_routed++;
            remember(satisfiable);
            return satisfiable;
        }
    }
    {
        std::scoped_lock lock(m_cache_mutex);
        n_cache_misses++;
    }
    const std::string ltlsynt = ltlsynt_path();
    assert(access(ltlsynt.c_str(), F_OK) == 0);
    std::vector<std::string> command = {ltlsynt, "--realizability", "-F", "-"};
    if (!inputs.empty()) {
        command.push_back("--ins=" + join_comma(inputs));
    } else if (!outputs.empty()) {
        command.push_back("--outs=" + join_comma(outputs));
    }
    const auto timeout = std::chrono::milliseconds(g_ltlsynt_timeout_ms.load());
    const ProcessResult result = execute_and_capture_with_input(
        command, spot_formula_line(conj_ltl), timeout);
    // A timed-out query is undecided, not unrealizable. Which of the two is
    // the safe reading depends on the question being asked -- admitting a
    // repair wants "unrealizable", the well-separation check wants
    // "realizable" -- so the direction is the caller's to pick, and this
    // reports nullopt rather than picking one for everybody.
    const std::optional<bool> realizable =
        result.m_timed_out ? std::nullopt : parse_realizability_output(result);
    // Diagnostic hook (off unless PEREDUR_LTLSYNT_LOG names a file): append one
    // "elapsed_s timed_out n_atoms" line per ltlsynt exec, for studying the
    // call-duration distribution and tuning ltlsynt_timeout. Zero cost when the
    // env var is unset.
    if (const char* log_path = std::getenv("PEREDUR_LTLSYNT_LOG")) {
        static std::mutex log_mutex;
        const std::scoped_lock log_lock(log_mutex);
        std::ofstream log_file(log_path, std::ios::app);
        log_file << result.m_wall_s << ' ' << (result.m_timed_out ? 1 : 0)
                 << ' ' << (inputs.size() + outputs.size()) << '\n';
    }
    std::scoped_lock lock(m_cache_mutex);
    record_exec<RealizabilityChecker>(result);
    if (!result.m_timed_out && !realizable.has_value()) {
        n_capability_errors++;
    }
    // Undecided is memoised like any other outcome, for the reason the ltlfilt
    // cache gives: a formula that blew the budget once will blow it every
    // time, and re-paying that wait per occurrence is the stall the timeout
    // exists to avoid. What #74 had to remove was caching a *verdict* nobody
    // decided; nullopt is not one, so every caller still picks its own
    // direction on every hit.
    remember(realizable);
    return realizable;
}

std::string ltlsynt_budget_screen(
    const std::function<std::optional<bool>()>& query,
    std::chrono::milliseconds budget) {
    if (budget.count() <= 0) {
        return {};
    }
    const auto start = std::chrono::steady_clock::now();
    // A raising query is an undecided one, exactly as a timeout is, and is
    // caught for the same reason the input screen catches: this runs before the
    // driver's own handler, so letting it out ends the run with neither a
    // `fatal:` line nor a manifest. The screen is advisory either way -- it
    // times a query the run was about to make anyway -- so the cost of a raise
    // is the timing, not the run.
    std::optional<bool> decided;
    std::string error;
    try {
        decided = query();
    } catch (const std::exception& exc) {
        error = exc.what();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    BudgetScreen::observed_ms = elapsed.count();
    BudgetScreen::decided = decided.has_value();
    if (!error.empty()) {
        return "warning: the input specification's own realizability query "
               "failed: " +
               error +
               "\n         The status objective cannot be decided for any "
               "candidate this tool rejects the same way.\n";
    }
    if (decided.has_value()) {
        return {};
    }
    // Elapsed is reported rather than the true cost, which is unobservable:
    // the child was killed at the budget, so all this says is "at least".
    return "warning: the input specification's own realizability query did "
           "not finish within runtime.ltlsynt_timeout_ms = " +
           std::to_string(budget.count()) +
           " ms.\n         Every realizability query this run makes is "
           "likely to be abandoned the same way, which leaves the status "
           "objective\n         constant, costs that budget per distinct "
           "candidate, and makes the final gate reject even a correct "
           "repair.\n         Raise runtime.ltlsynt_timeout_ms (0 disables "
           "the budget) if this run is meant to decide realizability.\n";
}
