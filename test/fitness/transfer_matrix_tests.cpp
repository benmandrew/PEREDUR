#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "fitness/exhaustive_count.hpp"
#include "fitness/model_counter.hpp"
#include "fitness/transfer_matrix.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "transfer_matrix";

using CountGrid = std::vector<std::vector<Count>>;

CountVector count_vector_from_values(const std::vector<Count>& values) {
    CountVector counts(static_cast<Eigen::Index>(values.size()));
    for (Eigen::Index index = 0; index < counts.size(); ++index) {
        counts(index) = values[static_cast<std::size_t>(index)];
    }
    return counts;
}

CountMatrix count_matrix_from_rows(const CountGrid& rows) {
    const auto row_count = static_cast<Eigen::Index>(rows.size());
    const Eigen::Index column_count =
        rows.empty() ? 0 : static_cast<Eigen::Index>(rows.front().size());
    CountMatrix matrix(row_count, column_count);
    for (Eigen::Index row = 0; row < row_count; ++row) {
        for (Eigen::Index column = 0; column < column_count; ++column) {
            matrix(row, column) = rows[static_cast<std::size_t>(row)]
                                      [static_cast<std::size_t>(column)];
        }
    }
    return matrix;
}

using Trace = std::vector<std::pair<bool, bool>>;

bool satisfies_immediately(const Trace& trace) {
    return std::all_of(trace.begin(), trace.end(), [](const auto& step) {
        const auto& [trigger, response] = step;
        return !trigger || response;
    });
}

bool satisfies_next_timepoint(const Trace& trace) {
    for (std::size_t idx = 0; idx + 1 < trace.size(); ++idx) {
        if (trace[idx].first && !trace[idx + 1].second) {
            return false;
        }
    }
    return true;
}

bool satisfies_within_ticks(const Trace& trace, std::size_t ticks) {
    std::size_t countdown = 0;
    for (const auto& [trigger, response] : trace) {
        if (response) {
            countdown = 0;
            continue;
        }
        if (countdown == 0) {
            countdown = trigger ? ticks : 0;
            continue;
        }
        if (countdown == 1) {
            return false;
        }
        countdown = trigger ? ticks : countdown - 1;
    }
    return true;
}

bool satisfies_for_ticks(const Trace& trace, std::size_t ticks) {
    std::size_t countdown = 0;
    for (const auto& [trigger, response] : trace) {
        if (!response) {
            if (countdown > 0 || trigger) {
                return false;
            }
            continue;
        }
        if (trigger) {
            countdown = ticks;
            continue;
        }
        if (countdown > 0) {
            --countdown;
        }
    }
    return true;
}

bool satisfies_after_ticks(const Trace& trace, std::size_t ticks) {
    std::size_t countdown = 0;
    for (const auto& [trigger, response] : trace) {
        if (countdown == 0) {
            if (!trigger) {
                continue;
            }
            if (response) {
                return false;
            }
            // FRET: after n res = (for n !res) & (within (n+1) res)
            // = !res at t=0..n, res at t=n+1, so n+1 waiting steps.
            countdown = ticks + 1;
        } else if (countdown == 1) {
            if (!response || trigger) {
                return false;
            }
            countdown = 0;
        } else {
            // Re-triggering during the waiting period is a violation: the
            // SPOT automaton for G(P -> body) reaches a dead state when P
            // fires while an obligation is already pending.
            if (response || trigger) {
                return false;
            }
            --countdown;
        }
    }
    return true;
}

bool satisfies_eventually(const Trace& trace) {
    bool pending = false;
    for (const auto& [trigger, response] : trace) {
        if (trigger && !response) {
            pending = true;
        } else if (response) {
            pending = false;
        }
    }
    return !pending;
}

bool trace_satisfies_requirement(const Requirement& requirement,
                                 const Trace& trace) {
    if (trace.empty()) {
        return true;
    }
    return std::visit(
        [&trace](const auto& timing_value) {
            using T = std::decay_t<decltype(timing_value)>;
            if constexpr (std::is_same_v<T, timing::Immediately>) {
                return satisfies_immediately(trace);
            } else if constexpr (std::is_same_v<T, timing::NextTimepoint>) {
                return satisfies_next_timepoint(trace);
            } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
                return satisfies_within_ticks(trace, timing_value.m_ticks);
            } else if constexpr (std::is_same_v<T, timing::ForTicks>) {
                return satisfies_for_ticks(trace, timing_value.m_ticks);
            } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
                return satisfies_after_ticks(trace, timing_value.m_ticks);
            } else {
                return satisfies_eventually(trace);
            }
        },
        requirement.m_timing);
}

