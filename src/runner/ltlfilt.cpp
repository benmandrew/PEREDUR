#include "runner/ltlfilt.hpp"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "formula_key.hpp"
#include "profile.hpp"
#include "prop_formula/identifier.hpp"
#include "runner/process.hpp"
#include "runner/spot.hpp"
#include "runner/tool_stats.hpp"

namespace {

// Per-call wall-clock budget for the ltlfilt exec, in milliseconds; 0 disables
// it. Set once at startup from Config::ltlfilt_timeout, read by every worker,
// hence atomic. The initial value mirrors that config default, for the callers
// that never load a config (the tests and the mucs/ltl tools).
//
// Unlike the ltlsynt and ltl2tgba budgets this defaults to a real value rather
// than to "off": --simplify is super-exponential on the deep nested-X
// conjunctions the search builds (35e1467, PR #24), and both callers below
// degrade gracefully when they get no answer, so a bounded wait costs only the
// wait itself.
std::atomic<std::int64_t> g_ltlfilt_timeout_ms{10'000};

// Guards every mutable static in this file: both memo caches below and the
// LtlfiltStats counters. One lock rather than one per cache, because the
// counters are shared across all three entry points -- a per-cache lock leaves
// them written under two different mutexes, which is a race whatever each
// lock's own cache is doing. Held only around map lookups and counter
// arithmetic; every ltlfilt exec runs outside it, so the serialisation costs
// nothing against a subprocess spawn.
std::mutex g_ltlfilt_mutex;

std::chrono::milliseconds ltlfilt_timeout() {
    return std::chrono::milliseconds(g_ltlfilt_timeout_ms.load());
}

// The formula-at-a-time exec behind every simplification, returning the
// simplified formula beside the exec it came from. A killed child exits
// non-zero, so a timeout lands on the same branch as any other ltlfilt failure
// and leaves the formula unsimplified.
std::pair<std::string, ProcessResult> one_shot_simplify(
    const std::string& binary, const std::string& formula) {
    PEREDUR_PROFILE_SCOPE("ltlfilt/one-shot-exec");
    ProcessResult result = execute_and_capture(
        {binary, "--simplify", "-f", formula}, ltlfilt_timeout());
    std::string simplified = formula;
    if (result.m_exit_code == 0 && !result.m_output.empty()) {
        simplified = result.m_output;
        while (!simplified.empty() && simplified.back() == '\n') {
            simplified.pop_back();
        }
    }
    return {std::move(simplified), std::move(result)};
}

}  // namespace

void set_ltlfilt_timeout(std::chrono::milliseconds timeout) {
    g_ltlfilt_timeout_ms.store(timeout.count());
}

std::string ltlfilt_path() { return spot_bin_dir() + "/ltlfilt"; }

std::string simplify_ltl(const std::string& formula) {
    PEREDUR_PROFILE_SCOPE("ltlfilt/simplify_ltl");
    // Keyed on the canonical form rather than on the caller's spelling, so
    // that operand order and association -- which vary freely in what the
    // search builds and change nothing about the answer -- stop buying an
    // exec each. formula_key::canonical is itself memoised on the input
    // string, so a spelling already seen costs a hash rather than a parse.
    //
    // The renamed key is not usable here. The value is a formula, so returning
    // it would mean renaming atoms back inside ltlfilt's own output, and SPOT
    // prints a unary operator hard against its operand -- `F` applied to an
    // atom named `fk1` comes back as `Ffk1` -- which no tokenisation of that
    // output can separate. Keying on the canonical form keeps the caller's own
    // atom names on both sides.
    static std::unordered_map<std::string, std::string> answers;
    const std::string& key = formula_key::canonical(formula);
    {
        PEREDUR_PROFILE_SCOPE("ltlfilt/simplify_ltl:cache-lookup");
        std::scoped_lock lock(g_ltlfilt_mutex);
        const auto found = answers.find(key);
        if (found != answers.end()) {
            LtlfiltStats::n_cache_hits++;
            return found->second;
        }
        LtlfiltStats::n_cache_misses++;
    }
    const std::string binary = ltlfilt_path();
    if (access(binary.c_str(), F_OK) != 0) {
        std::scoped_lock lock(g_ltlfilt_mutex);
        answers.emplace(key, formula);
        return formula;
    }
    // The canonical form is what the subprocess is asked about, so that one
    // answer serves every spelling that reaches this key.
    const auto [simplified, result] = one_shot_simplify(binary, key);
    std::scoped_lock lock(g_ltlfilt_mutex);
    record_exec<LtlfiltStats>(result);
    // The unsimplified fallback is cached like any other result, including
    // after a timeout: a formula that blew the budget once will blow it every
    // time, and re-paying the wait per occurrence is the stall this timeout
    // exists to avoid.
    answers.emplace(key, simplified);
    return simplified;
}

bool has_weak_operator(const std::string& formula) {
    for (std::size_t pos = 0; pos < formula.size(); ++pos) {
        if (formula[pos] != 'W' && formula[pos] != 'M') {
            continue;
        }
        const bool joined_left =
            pos > 0 && is_identifier_char(formula[pos - 1]);
        const bool joined_right =
            pos + 1 < formula.size() && is_identifier_char(formula[pos + 1]);
        if (!joined_left && !joined_right) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> rewrite_weak_operators(const std::string& formula) {
    PEREDUR_PROFILE_SCOPE("ltlfilt/rewrite_weak_operators");
    static std::unordered_map<std::string, std::optional<std::string>> cache;
    // The canonical key, for the reason simplify_ltl gives: this value is a
    // formula, so it must come back over the caller's own atom names.
    const std::string& key = formula_key::canonical(formula);
    {
        std::scoped_lock lock(g_ltlfilt_mutex);
        const auto found = cache.find(key);
        if (found != cache.end()) {
            LtlfiltStats::n_remove_wm_hits++;
            return found->second;
        }
        LtlfiltStats::n_remove_wm_execs++;
    }
    const auto remember = [&key](std::optional<std::string> result) {
        std::scoped_lock lock(g_ltlfilt_mutex);
        // Failures are cached like successes, as in simplify_ltl: a formula
        // ltlfilt cannot rewrite once it cannot rewrite ever, and a timeout
        // that blew the budget once will blow it again.
        cache.emplace(key, result);
        return result;
    };
    const std::string binary = ltlfilt_path();
    if (access(binary.c_str(), F_OK) != 0) {
        return remember(std::nullopt);
    }
    const ProcessResult result = execute_and_capture(
        {binary, "--remove-wm", "-p", "-f", key}, ltlfilt_timeout());
    {
        std::scoped_lock lock(g_ltlfilt_mutex);
        record_exec<LtlfiltStats>(result);
    }
    if (result.m_exit_code != 0 || result.m_output.empty()) {
        return remember(std::nullopt);
    }
    std::string rewritten = result.m_output;
    while (!rewritten.empty() && rewritten.back() == '\n') {
        rewritten.pop_back();
    }
    // --remove-wm is defined to eliminate both operators, so a survivor means
    // the rewrite did not do what its caller is relying on. Report no answer
    // rather than pass it on.
    if (rewritten.empty() || has_weak_operator(rewritten)) {
        return remember(std::nullopt);
    }
    return remember(rewritten);
}

std::optional<bool> spot_satisfiable(const std::string& formula,
                                     std::chrono::milliseconds timeout) {
    PEREDUR_PROFILE_SCOPE("ltlfilt/spot_satisfiable");
    const std::string binary = ltlfilt_path();
    if (access(binary.c_str(), F_OK) != 0) {
        return std::nullopt;
    }
    {
        std::scoped_lock lock(g_ltlfilt_mutex);
        LtlfiltStats::n_satisfiable_execs++;
    }
    const ProcessResult result =
        execute_and_capture({binary, "--satisfiable", "-f", formula}, timeout);
    {
        std::scoped_lock lock(g_ltlfilt_mutex);
        record_exec<LtlfiltStats>(result);
    }
    // Tested before the exit code: the SIGKILL that ends a timed-out call
    // leaves a status this would otherwise read as a verdict.
    if (result.m_timed_out) {
        return std::nullopt;
    }
    // ltlfilt's filter convention, as in ltl_equivalent: 0 means the formula
    // matched the --satisfiable filter, 1 means it did not. Every other status
    // is a parse error or a crash, and reporting either as UNSAT would drop a
    // candidate over a spelling.
    if (result.m_exit_code == 0) {
        return true;
    }
    if (result.m_exit_code == 1) {
        return false;
    }
    return std::nullopt;
}

bool ltl_equivalent(const std::string& lhs, const std::string& rhs) {
    const std::string binary = ltlfilt_path();
    if (access(binary.c_str(), F_OK) != 0) {
        return true;
    }
    const ProcessResult result = execute_and_capture(
        {binary, "--equivalent-to=" + rhs, "-f", lhs}, ltlfilt_timeout());
    if (result.m_timed_out) {
        std::scoped_lock lock(g_ltlfilt_mutex);
        LtlfiltStats::n_timeouts++;
    }
    // ltlfilt's filter convention: exit 0 means the input formula (lhs)
    // matched (i.e. is equivalent to rhs); exit 1 means it didn't. Only exit 1
    // is a mismatch — any other status (parse error, crash, the SIGKILL from a
    // timeout) is inconclusive, and reported as equivalent rather than as a
    // false mismatch.
    return result.m_exit_code != 1;
}
