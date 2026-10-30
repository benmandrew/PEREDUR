#include "runner/spot.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

// The ltlsynt invocation for a query over @p inputs and @p outputs. The
// controller is asked for only where it may be stored. ltlsynt prints the
// verdict line first either way, and on the measured queries the strategy
// cost nothing beyond the verdict.
std::vector<std::string> ltlsynt_command(
    const std::string& ltlsynt, const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs, bool wants_controller) {
    std::vector<std::string> command = {
        ltlsynt, wants_controller ? "--hoaf" : "--realizability", "-F", "-"};
    if (!inputs.empty()) {
        command.push_back("--ins=" + join_comma(inputs));
    } else if (!outputs.empty()) {
        command.push_back("--outs=" + join_comma(outputs));
    }
    return command;
}

// The verdict of an ltlsynt run that finished, with the controller it printed
// after a REALIZABLE line into @p controller. With a controller in the output
// only the first line is the verdict: the HOA after it names atoms, and an
// output may be spelt in capitals.
std::optional<bool> read_ltlsynt_output(const ProcessResult& result,
                                        std::string& controller) {
    const std::string& output = result.m_output;
    if (output.rfind("REALIZABLE\nHOA:", 0) == 0) {
        controller = output.substr(output.find('\n') + 1);
        return true;
    }
    const std::size_t strategy = output.find("\nHOA:");
    if (strategy == std::string::npos) {
        return parse_realizability_output(result);
    }
    ProcessResult verdict_only = result;
    verdict_only.m_output.resize(strategy);
    return parse_realizability_output(verdict_only);
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

std::string autfilt_path() { return spot_bin_dir() + "/autfilt"; }

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

// With no inputs the environment makes no choice, so the query is
// satisfiability: a satisfying word is a strategy the system plays blind, and
// Mealy and Moore coincide. On lift-plus-cruise-mini, 72 queries took a median
// 0.538s in ltlsynt and 0.028s in black, with every verdict agreeing. black
// goes first because SPOT's satisfiability stage is the slow side on whole
// specifications. Returns nullopt for a query with an environment, and for an
// undecided or failed answer, which falls through to ltlsynt: it may still
// decide the query within its own budget.
std::optional<bool> decide_without_environment(
    const std::string& ltl, const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs) {
    if (!plays_without_environment(ltl, inputs, outputs)) {
        return std::nullopt;
    }
    try {
        return global_sat_checker().check_satisfiability_by_black(ltl);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

void RealizabilityChecker::remember_verdict(
    const std::string& cache_key, const std::string& scope, bool have_sides,
    const std::vector<int>& assumption_ids,
    const std::vector<int>& guarantee_ids, const std::optional<bool>& verdict) {
    m_cache.emplace(cache_key, verdict);
    // Only a decided verdict joins the table. An undecided one is memoised
    // like any other, but it carries no direction, so nothing follows from it
    // about any other query.
    if (have_sides && verdict.has_value()) {
        m_decided[scope].push_back(
            Decided{assumption_ids, guarantee_ids, signature_of(assumption_ids),
                    signature_of(guarantee_ids), *verdict});
    }
}

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
            m_conjunct_formulas.push_back(key);
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

namespace {

// Stored controllers kept per scope, and the candidates tried per query.
constexpr std::size_t k_controllers_per_scope = 64;
constexpr std::size_t k_reuse_candidates = 3;
// Products cached per controller, one per assumption set it was checked under.
constexpr std::size_t k_products_per_controller = 8;
// Automata cached for assumption sets and guarantee negations; past this the
// map is cleared rather than kept in order.
constexpr std::size_t k_automata_cache_cap = 4096;
// Above this many inputs the completeness walk over input valuations is not
// run, and the controller is not stored.
constexpr std::size_t k_max_controller_inputs = 16;
// Per-exec budget for the checks, cut to the ltlsynt budget when that is
// smaller: a check slower than the exec it replaces has lost already.
constexpr std::chrono::milliseconds k_reuse_exec_budget{2000};

// A directory of this process's own for the HOA files autfilt reads with
// --product, which takes a file and nothing else. Created on first use and
// removed with everything in it at exit. A run killed by a signal leaves it.
class ScratchDirectory {
   public:
    ScratchDirectory() {
        const char* tmpdir = std::getenv("TMPDIR");
        std::string base =
            tmpdir != nullptr && *tmpdir != '\0' ? tmpdir : "/tmp";
        while (base.size() > 1 && base.back() == '/') {
            base.pop_back();
        }
        std::string pattern = base + "/peredur-controllers-XXXXXX";
        if (mkdtemp(pattern.data()) != nullptr) {
            m_path = pattern;
        }
    }
    // remove_all reports through the error code; only an allocation can
    // throw, at exit, where there is nothing left to do about it.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    ~ScratchDirectory() {
        if (!m_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;
    ScratchDirectory(ScratchDirectory&&) = delete;
    ScratchDirectory& operator=(ScratchDirectory&&) = delete;

    // Empty when the directory could not be made, which turns the store off.
    [[nodiscard]] const std::string& path() const { return m_path; }

   private:
    std::string m_path;
};

const std::string& scratch_directory() {
    static const ScratchDirectory k_directory;
    return k_directory.path();
}

// A new file name in the scratch directory, or empty when there is none.
std::string scratch_file(const char* stem) {
    static std::atomic<std::uint64_t> serial{0};
    const std::string& directory = scratch_directory();
    if (directory.empty()) {
        return {};
    }
    return directory + "/" + stem + std::to_string(serial++) + ".hoa";
}

bool write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return static_cast<bool>(out);
}

bool controller_reuse_available() {
    static const bool k_available = !scratch_directory().empty() &&
                                    access(autfilt_path().c_str(), X_OK) == 0;
    return k_available;
}

std::chrono::milliseconds reuse_exec_budget() {
    const std::int64_t ltlsynt_ms = g_ltlsynt_timeout_ms.load();
    if (ltlsynt_ms > 0 && ltlsynt_ms < k_reuse_exec_budget.count()) {
        return std::chrono::milliseconds(ltlsynt_ms);
    }
    return k_reuse_exec_budget;
}

// The ids in both sorted vectors.
std::size_t shared_count(const std::vector<int>& lhs,
                         const std::vector<int>& rhs) {
    std::size_t count = 0;
    auto left = lhs.begin();
    auto right = rhs.begin();
    while (left != lhs.end() && right != rhs.end()) {
        if (*left < *right) {
            ++left;
        } else if (*right < *left) {
            ++right;
        } else {
            ++count;
            ++left;
            ++right;
        }
    }
    return count;
}

std::string ids_key(const std::vector<int>& ids) {
    std::string key;
    for (const int conjunct_id : ids) {
        key += std::to_string(conjunct_id);
        key += ',';
    }
    return key;
}

// The value after `name` on a header line of @p hoa, before @p body.
std::optional<std::string> hoa_header_value(const std::string& hoa,
                                            std::size_t body,
                                            const std::string& name) {
    const std::size_t line = hoa.find("\n" + name);
    if (line == std::string::npos || line > body) {
        return std::nullopt;
    }
    const std::size_t start = line + 1 + name.size();
    return hoa.substr(start, hoa.find('\n', start) - start);
}

// Each AP's bit in an input valuation, or -1 for an output, from the AP and
// controllable-AP headers. Empty when the header is not one this reads.
std::vector<int> mealy_input_bits(const std::string& hoa, std::size_t body) {
    const std::optional<std::string> aps = hoa_header_value(hoa, body, "AP:");
    if (!aps.has_value()) {
        return {};
    }
    const std::size_t n_aps = std::stoul(*aps);
    std::vector<bool> controllable(n_aps, false);
    const std::optional<std::string> outputs =
        hoa_header_value(hoa, body, "controllable-AP:");
    const std::string listed = outputs.value_or("");
    std::size_t cursor = listed.find_first_not_of(' ');
    while (cursor != std::string::npos) {
        std::size_t used = 0;
        const std::size_t index = std::stoul(listed.substr(cursor), &used);
        if (index >= n_aps) {
            return {};
        }
        controllable[index] = true;
        cursor = listed.find_first_not_of(' ', cursor + used);
    }
    std::vector<int> input_bit(n_aps, -1);
    int n_inputs = 0;
    for (std::size_t index = 0; index < n_aps; ++index) {
        if (!controllable[index]) {
            input_bit[index] = n_inputs++;
        }
    }
    return input_bit;
}

// One cube's input literals: the bits it fixes and their values.
struct InputCube {
    std::uint32_t m_mask = 0;
    std::uint32_t m_value = 0;
};

// Adds one literal to @p cube when its AP is an input, or false when the cube
// already fixes that input the other way.
bool add_literal(InputCube& cube, int input_bit, bool negated) {
    if (input_bit < 0) {
        return true;
    }
    const std::uint32_t bit = std::uint32_t{1}
                              << static_cast<unsigned>(input_bit);
    const std::uint32_t value = negated ? 0U : bit;
    const bool consistent =
        (cube.m_mask & bit) == 0 || (cube.m_value & bit) == value;
    cube.m_mask |= bit;
    cube.m_value |= value;
    return consistent;
}

// The input projection of each cube in the label of an edge line, appended to
// @p cubes. A cube contradicting itself on an input enables nothing and is
// dropped. False on any label this does not read.
bool read_label_cubes(const std::string& line,
                      const std::vector<int>& input_bit,
                      std::vector<InputCube>& cubes) {
    const std::size_t close = line.find(']');
    if (line.empty() || line[0] != '[' || close == std::string::npos) {
        return false;
    }
    InputCube cube;
    bool contradictory = false;
    bool negated = false;
    for (std::size_t cursor = 1; cursor <= close; ++cursor) {
        const char symbol = line[cursor];
        if (symbol == '|' || symbol == ']') {
            if (!contradictory) {
                cubes.push_back(cube);
            }
            cube = InputCube{};
            contradictory = false;
        } else if (symbol == '!') {
            negated = true;
            continue;
        } else if (symbol == 'f') {
            contradictory = true;
        } else if (symbol >= '0' && symbol <= '9') {
            std::size_t used = 0;
            const std::size_t index = std::stoul(line.substr(cursor), &used);
            cursor += used - 1;
            if (index >= input_bit.size()) {
                return false;
            }
            contradictory =
                !add_literal(cube, input_bit[index], negated) || contradictory;
        } else if (symbol != ' ' && symbol != '&' && symbol != 't') {
            return false;
        }
        negated = false;
    }
    return true;
}

// Whether the cubes leaving one state enable every valuation of the inputs.
bool enables_every_input(const std::vector<InputCube>& cubes,
                         std::size_t n_inputs) {
    const std::uint32_t n_valuations = std::uint32_t{1} << n_inputs;
    for (std::uint32_t valuation = 0; valuation < n_valuations; ++valuation) {
        const bool enabled =
            std::any_of(cubes.begin(), cubes.end(), [&](const InputCube& cube) {
                return (valuation & cube.m_mask) == cube.m_value;
            });
        if (!enabled) {
            return false;
        }
    }
    return true;
}

// Whether @p hoa is a Mealy machine the store can believe: one accepting
// condition over every run, and a transition for every input valuation from
// every state. The second is what the soundness of the check rests on. A
// machine missing an input at some state drops every word through it from its
// language, and a product emptiness check would then pass a guarantee those
// words violate. ltlsynt emitted only complete machines on all 2,097
// realizable queries of the recorded lift-plus-cruise-full stream, so this is
// a guard rather than a filter.
//
// Labels are read as SPOT prints them, a disjunction of conjunctions of
// literals over AP indices. Anything else is refused, not interpreted.
bool is_input_complete_mealy(const std::string& hoa) {
    const std::size_t body = hoa.find("--BODY--");
    if (body == std::string::npos ||
        hoa_header_value(hoa, body, "Acceptance:") != " 0 t") {
        return false;
    }
    const std::vector<int> input_bit = mealy_input_bits(hoa, body);
    const auto n_inputs = static_cast<std::size_t>(std::count_if(
        input_bit.begin(), input_bit.end(), [](int bit) { return bit >= 0; }));
    if (input_bit.empty() || n_inputs > k_max_controller_inputs) {
        return false;
    }
    std::vector<std::vector<InputCube>> states;
    std::size_t line_start = hoa.find('\n', body);
    while (line_start != std::string::npos && line_start + 1 < hoa.size()) {
        const std::size_t line_end = hoa.find('\n', line_start + 1);
        const std::string line =
            hoa.substr(line_start + 1, line_end == std::string::npos
                                           ? std::string::npos
                                           : line_end - line_start - 1);
        line_start = line_end;
        if (line.rfind("--END--", 0) == 0) {
            break;
        }
        if (line.rfind("State:", 0) == 0) {
            states.emplace_back();
        } else if (states.empty() ||
                   !read_label_cubes(line, input_bit, states.back())) {
            return false;
        }
    }
    return !states.empty() &&
           std::all_of(states.begin(), states.end(),
                       [n_inputs](const std::vector<InputCube>& cubes) {
                           return enables_every_input(cubes, n_inputs);
                       });
}
}  // namespace

// One stored Mealy machine, with the sides of the query it was synthesised
// for. The file goes with the last reference, so a check still reading it
// outlives an eviction.
struct RealizabilityChecker::StoredController {
    std::vector<int> m_assumptions;
    std::vector<int> m_guarantees;
    std::string m_path;
    /// Products by assumption-set key, under m_reuse_mutex.
    std::unordered_map<std::string, std::shared_ptr<ControllerProduct>>
        m_products;

    StoredController() = default;
    StoredController(const StoredController&) = delete;
    StoredController& operator=(const StoredController&) = delete;
    StoredController(StoredController&&) = delete;
    StoredController& operator=(StoredController&&) = delete;
    ~StoredController() { std::remove(m_path.c_str()); }
};

// A controller's product with the automaton of one assumption set, and the
// guarantee checks already run against it.
struct RealizabilityChecker::ControllerProduct {
    std::string m_path;
    /// False when m_path is the controller's own file, as it is under no
    /// assumptions.
    bool m_owns_file = true;
    /// Guarantee id to whether the product with its negation was empty, under
    /// m_reuse_mutex.
    std::unordered_map<int, bool> m_verdicts;

    ControllerProduct() = default;
    ControllerProduct(const ControllerProduct&) = delete;
    ControllerProduct& operator=(const ControllerProduct&) = delete;
    ControllerProduct(ControllerProduct&&) = delete;
    ControllerProduct& operator=(ControllerProduct&&) = delete;
    ~ControllerProduct() {
        if (m_owns_file) {
            std::remove(m_path.c_str());
        }
    }
};

std::optional<std::string> RealizabilityChecker::assumption_automaton(
    const ReuseQuery& query) {
    const std::string key = ids_key(query.m_assumptions);
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = m_assumption_automata.find(key);
        if (found != m_assumption_automata.end()) {
            return found->second;
        }
    }
    std::string conjunction;
    for (const std::string& formula : query.m_assumption_formulas) {
        conjunction += conjunction.empty() ? "(" : " & (";
        conjunction += formula;
        conjunction += ")";
    }
    const ProcessResult result = execute_and_capture_with_input(
        {ltl2tgba_path(), "-H", "-F", "-"}, spot_formula_line(conjunction),
        reuse_exec_budget());
    std::optional<std::string> automaton;
    if (!result.m_timed_out && is_tautology_print_error(result)) {
        automaton = k_universal_hoa;
    } else if (!result.m_timed_out && result.m_exit_code == 0 &&
               result.m_output.rfind("HOA:", 0) == 0) {
        automaton = result.m_output;
    }
    std::scoped_lock lock(m_reuse_mutex);
    record_exec<ControllerReuseStats>(result);
    ControllerReuseStats::n_execs++;
    // A failure, a timeout among them, is memoised as nullopt: ltl2tgba is
    // deterministic, and every query it stops falls through to ltlsynt.
    if (m_assumption_automata.size() >= k_automata_cache_cap) {
        m_assumption_automata.clear();
    }
    m_assumption_automata.emplace(key, automaton);
    return automaton;
}

std::optional<std::string> RealizabilityChecker::negation_automaton(
    const std::string& formula) {
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = m_negation_automata.find(formula);
        if (found != m_negation_automata.end()) {
            return found->second;
        }
    }
    const ProcessResult result = execute_and_capture_with_input(
        {ltl2tgba_path(), "-H", "-F", "-"},
        spot_formula_line("!(" + formula + ")"), reuse_exec_budget());
    // A tautologous negation is a guarantee no controller meets, and is left
    // to ltlsynt like any other failure.
    std::optional<std::string> automaton;
    if (!result.m_timed_out && result.m_exit_code == 0 &&
        result.m_output.rfind("HOA:", 0) == 0) {
        automaton = result.m_output;
    }
    std::scoped_lock lock(m_reuse_mutex);
    record_exec<ControllerReuseStats>(result);
    ControllerReuseStats::n_execs++;
    if (m_negation_automata.size() >= k_automata_cache_cap) {
        m_negation_automata.clear();
    }
    m_negation_automata.emplace(formula, automaton);
    return automaton;
}

std::shared_ptr<RealizabilityChecker::ControllerProduct>
RealizabilityChecker::product_for(
    const std::shared_ptr<StoredController>& controller,
    const ReuseQuery& query) {
    const std::string key = ids_key(query.m_assumptions);
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = controller->m_products.find(key);
        if (found != controller->m_products.end()) {
            return found->second;
        }
    }
    auto product = std::make_shared<ControllerProduct>();
    if (query.m_assumptions.empty()) {
        product->m_path = controller->m_path;
        product->m_owns_file = false;
    } else {
        const std::optional<std::string> assumptions =
            assumption_automaton(query);
        if (!assumptions.has_value()) {
            return nullptr;
        }
        const ProcessResult result = execute_and_capture_with_input(
            {autfilt_path(), "--product=" + controller->m_path, "-"},
            *assumptions, reuse_exec_budget());
        {
            std::scoped_lock lock(m_reuse_mutex);
            record_exec<ControllerReuseStats>(result);
            ControllerReuseStats::n_execs++;
        }
        if (result.m_timed_out || result.m_exit_code != 0 ||
            result.m_output.rfind("HOA:", 0) != 0) {
            return nullptr;
        }
        product->m_path = scratch_file("product-");
        if (product->m_path.empty() ||
            !write_file(product->m_path, result.m_output)) {
            return nullptr;
        }
    }
    std::scoped_lock lock(m_reuse_mutex);
    if (controller->m_products.size() >= k_products_per_controller) {
        controller->m_products.clear();
    }
    // A concurrent check may have built the same product first; either is
    // the same automaton, and the one kept carries any verdicts already in.
    return controller->m_products.emplace(key, product).first->second;
}