// Enumerate all 4^k traces over atoms {trigger, response} and count how many
// satisfy req according to the brute-force oracle.
Count brute_force_trace_count(const Requirement& req, std::size_t step_count) {
    Count count = 0;
    const std::size_t total = std::size_t{1} << (2U * step_count);
    for (std::size_t mask = 0; mask < total; ++mask) {
        Trace trace;
        trace.reserve(step_count);
        for (std::size_t step = 0; step < step_count; ++step) {
            trace.emplace_back(
                static_cast<bool>((mask >> (2U * step)) & 1U),
                static_cast<bool>((mask >> (2U * step + 1U)) & 1U));
        }
        if (trace_satisfies_requirement(req, trace)) {
            ++count;
        }
    }
    return count;
}

void expect_exact_count(Count actual, Count expected,
                        const std::string& label) {
    if (actual != expected) {
        std::ostringstream message;
        message << label << ": expected " << count_to_string(expected)
                << " but found " << count_to_string(actual);
        fail(message.str());
    }
}

void expect_trace_counts(const TransferSystem& system,
                         const std::vector<Count>& expected_counts,
                         const std::string& label) {
    for (std::size_t index = 0; index < expected_counts.size(); ++index) {
        const std::size_t trace_length = index + 1;
        expect_exact_count(
            count_traces(system, trace_length), expected_counts[index],
            label + ": count for trace length " + std::to_string(trace_length));
    }
}

void expect_matrix_equals(const CountMatrix& actual, const CountGrid& expected,
                          const std::string& label) {
    expect(actual.rows() == static_cast<Eigen::Index>(expected.size()),
           label + ": unexpected row count");

    const Eigen::Index expected_columns =
        expected.empty() ? 0
                         : static_cast<Eigen::Index>(expected.front().size());
    expect(actual.cols() == expected_columns,
           label + ": unexpected column count");

    for (Eigen::Index row = 0; row < actual.rows(); ++row) {
        for (Eigen::Index column = 0; column < actual.cols(); ++column) {
            expect_exact_count(actual(row, column),
                               expected[static_cast<std::size_t>(row)]
                                       [static_cast<std::size_t>(column)],
                               label + ": entry (" + std::to_string(row) +
                                   ", " + std::to_string(column) + ")");
        }
    }
}

struct TransferSystemCase {
    std::string m_label;
    Requirement m_requirement;
    std::vector<Count> m_expected_trace_counts;
};

TEST(test_transfer_system_cases) {
    const std::vector<TransferSystemCase> cases = {
        {"immediately",
         Requirement(Formula("P"), Formula("Q"), timing::immediately()),
         {3, 9, 27, 81, 243}},
        {"next-timepoint",
         Requirement(Formula("P"), Formula("Q"), timing::next_timepoint()),
         {4, 12, 36, 108, 324}},
        // k=3,4 counts differ from the old bespoke automaton: SPOT's
        // G(P -> F[0..2] Q) uses standard LTL semantics with no
        // restart-on-retrigger.
        {"within-ticks (P,Q)",
         Requirement(Formula("P"), Formula("Q"), timing::within_ticks(2)),
         {4, 16, 60, 228}},
        {"within-ticks (P,P|Q)",
         Requirement(Formula("P"), Formula("P|Q"), timing::within_ticks(2)),
         {4, 16, 64, 256}},
        {"for-ticks (P,Q)",
         Requirement(Formula("P"), Formula("Q"), timing::for_ticks(2)),
         {3, 8, 20, 49}},
        {"for-ticks (P,P|Q)",
         Requirement(Formula("P"), Formula("P|Q"), timing::for_ticks(2)),
         {4, 14, 46, 148}},
    };

    for (const TransferSystemCase& test_case : cases) {
        const TransferSystem system =
            build_transfer_system(test_case.m_requirement);
        expect_trace_counts(system, test_case.m_expected_trace_counts,
                            test_case.m_label + " trace counts");
    }
}

