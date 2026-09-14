#include "runner/black.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formula_key.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "tool_paths.hpp"

namespace {

bool is_identifier_char(char chr) {
    return std::isalnum(static_cast<unsigned char>(chr)) != 0 || chr == '_';
}

// First-stage budget for the SPOT satisfiability query. Every one of the 5,579
// queries taken from real runs was decided well inside it -- p99 15ms, max
// 140ms -- so this is not a throughput knob but a bound on the blowup case,
// where the automaton construction would otherwise run unboundedly.
// Deliberately not a config key: no campaign would sweep it, and the value
// follows from the measured distribution rather than from a preference.
// This codebase spells its boolean constants as atoms named "true"/"false"
// (see prop_formula.hpp). black parses those bare identifiers as free
// variables, so it reports e.g. "G(false)" satisfiable by holding the variable
// forever. It does read "True"/"False" as genuine constants, so rewrite whole
// tokens on the way in — without this, black is only ever correct about
// constants when ltlfilt happens to fold them away first, which it cannot do
// when the binary is missing or errors. SPOT needs no equivalent: ltlfilt and
// ltl2tgba already treat "true"/"false" as constants.
//
// SPOT's own spellings "0"/"1" are mapped for the mirror-image reason: black
// rejects them as a syntax error rather than misreading them, so any formula
// ltlfilt has printed aborts the run instead of answering it. Whole tokens
// only, so an atom named "g0" or a "10" keeps its digits — is_identifier_char
// counts digits, which is what makes the existing boundary test cover these.
std::string to_black_constants(const std::string& formula) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>,
                                4>
        k_constants{{{"true", "True"},
                     {"false", "False"},
                     {"1", "True"},
                     {"0", "False"}}};
    std::string out;
    out.reserve(formula.size());
    std::size_t pos = 0;
    while (pos < formula.size()) {
        bool rewritten = false;
        if (pos == 0 || !is_identifier_char(formula[pos - 1])) {
            for (const auto& [atom, constant] : k_constants) {
                const std::size_t end = pos + atom.size();
                if (formula.compare(pos, atom.size(), atom) != 0 ||
                    (end < formula.size() &&
                     is_identifier_char(formula[end]))) {
                    continue;
                }
                out.append(constant);
                pos = end;
                rewritten = true;
                break;
            }
        }
        if (!rewritten) {
            out.push_back(formula[pos]);
            ++pos;
        }
    }
    return out;
}

// SPOT spells the boolean constants "0" and "1", which black rejects outright
// as a syntax error — it has no numeric literal. Any string SPOT has touched
// can therefore arrive already decided, and must be answered here rather than
// forwarded: "0" is unsatisfiable, "1" is satisfiable.
std::optional<bool> constant_answer(const std::string& formula) {
    if (formula == "0") {
        return false;
    }
    if (formula == "1") {
        return true;
    }
    return std::nullopt;
}

}  // namespace

SatisfiabilityChecker& global_sat_checker() {
    static SatisfiabilityChecker instance;
    return instance;
}

std::string black_executable_path() {
#ifdef BLACK_EXECUTABLE_PATH
    static const ToolPath k_path =
        tool_path_from_env("PEREDUR_BLACK_PATH", BLACK_EXECUTABLE_PATH);
    return k_path.m_path;
#else
    assert(false);
    return "";
#endif
}