std::vector<std::shared_ptr<RealizabilityChecker::StoredController>>
RealizabilityChecker::reuse_candidates(const ReuseQuery& query) {
    std::scoped_lock lock(m_reuse_mutex);
    const auto bucket = m_controllers.find(query.m_scope);
    if (bucket == m_controllers.end()) {
        return {};
    }
    // The k entries sharing the most conjuncts, newest first among equals.
    const auto& stored = bucket->second;
    std::vector<std::pair<std::size_t, std::size_t>> ranked;
    ranked.reserve(stored.size());
    for (std::size_t age = 0; age < stored.size(); ++age) {
        const StoredController& entry = *stored[stored.size() - 1 - age];
        ranked.emplace_back(
            shared_count(entry.m_assumptions, query.m_assumptions) +
                shared_count(entry.m_guarantees, query.m_guarantees),
            age);
    }
    const std::size_t keep = std::min(k_reuse_candidates, ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(keep),
        ranked.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first != rhs.first ? lhs.first > rhs.first
                                          : lhs.second < rhs.second;
        });
    std::vector<std::shared_ptr<StoredController>> candidates;
    candidates.reserve(keep);
    for (std::size_t rank = 0; rank < keep; ++rank) {
        candidates.push_back(stored[stored.size() - 1 - ranked[rank].second]);
    }
    if (!candidates.empty()) {
        ControllerReuseStats::n_attempts++;
    }
    return candidates;
}