struct TraceAcceptanceCase {
    std::string m_label;
    Requirement m_requirement;
    std::vector<std::pair<bool, bool>> m_trace;
    bool m_expected_acceptance = false;
};

TEST(test_trace_acceptance_cases) {
    const std::vector<TraceAcceptanceCase> cases = {
        {"immediately positive trace",
         Requirement(Formula("P"), Formula("Q"), timing::immediately()),
         {{false, false}, {false, true}, {true, true}, {false, false}},
         true},
        {"immediately negative trace",
         Requirement(Formula("P"), Formula("Q"), timing::immediately()),
         {{false, true}, {true, false}, {false, true}},
         false},
        {"next-timepoint positive trace",
         Requirement(Formula("P"), Formula("Q"), timing::next_timepoint()),
         {{true, false}, {false, true}, {true, true}, {false, true}},
         true},
        {"next-timepoint negative trace",
         Requirement(Formula("P"), Formula("Q"), timing::next_timepoint()),
         {{true, false}, {false, false}},
         false},
        {"within-ticks positive trace",
         Requirement(Formula("P"), Formula("Q"), timing::within_ticks(2)),
         {{true, false}, {false, false}, {false, true}},
         true},
        {"within-ticks negative trace",
         Requirement(Formula("P"), Formula("Q"), timing::within_ticks(2)),
         {{true, false}, {false, false}, {false, false}},
         false},
        {"for-ticks positive trace",
         Requirement(Formula("P"), Formula("Q"), timing::for_ticks(2)),
         {{true, true}, {false, true}, {false, true}, {false, false}},
         true},
        {"for-ticks negative trace",
         Requirement(Formula("P"), Formula("Q"), timing::for_ticks(2)),
         {{true, true}, {false, true}, {false, false}},
         false},
    };

    for (const TraceAcceptanceCase& test_case : cases) {
        const bool actual = trace_satisfies_requirement(test_case.m_requirement,
                                                        test_case.m_trace);
        expect(actual == test_case.m_expected_acceptance,
               test_case.m_label + ": oracle acceptance mismatch");
    }
}

// Cross-validates count_traces against the brute-force oracle for all timing
// variants (including after_ticks and eventually which were previously
// untested). For k=1..4 and 2 atoms, the oracle enumerates 4^k traces.
// within-ticks/for-ticks are excluded: the oracle's restart-on-retrigger
// semantics diverge from SPOT's actual LTL automaton at k>=3 (see the comment
// on within-ticks/for-ticks in test_transfer_system_cases), so they are
// instead covered there via hardcoded expected counts.
TEST(test_transfer_system_vs_oracle) {
    const std::vector<std::pair<std::string, Requirement>> cases = {
        {"immediately",
         Requirement(Formula("P"), Formula("Q"), timing::immediately())},
        {"next-timepoint",
         Requirement(Formula("P"), Formula("Q"), timing::next_timepoint())},
        {"after-ticks",
         Requirement(Formula("P"), Formula("Q"), timing::after_ticks(1))},
        {"eventually",
         Requirement(Formula("P"), Formula("Q"), timing::eventually())},
    };
    for (const auto& [label, req] : cases) {
        const TransferSystem system = build_transfer_system(req);
        for (std::size_t k = 1; k <= 4; ++k) {
            expect_exact_count(count_traces(system, k),
                               brute_force_trace_count(req, k),
                               label + " k=" + std::to_string(k));
        }
    }
}

TEST(test_weighted_transition_matrix_cases) {
    struct WeightedTransitionCase {
        std::string m_label;
        std::vector<Count> m_valuation_counts;
        CountGrid m_transition_matrix;
        bool m_transition_matrix_is_weighted = false;
        CountGrid m_expected_weighted_matrix;
    };

    const std::vector<WeightedTransitionCase> cases = {
        {"weighted transition column scaling",
         {5, 7},
         {{1, 2}, {3, 4}},
         false,
         {{5, 14}, {15, 28}}},
        {"weighted transition passthrough",
         {3, 9},
         {{2, 4}, {6, 8}},
         true,
         {{2, 4}, {6, 8}}},
    };

    for (const WeightedTransitionCase& test_case : cases) {
        TransferSystem system;
        const std::size_t state_count = test_case.m_valuation_counts.size();
        system.m_states.assign(state_count, State{});
        system.m_valuation_counts =
            count_vector_from_values(test_case.m_valuation_counts);
        system.m_transition_matrix =
            count_matrix_from_rows(test_case.m_transition_matrix);
        system.m_transition_matrix_is_weighted =
            test_case.m_transition_matrix_is_weighted;

        const CountMatrix weighted = weighted_transition_matrix(system);
        expect_matrix_equals(weighted, test_case.m_expected_weighted_matrix,
                             test_case.m_label);
    }
}