std::optional<bool> SatisfiabilityChecker::check_satisfiability(
    const std::string& ltl_formula, QueryPolarity polarity) {
    // Two keys into one map, tagged apart so a SPOT spelling can never be
    // read as a canonical one.
    //
    // The first is the caller's own formula in canonical renamed form:
    // satisfiability is invariant under a bijection on the atoms, so every
    // spelling and every naming of one query meets there, and a hit on it
    // skips the ltlfilt exec that computing the second key costs. The second
    // is `normalised`, which is what this cache used alone. Both are kept
    // because neither contains the other: ltlfilt simplifies, which the
    // canonical form deliberately does not, and the canonical form renames,
    // which ltlfilt does not. Keying on the first alone was measured on rg2 at
    // 2,773 execs against 2,211 for the second alone -- ltlfilt's collapse is
    // the larger of the two and must not be given up to gain the renaming.
    const std::string spelling_key =
        "r\x1f" + formula_key::renamed(ltl_formula);
    {
        std::shared_lock lock(m_cache_mutex);
        const auto found = m_cache.find(spelling_key);
        if (found != m_cache.end()) {
            n_cache_hits++;
            return found->second;
        }
    }
    // --simplify is kept on this path, unlike the ltl2tgba and ltlsynt ones
    // that dropped it: black's inputs are single requirement formulae and
    // implication checks, not the deep nested-X conjunctions that make ltlfilt
    // --simplify blow up super-exponentially.
    // Except on the implication sweep `maximal` runs, whose whole-spec
    // queries are exactly those conjunctions, and where the pass was measured
    // at 21.8x the cost of the decision it precedes.
    const std::string normalised =
        m_simplify ? simplify_ltl(ltl_formula) : ltl_formula;
    // A formula that SPOT reduces to a boolean constant is already decided:
    // "0" is unsatisfiable, "1" is valid and therefore satisfiable. The
    // genetic algorithm generates these constantly — mostly implication checks
    // that reduce away entirely — and they are the bulk of what black would
    // otherwise time out on, so answering here skips the subprocess. This is
    // an optimisation only: to_black_constants keeps black correct on these
    // formulae by itself if the folding does not fire.
    if (const std::optional<bool> decided = constant_answer(normalised)) {
        n_constant_folded++;
        std::scoped_lock lock(m_cache_mutex);
        m_cache.emplace(spelling_key, decided);
        return decided;
    }
    const std::string cache_key = "n\x1f" + normalised;
    {
        std::shared_lock lock(m_cache_mutex);
        const auto found = m_cache.find(cache_key);
        if (found != m_cache.end()) {
            n_cache_hits++;
            return found->second;
        }
    }
    n_cache_misses++;
    // Every verdict is written under both keys, so the next arrival by either
    // route finds it.
    const auto remember = [this,
                           &spelling_key](const std::optional<bool>& verdict) {
        m_cache.emplace(spelling_key, verdict);
    };
    // SPOT first, on every query. It decides by automaton emptiness, so its
    // cost tracks the automaton's size rather than the verdict, and it settles
    // in tens of milliseconds the deep nested-X implications that black cannot
    // finish at all: measured over the search's own queries it answered 5,579
    // of 5,579 with a 10ms median, against black's 27ms, and at an X-chain
    // depth of 640 it answers in 30ms where black exceeds 60s.
    //
    // The formula handed over is the --simplify output rather than the
    // original. It is SPOT's own spelling, so SPOT reads it back, and it is
    // the smaller of the two whenever simplification fired at all.
    // Bounded by the caller's own budget as well as by m_spot_budget, so a
    // configured timeout still bounds the whole query rather than the black
    // stage alone -- otherwise a 1000ms setting would admit a 1500ms query. A
    // zero budget disables black's timeout by convention; SPOT keeps its own
    // either way, since the unbounded automaton construction is the case
    // m_spot_budget exists to stop.
    const std::chrono::milliseconds spot_budget =
        m_timeout.count() == 0 ? m_spot_budget
                               : std::min(m_timeout, m_spot_budget);
    if (const std::optional<bool> decided =
            spot_satisfiable(normalised, spot_budget)) {
        n_spot_decided++;
        std::scoped_lock lock(m_cache_mutex);
        m_cache.emplace(cache_key, decided);
        remember(decided);
        return decided;
    }
    // SPOT's own blowup case is a large automaton, which happens on satisfiable
    // formulae -- chained `<->` over `G F` terms, period-2 counters -- and
    // those are precisely the ones black finds a model for in milliseconds. An
    // unsatisfiable query is the mirror image: black proves it only by
    // exhausting a completeness bound, so escalating one SPOT could not build
    // an automaton for buys a second subprocess and the same non-answer.
    if (polarity == QueryPolarity::ExpectUnsat) {
        n_escalations_declined++;
        std::scoped_lock lock(m_cache_mutex);
        m_cache.emplace(cache_key, std::nullopt);
        remember(std::nullopt);
        return std::nullopt;
    }
    const std::string black = black_executable_path();
    assert(access(black.c_str(), F_OK) == 0);
    // -t is never omitted. black has no default timeout of its own, so an
    // unbounded solve parks a scoring-pool thread for as long as it takes,
    // and the search loses a worker rather than an individual.
    //
    // Round up: black's -t takes whole seconds, and truncating turned any
    // sub-second budget into "-t 0", which disables black's own timeout and
    // left the outer deadline as the only one. Rounding up also keeps the
    // inner timeout the looser of the two, so black still gets its chance to
    // answer "UNKNOWN" cleanly before the outer deadline SIGKILLs it. A zero
    // budget stays zero, which disables both sides at once and leaves nothing
    // bounding the solve; the config default is 1000 ms and no shipped
    // configuration sets zero.
    const auto timeout_s =
        std::chrono::ceil<std::chrono::seconds>(m_timeout).count();
    // Pass ltl_formula (not normalised) to black. black does parse SPOT's
    // compact operator notation ("GFa"), but not every token SPOT can emit:
    // "0"/"1" are a syntax error and "xor" is unsupported. The original
    // formula is always otherwise black-compatible because it comes from
    // requirement_to_ltl / implication check construction. The normalised form
    // is the cache key.
    //
    // Weak until is the exception, and it is a soundness problem rather than a
    // parsing one. black reads "a W b" as weak until, exactly as SPOT writes
    // it -- its own trace checker defines it as `G a | (a U b)`. But its
    // infinite-trace encoding puts a negated one into NNF as `(!a) M (!b)`,
    // and the loop-closure rules recognise only F and U as eventualities, so
    // an M request is never forced to be fulfilled and a lasso that never
    // satisfies it is accepted. A validity check is phrased as "is !(phi)
    // unsatisfiable", which puts every W under exactly one negation, so black
    // answers SAT where the truth is UNSAT -- a guarantee that says nothing
    // then reads as one that says something, and the vacuity screen passes it.
    // Confirmed against black 25.09.0 (encoding.cpp `_get_ev` handles only
    // `eventually` and `until`); `(G !a) & (a M b)` is SAT there, and black's
    // own `check` rejects the model its `solve` returns. Unchanged from
    // v0.10.8 to v26.05.0, so no upgrade retires this.
    //
    // Rewriting W and M away in terms of U and R keeps black on the operators
    // its loop rules do cover. Guarded by a lexical scan, so the overwhelming
    // majority of queries -- every FRETISH one, since requirement_to_ltl never
    // emits W -- pay nothing.
    std::string query = ltl_formula;
    if (has_weak_operator(query)) {
        const std::optional<std::string> rewritten =
            rewrite_weak_operators(query);
        if (!rewritten.has_value()) {
            // No answer beats a wrong one: every caller treats nullopt
            // conservatively and keeps the candidate, whereas black's answer
            // here would be confidently wrong in the direction that admits a
            // vacuous repair.
            n_weak_operator_unresolved++;
            std::scoped_lock lock(m_cache_mutex);
            m_cache.emplace(cache_key, std::nullopt);
            remember(std::nullopt);
            return std::nullopt;
        }
        query = *rewritten;
        // The guard above tested the --simplify output, which is not the same
        // formula: simplify_ltl returns its input unchanged when ltlfilt is
        // missing, errors or times out, and --simplify is the pass that blows
        // up super-exponentially on deep nested-X conjunctions. --remove-wm is
        // far cheaper and does not, so it routinely folds to a constant a
        // formula the guard above saw unfolded. Cache it: the answer cost an
        // ltlfilt exec, and it is the same answer for every query sharing the
        // key.
        if (const std::optional<bool> decided = constant_answer(query)) {
            n_constant_folded++;
            std::scoped_lock lock(m_cache_mutex);
            m_cache.emplace(cache_key, decided);
            remember(decided);
            return decided;
        }
    }
    const std::vector<std::string> command = {black, "solve",
                                              "-t",  std::to_string(timeout_s),
                                              "-f",  to_black_constants(query)};
    const auto start = std::chrono::steady_clock::now();
    n_black_calls++;
    const ProcessResult result = execute_and_capture(command, m_timeout);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::scoped_lock lock(m_cache_mutex);
    total_time_s += elapsed;
    total_cpu_s += result.m_cpu_s;
    if (result.m_timed_out) {
        n_timeouts++;
        m_cache.emplace(cache_key, std::nullopt);
        remember(std::nullopt);
        return std::nullopt;
    }
    // Check UNSAT before SAT: the former contains the latter as a substring.
    bool sat = false;
    if (result.m_output.find("UNSAT") != std::string::npos) {
        sat = false;
    } else if (result.m_output.find("SAT") != std::string::npos) {
        sat = true;
    } else if (result.m_output.find("UNKNOWN") != std::string::npos) {
        // black's own internal timeout fired before our outer deadline: it
        // exited normally with "UNKNOWN (stopped at k = N)".  Treat as
        // indeterminate, same as a process-level timeout.
        n_timeouts++;
        m_cache.emplace(cache_key, std::nullopt);
        remember(std::nullopt);
        return std::nullopt;
    } else {
        // black's output crossed a process boundary and didn't match any
        // expected form: don't let assert() (a no-op in release builds) fall
        // through to __builtin_unreachable(), which is undefined behavior if
        // this branch is ever actually taken.
        throw std::runtime_error("unexpected output from black: " +
                                 result.m_output);
    }
    m_cache.emplace(cache_key, sat);
    remember(sat);
    return sat;
}