bool RealizabilityChecker::product_meets(ControllerProduct& product,
                                         int guarantee,
                                         const std::string& formula) {
    {
        std::scoped_lock lock(m_reuse_mutex);
        const auto found = product.m_verdicts.find(guarantee);
        if (found != product.m_verdicts.end()) {
            ControllerReuseStats::n_verdict_hits++;
            return found->second;
        }
    }
    const std::optional<std::string> negation = negation_automaton(formula);
    if (!negation.has_value()) {
        return false;
    }
    // --stats prints one line per automaton autfilt outputs, which is the
    // product; %w is a word it accepts, and nothing when it is empty.
    // --is-empty would not do: it filters the input, before the product is
    // taken. The brackets make an empty word a positive answer, so that no
    // output at all is never read as a pass.
    const ProcessResult result = execute_and_capture_with_input(
        {autfilt_path(), "--product=" + product.m_path, "--stats=<%w>", "-"},
        *negation, reuse_exec_budget());
    const bool decided = !result.m_timed_out && result.m_exit_code == 0 &&
                         result.m_output.rfind('<', 0) == 0;
    const bool empty = decided && result.m_output == "<>\n";
    std::scoped_lock lock(m_reuse_mutex);
    record_exec<ControllerReuseStats>(result);
    ControllerReuseStats::n_execs++;
    ControllerReuseStats::n_guarantees_checked++;
    if (decided) {
        product.m_verdicts.emplace(guarantee, empty);
    }
    return empty;
}