TEST(test_count_traces_formula) {
    CountMatrix weighted(2, 2);
    weighted << 1, 2, 3, 4;

    TransferSystem system;
    system.m_states.assign(2, State{});
    system.m_transition_matrix = weighted;
    system.m_transition_matrix_is_weighted = true;

    const std::vector<Count> expected = {1, 3, 17};
    for (std::size_t k = 0; k < expected.size(); ++k) {
        expect(count_traces(system, k) == expected[k],
               "start-state trace count for k=" + std::to_string(k));
    }
}

// build_transfer_system_from_ltl weights each edge by
// ganak_models(guard) * 2^(n_total_atoms - n_mentioned), so a single-state
// automaton with self-loop weight W counts exactly W^k traces. Every case below
// asserts a value past 2^128, i.e. one that a 128-bit Count could not hold --
// which is the regime the long double Count exists for, and the one the rest of
// this file (expected counts of 1, 3, 17) never reaches.
Count exact_power_of_two(int exponent) { return std::ldexp(1.0L, exponent); }

Count power_of_three(std::size_t exponent) {
    Count result = 1.0L;
    for (std::size_t step = 0; step < exponent; ++step) {
        result *= 3.0L;
    }
    return result;
}

TEST(test_count_traces_beyond_128_bits) {
    struct WideCase {
        std::string m_ltl;
        std::size_t m_n_atoms = 0;
        std::size_t m_step_count = 0;
        Count m_expected = 0;
    };
    const std::vector<WideCase> cases = {
        // A tautology mentions no atoms, so every one of the N is free and the
        // "t" self-loop weighs 2^N. Powers of two are bit-exact in long double
        // at any magnitude (mantissa 1.0, exponent e), so these comparisons are
        // exact.
        {"1", 10, 20, exact_power_of_two(200)},
        {"1", 32, 100, exact_power_of_two(3200)},
        // Deliberately near long double's 2^16384 ceiling.
        {"1", 50, 300, exact_power_of_two(15000)},
        // Drives ganak and the HOA label parse with ten genuinely mentioned
        // atoms, while keeping the oracle exact: the guard has a single model
        // over them, so the self-loop weighs 2^(20-10).
        {"G(a0 & a1 & a2 & a3 & a4 & a5 & a6 & a7 & a8 & a9)", 20, 30,
         exact_power_of_two(10 * 30)},
        // The strongest case: three of the four (a, b) valuations satisfy the
        // guard, so the self-loop weighs 3 * 2^8 and the count is
        // 3^k * 2^(8k) -- mantissa exactness and exponent range at once, where
        // the 2^160 factor alone already overflows 128 bits. k stays <= 40 so
        // that 3^k remains under 2^64 and is therefore still exact in long
        // double's 64-bit mantissa.
        {"G(a | b)", 10, 20, power_of_three(20) * exact_power_of_two(8 * 20)},
    };
    for (const WideCase& wide : cases) {
        const TransferSystem system =
            build_transfer_system_from_ltl(wide.m_ltl, wide.m_n_atoms);
        expect_exact_count(
            count_traces(system, wide.m_step_count), wide.m_expected,
            wide.m_ltl + " over " + std::to_string(wide.m_n_atoms) +
                " atoms at k=" + std::to_string(wide.m_step_count));
    }
}

// Now that Count is floating-point, overflow is reported by an isfinite check
// rather than a carry flag. Past the ceiling it must still be reported, not
// silently saturated to a finite maximum.
TEST(test_count_overflow_detected_at_ceiling) {
    Count result = 0;
    expect(count_mul_overflow(std::numeric_limits<Count>::max(), 2.0L, result),
           "count_mul_overflow: max * 2 must report overflow");
    expect(!std::isfinite(result),
           "count_mul_overflow: an overflowing product must not stay finite");
}

