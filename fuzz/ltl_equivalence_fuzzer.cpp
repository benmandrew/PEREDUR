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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

#include "requirement.hpp"
#include "runner/formaliser.hpp"
#include "runner/ltlfilt.hpp"

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

Timing generate_timing(ByteConsumer& bytes) {
    switch (bytes.next(7)) {
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
        default:
            return timing::always();
    }
}

// Drawn over one fixed mode name: the fuzzer varies the scope's shape, and the
// mode is an opaque atom to every rule the lowering applies to it.
//
// Bounded timings are left to the caller. At zero ticks FRET's own output holds
// the degenerate `F[0,-1] ...`, which SPOT cannot parse, so the CLI's answer
// there is unusable rather than merely different -- those rows are pinned by
// hand in test/scope_tests.cpp instead.
Scope generate_scope(ByteConsumer& bytes) {
    switch (bytes.next(8)) {
        case 0:
            return Scope{};
        case 1:
            return Scope{ScopeKind::In, "mode"};
        case 2:
            return Scope{ScopeKind::NotIn, "mode"};
        case 3:
            return Scope{ScopeKind::Before, "mode"};
        case 4:
            return Scope{ScopeKind::After, "mode"};
        case 5:
            return Scope{ScopeKind::OnlyIn, "mode"};
        case 6:
            return Scope{ScopeKind::OnlyBefore, "mode"};
        default:
            return Scope{ScopeKind::OnlyAfter, "mode"};
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    if (size < 4) {
        return 0;
    }
    ByteConsumer bytes(data, size);
    constexpr int kMaxFormulaDepth = 2;
    const std::string condition_str = generate_formula(bytes, kMaxFormulaDepth);
    const std::string response_str = generate_formula(bytes, kMaxFormulaDepth);
    const Timing tim = generate_timing(bytes);
    const ConditionType condition_type =
        bytes.next(2) == 0 ? ConditionType::Continual : ConditionType::Trigger;
    const Scope scope = generate_scope(bytes);
    // A zero-tick bounded timing under a bounded scope is the one family the
    // CLI cannot arbitrate, so it is skipped rather than compared. See
    // generate_scope.
    const auto* within = std::get_if<timing::WithinTicks>(&tim);
    const auto* for_ticks = std::get_if<timing::ForTicks>(&tim);
    if (!scope.is_global() &&
        ((within != nullptr && within->m_ticks == 0) ||
         (for_ticks != nullptr && for_ticks->m_ticks == 0))) {
        return 0;
    }

    const Requirement req(Formula(condition_str), Formula(response_str), tim,
                          condition_type, /*weakenable=*/true,
                          /*removed=*/false, scope);
    const std::string hand_rolled_ltl = req.m_ltl;
    const std::string fretish = req.to_string();
    const std::string cli_ltl = global_formaliser().formalise(fretish);
    if (cli_ltl.empty()) {
        // The CLI rejected this FRETish text outright (to_string()'s output
        // validity is covered separately by test/requirement_tests.cpp), so
        // there's nothing to cross-check the hand-rolled LTL against here.
        return 0;
    }
    if (!ltl_equivalent(hand_rolled_ltl, cli_ltl)) {
        std::cerr << "LTL equivalence mismatch:\n"
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