bool RealizabilityChecker::controller_realizes(const ReuseQuery& query) {
    for (const auto& controller : reuse_candidates(query)) {
        // The controller realizes (&A_e) -> (&G_e), so on every word it
        // produces each g in G_e holds once A_e does. Where the query assumes
        // at least A_e those guarantees hold under its assumptions too, and
        // only the ones the entry lacked need checking; otherwise all do. The
        // ones it lacked go first, being the likelier to fail.
        std::vector<std::size_t> order;
        std::vector<std::size_t> shared;
        for (std::size_t position = 0; position < query.m_guarantees.size();
             ++position) {
            const bool known = std::binary_search(
                controller->m_guarantees.begin(),
                controller->m_guarantees.end(), query.m_guarantees[position]);
            (known ? shared : order).push_back(position);
        }
        if (!includes_sorted(query.m_assumptions, controller->m_assumptions)) {
            order.insert(order.end(), shared.begin(), shared.end());
        }
        const std::shared_ptr<ControllerProduct> product =
            product_for(controller, query);
        if (product != nullptr &&
            std::all_of(order.begin(), order.end(), [&](std::size_t position) {
                return product_meets(*product, query.m_guarantees[position],
                                     query.m_guarantee_formulas[position]);
            })) {
            std::scoped_lock lock(m_reuse_mutex);
            ControllerReuseStats::n_hits++;
            return true;
        }
    }
    return false;
}