// Checked against hand-counted truth tables rather than against a second
// implementation, the whole claim of this counter being that it enumerates.
TEST(test_exhaustive_count_small_cases) {
    struct Case {
        std::string m_formula;
        Count m_models;
    };
    const std::vector<Case> cases = {
        {"(a)", 1},
        {"(!a)", 1},
        {"(a) & (b)", 1},
        {"(a) | (b)", 3},
        {"(a) -> (b)", 3},
        {"(a) <-> (b)", 2},
        {"(a) & (!a)", 0},
        {"(a) | (!a)", 2},
        {"((a) & (b)) | ((!a) & (c))", 4},
        // Seven atoms is the first width whose truth table does not fit one
        // machine word, and thirteen is the first that needs more than a
        // cache line of them; both are inside the 12-atom widest guard
        // measured, so the word-boundary arithmetic is what these pin.
        {"(a) | (b) | (c) | (d) | (e) | (f) | (g)", 127},
        {"(a) & (b) & (c) & (d) & (e) & (f) & (g) & (h) & (i) & (j) & (k) & "
         "(l) & (m)",
         1},
    };
    for (const Case& one : cases) {
        const std::optional<Count> models =
            count_models_exhaustively(one.m_formula);
        expect(models.has_value(),
               "exhaustive-count: declined " + one.m_formula);
        expect(models.value_or(-1) == one.m_models,
               "exhaustive-count: wrong model count for " + one.m_formula);
    }
}

// The three shapes it must hand back to the model counter rather than answer
// itself. A wrong answer here is silent, the caller having no way to tell a
// declined count from a refused one.
TEST(test_exhaustive_count_declines_what_it_cannot_answer) {
    expect(!count_models_exhaustively("G((a))").has_value(),
           "exhaustive-count: answered a temporal formula");
    expect(!count_models_exhaustively("(a) U (b)").has_value(),
           "exhaustive-count: answered a temporal formula");
    expect(!count_models_exhaustively("(a) &").has_value(),
           "exhaustive-count: answered a string it cannot parse");
    std::string wide = "(a0)";
    for (std::size_t index = 1; index <= k_exhaustive_count_max_atoms;
         ++index) {
        wide += " & (a" + std::to_string(index) + ")";
    }
    expect(!count_models_exhaustively(wide).has_value(),
           "exhaustive-count: answered a formula past the width cap");
}

#ifdef __SIZEOF_INT128__
// The cases above all stay exact: powers of two are bit-exact at any exponent,
// and the 3^k factor was kept under 2^64. This one deliberately pushes the
// count's non-power-of-two part past the 64-bit mantissa so it is genuinely
// rounded -- the trade the float Count makes -- and pins that the rounding is
// faithful (correct to a few ulps), not garbage. The exact reference is 3^70
// held in unsigned __int128, the very width the count no longer uses; the test
// compiles out where that width is unavailable, since the exactness bound is
// x86-specific anyway.
TEST(test_count_traces_rounds_faithfully_above_64_bits) {
    const std::size_t step_count = 70;  // 6^70 = 3^70 * 2^70; 3^70 ~ 2^111
    const std::size_t n_atoms = 3;      // G(a|b): self-loop weight 3 * 2^(3-2)
    const TransferSystem system =
        build_transfer_system_from_ltl("G(a | b)", n_atoms);

    // __extension__ silences -Wpedantic's "ISO C++ does not support __int128":
    // the reference value is deliberately held in that wider type (see above).
    __extension__ unsigned __int128 exact_three_pow = 1;
    for (std::size_t step = 0; step < step_count; ++step) {
        exact_three_pow *= 3U;
    }
    // count = weight^k = 3^k * 2^((n_atoms - 2) * k); the 2^70 scale is exact.
    const int scale_exponent = static_cast<int>((n_atoms - 2) * step_count);
    const Count reference = static_cast<Count>(exact_three_pow) *
                            exact_power_of_two(scale_exponent);

    const Count actual = count_traces(system, step_count);
    expect(
        actual > exact_power_of_two(64),
        "precondition: the count must exceed 2^64 for rounding to be in play");
    const Count relative_error = std::fabs(actual - reference) / reference;
    expect(relative_error < 1e-16,
           "count_traces: a count past 2^64 must round faithfully (relative "
           "error " +
               std::to_string(static_cast<double>(relative_error)) + ")");
}
#endif

}  // namespace
