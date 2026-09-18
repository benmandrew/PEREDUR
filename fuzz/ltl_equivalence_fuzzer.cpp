/// @file ltl_equivalence_fuzzer.cpp
/// @brief libFuzzer differential-testing target: for a randomly generated
///        Requirement, checks that the hand-rolled requirement_to_ltl()
///        translation is logically equivalent (via ltlfilt) to the LTL the
///        real FRET formaliser CLI derives from the same requirement's
///        FRETish text. A mismatch means the hand-rolled translator has
///        drifted from FRET's own semantics for some input shape, so it
///        aborts to let libFuzzer capture and minimise the repro.
///
/// Build: requires a clang++ with libFuzzer support on PATH (see
/// fuzz/CMakeLists.txt, gated behind -DPEREDUR_FUZZ=ON). Each input spawns a
/// ltlfilt subprocess (the formaliser CLI process is reused across inputs
/// via global_formaliser()), so this runs orders of magnitude slower than a
/// typical libFuzzer target — that's expected for differential testing
/// against external tools, not a bug.

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "requirement.hpp"
#include "runner/formaliser.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"

namespace {

// Turns fuzzer-provided bytes into bounded choices. Never fails: reading
// past the end of the input just yields 0, so every byte sequence (however
// short) produces some well-formed Requirement.
class ByteConsumer {
   public:
    ByteConsumer(const uint8_t* data, std::size_t size)
        : m_data(data), m_size(size) {}

    // Returns a value in [0, bound). bound must be > 0.
    std::uint32_t next(std::uint32_t bound) {
        const std::uint8_t byte = m_pos < m_size ? m_data[m_pos++] : 0;
        return static_cast<std::uint32_t>(byte) % bound;
    }

   private:
    const uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
};

// Generates a small propositional formula string over a 3-atom alphabet,
// bounded to `depth` levels of nesting so the resulting LTL (and the
// automata ltlfilt/ltl2tgba build from it) stays cheap to check.
std::string generate_formula(ByteConsumer& bytes, int depth) {
    static const std::array<const char*, 3> atoms = {"p", "q", "r"};
    if (depth <= 0 || bytes.next(3) == 0) {
        const std::uint32_t choice = bytes.next(5);
        if (choice == 3) {
            return "true";
        }
        if (choice == 4) {
            return "false";
        }
        return atoms[bytes.next(3)];
    }
    const std::string lhs = generate_formula(bytes, depth - 1);
    switch (bytes.next(5)) {
        case 0:
            return "!(" + lhs + ")";
        case 1:
            return "(" + lhs + ") & (" + generate_formula(bytes, depth - 1) +
                   ")";
        case 2:
            return "(" + lhs + ") | (" + generate_formula(bytes, depth - 1) +
                   ")";
        case 3:
            return "(" + lhs + ") -> (" + generate_formula(bytes, depth - 1) +
                   ")";
        default:
            return "(" + lhs + ") <-> (" + generate_formula(bytes, depth - 1) +
                   ")";
    }
}

// Bounds tick counts to keep the bounded-operator expansions (both the
// hand-rolled X-chains and the CLI's F[a,b]/G[a,b] forms) small.
constexpr std::size_t kMaxTicks = 5;
constexpr int kMaxFormulaDepth = 2;

Timing generate_timing(ByteConsumer& bytes) {
    switch (bytes.next(9)) {
        case 0:
            return timing::immediately();
        case 1:
            return timing::next_timepoint();
        case 2:
            return timing::within_ticks(bytes.next(kMaxTicks + 1));
        case 3:
            return timing::for_ticks(bytes.next(kMaxTicks + 1));
        case 4:
            return timing::after_ticks(bytes.next(kMaxTicks + 1));
        case 5:
            return timing::eventually();
        case 6:
            return timing::always();
        case 7:
            return timing::until(
                Formula(generate_formula(bytes, kMaxFormulaDepth)));
        default:
            return timing::before(
                Formula(generate_formula(bytes, kMaxFormulaDepth)));
    }
}

// The mode is either a fresh atom or one the condition, response and stop also
// draw from, so a lowering that confused the mode with the formulae around it
// would show. Neither name may be a FRET keyword: a mode called `mode` makes
// every scoped sentence a parse error.
Scope generate_scope(ByteConsumer& bytes) {
    const std::string mode = bytes.next(2) == 0 ? "m" : "p";
    switch (bytes.next(8)) {
        case 0:
            return Scope{};
        case 1:
            return Scope{ScopeKind::In, mode};
        case 2:
            return Scope{ScopeKind::NotIn, mode};
        case 3:
            return Scope{ScopeKind::Before, mode};
        case 4:
            return Scope{ScopeKind::After, mode};
        case 5:
            return Scope{ScopeKind::OnlyIn, mode};
        case 6:
            return Scope{ScopeKind::OnlyBefore, mode};
        default:
            return Scope{ScopeKind::OnlyAfter, mode};
    }
}

enum class Verdict { Equivalent, Mismatch, Rejected, Inconclusive };

const char* verdict_name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Equivalent:
            return "LTL equivalent";
        case Verdict::Mismatch:
            return "LTL equivalence mismatch";
        case Verdict::Rejected:
            return "FRET CLI rejected the generated FRETish";
        default:
            return "ltlfilt could not decide the equivalence";
    }
}