void RealizabilityChecker::store_controller(const ReuseQuery& query,
                                            const std::string& hoa) {
    bool complete = false;
    try {
        complete = is_input_complete_mealy(hoa);
    } catch (const std::exception&) {
        // An index past what stoul reads is a machine not stored, as any
        // other shape the reader does not know is.
        return;
    }
    if (!complete) {
        return;
    }
    auto controller = std::make_shared<StoredController>();
    controller->m_assumptions = query.m_assumptions;
    controller->m_guarantees = query.m_guarantees;
    controller->m_path = scratch_file("controller-");
    if (controller->m_path.empty() || !write_file(controller->m_path, hoa)) {
        return;
    }
    std::scoped_lock lock(m_reuse_mutex);
    auto& bucket = m_controllers[query.m_scope];
    bucket.push_back(std::move(controller));
    if (bucket.size() > k_controllers_per_scope) {
        bucket.pop_front();
    }
    ControllerReuseStats::n_stored++;
}

bool RealizabilityChecker::settled_by_controller(const ReuseQuery& query,
                                                 const std::string& cache_key) {
    const bool realized = controller_realizes(query);
    std::scoped_lock lock(m_cache_mutex);
    if (!realized) {
        n_cache_misses++;
        return false;
    }
    // Recorded as a decided verdict, since it is one. Its controller is not
    // stored: the one that answered it already is.
    m_cache.emplace(cache_key, true);
    m_decided[query.m_scope].push_back(
        Decided{query.m_assumptions, query.m_guarantees,
                signature_of(query.m_assumptions),
                signature_of(query.m_guarantees), true});
    return true;
}