// ltl_equivalent() folds an undecided call into "equivalent", which suits a
// caller that must not stall on a false mismatch. Here that fold would be a
// silent skip, so ltlfilt runs directly and without a budget: formulae this
// small decide in milliseconds.
Verdict compare_ltl(const std::string& lhs, const std::string& rhs) {
    const ProcessResult result = execute_and_capture(
        {ltlfilt_path(), "--equivalent-to=" + rhs, "-f", lhs});
    switch (result.m_exit_code) {
        case 0:
            return Verdict::Equivalent;
        case 1:
            return Verdict::Mismatch;
        default:
            std::cerr << "ltlfilt exit " << result.m_exit_code << ": "
                      << result.m_output << "\n";
            return Verdict::Inconclusive;
    }
}

// A zero-tick bounded timing under a non-Global scope makes FRET print the
// empty range `F[0,-1] φ`, which SPOT will not parse. No timepoint lies in
// [0,-1], so that term is false, and likewise `G[0,-1] φ` is true. Folding them
// here lets those rows be compared instead of skipped. The operand is either a
// parenthesised group or a bare atom, the only shapes FRET's fully
// parenthesised output takes.
// Returns the index one past the operand that starts at or after `pos`.
std::size_t operand_end(const std::string& ltl, std::size_t pos) {
    while (pos < ltl.size() && ltl[pos] == ' ') {
        ++pos;
    }
    if (pos < ltl.size() && ltl[pos] == '(') {
        int nesting = 0;
        do {
            if (ltl[pos] == '(') {
                ++nesting;
            } else if (ltl[pos] == ')') {
                --nesting;
            }
            ++pos;
        } while (pos < ltl.size() && nesting > 0);
        return pos;
    }
    while (pos < ltl.size() &&
           (std::isalnum(static_cast<unsigned char>(ltl[pos])) != 0 ||
            ltl[pos] == '_')) {
        ++pos;
    }
    return pos;
}

std::string fold_empty_ranges(std::string ltl) {
    const std::array<std::pair<std::string, std::string>, 2> folds = {
        {{"F[0,-1]", "(false)"}, {"G[0,-1]", "(true)"}}};
    for (const auto& [op, value] : folds) {
        std::size_t start = 0;
        while ((start = ltl.find(op, start)) != std::string::npos) {
            const std::size_t end = operand_end(ltl, start + op.size());
            ltl.replace(start, end - start, value);
        }
    }
    return ltl;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    if (size < 4) {
        return 0;
    }
    ByteConsumer bytes(data, size);
    // The structure is drawn before the formulae. Reading past the input
    // yields 0, so the other order spends a short input on formulae and
    // leaves almost every case Global and `immediately`.
    const Scope scope = generate_scope(bytes);
    const Timing tim = generate_timing(bytes);
    const ConditionType condition_type =
        bytes.next(2) == 0 ? ConditionType::Continual : ConditionType::Trigger;
    const std::string condition_str = generate_formula(bytes, kMaxFormulaDepth);
    const std::string response_str = generate_formula(bytes, kMaxFormulaDepth);

    const Requirement req(Formula(condition_str), Formula(response_str), tim,
                          condition_type, /*weakenable=*/true,
                          /*removed=*/false, scope);
    const std::string hand_rolled_ltl = req.m_ltl;
    const std::string fretish = req.to_string();
    const std::string cli_ltl = global_formaliser().formalise(fretish);
    // Every generated sentence is one PEREDUR itself would emit, so a CLI
    // rejection is a to_string() bug and an inconclusive ltlfilt call is a
    // comparison that never happened. Both abort: skipping them once hid every
    // scoped requirement from this target.
    const Verdict verdict =
        cli_ltl.empty()
            ? Verdict::Rejected
            : compare_ltl(hand_rolled_ltl, fold_empty_ranges(cli_ltl));
    if (verdict != Verdict::Equivalent) {
        std::cerr << verdict_name(verdict) << ":\n"
                  << "  condition:       " << condition_str << "\n"
                  << "  response:        " << response_str << "\n"
                  << "  scope:           " << ::to_string(scope) << "\n"
                  << "  timing/type:     " << ::to_string(tim) << " / "
                  << (condition_type == ConditionType::Trigger ? "Trigger"
                                                               : "Continual")
                  << "\n"
                  << "  FRETish:         " << fretish << "\n"
                  << "  hand-rolled LTL: " << hand_rolled_ltl << "\n"
                  << "  CLI LTL:         " << cli_ltl << "\n";
        std::abort();
    }
    return 0;
}