RealizabilityChecker::ReuseQuery RealizabilityChecker::reuse_query(
    const std::string& scope, const std::vector<int>& assumptions,
    const std::vector<int>& guarantees) const {
    ReuseQuery query{scope, assumptions, guarantees, {}, {}};
    for (const int conjunct_id : assumptions) {
        query.m_assumption_formulas.push_back(m_conjunct_formulas[conjunct_id]);
    }
    for (const int conjunct_id : guarantees) {
        query.m_guarantee_formulas.push_back(m_conjunct_formulas[conjunct_id]);
    }
    return query;
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
    // Only the FRETISH lowering is `(&A) -> (&G)` conjunct by conjunct, which
    // the per-guarantee check needs; TLSF wraps whole sections. It is the one
    // front end that passes a requirement per conjunct.
    const bool fretish_sides =
        have_sides && !sides.m_guarantee_requirements.empty() &&
        sides.m_guarantee_requirements.size() == sides.m_guarantees.size() &&
        sides.m_assumption_requirements.size() == sides.m_assumptions.size();
    std::optional<ReuseQuery> reuse;
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
    if (m_route_input_free) {
        if (const std::optional<bool> satisfiable =
                decide_without_environment(conj_ltl, inputs, outputs)) {
            // Recorded exactly as an ltlsynt verdict is, so the subsumption
            // passes read a routed answer like any other.
            std::scoped_lock lock(m_cache_mutex);
            n_satisfiability_routed++;
            remember_verdict(cache_key, scope, have_sides, assumption_ids,
                             guarantee_ids, satisfiable);
            return satisfiable;
        }
    }
    {
        std::scoped_lock lock(m_cache_mutex);
        if (fretish_sides && guarantee_ids.size() >= m_reuse_min_guarantees &&
            controller_reuse_available()) {
            reuse = reuse_query(scope, assumption_ids, guarantee_ids);
        } else {
            n_cache_misses++;
        }
    }
    if (reuse.has_value() && settled_by_controller(*reuse, cache_key)) {
        return true;
    }
    const std::string ltlsynt = ltlsynt_path();
    assert(access(ltlsynt.c_str(), F_OK) == 0);
    const std::vector<std::string> command =
        ltlsynt_command(ltlsynt, inputs, outputs, reuse.has_value());
    const auto timeout = std::chrono::milliseconds(g_ltlsynt_timeout_ms.load());
    const ProcessResult result = execute_and_capture_with_input(
        command, spot_formula_line(conj_ltl), timeout);
    // A timed-out query is undecided, not unrealizable. Which of the two is
    // the safe reading depends on the question being asked -- admitting a
    // repair wants "unrealizable", the well-separation check wants
    // "realizable" -- so the direction is the caller's to pick, and this
    // reports nullopt rather than picking one for everybody.
    std::string controller;
    const std::optional<bool> realizable =
        result.m_timed_out ? std::nullopt
                           : read_ltlsynt_output(result, controller);
    if (reuse.has_value() && !controller.empty()) {
        store_controller(*reuse, controller);
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
    remember_verdict(cache_key, scope, have_sides, assumption_ids,
                     guarantee_ids, realizable);
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
