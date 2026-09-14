#pragma once

/// @file transfer_matrix.hpp
/// @brief TransferSystem type and construction: finite-state automaton
///        (via ltl2tgba + Ganak) used for bounded trace model counting.

#include <Eigen/Dense>

#include <cassert>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "requirement.hpp"

/// Trace counts are only ever consumed as ratios cast to double, so exponent
/// range matters and exact integer width does not. x87 80-bit long double is
/// exact to 2^64 (same as uint64) but reaches 2^16384 rather than 2^128.
using Count = long double;

// Three things below depend on Count being floating-point and break silently
// under an integral one: the isfinite overflow checks here, max_exponent in
// semantic_similarity's max_representable_step_count, and the unconverted
// division in its ratio_or_throw -- which would truncate every ratio to 0.
static_assert(std::is_floating_point_v<Count>, "Count must be floating-point");

// x87 80-bit long double gives the 2^64-exact mantissa and 2^16384 range the
// model counting is sized for (see max_representable_step_count). Where the
// platform aliases long double to double -- MSVC, 32-bit ARM -- the code still
// runs: the bound clamps itself ~16x shallower off max_exponent and scores lose
// ~3 significant digits, so only atom-rich deep-horizon specs are affected.
// Warn rather than block. #pragma message is a note (not a warning), so it
// survives -Werror on exactly those targets, where #warning would wrongly fail
// the build.
#if LDBL_MANT_DIG < 64 || LDBL_MAX_EXP < 16384
#pragma message(                                                      \
    "peredur: long double is narrower than x87 here; model-counting " \
    "range is ~16x shallower and scores lose ~3 digits. Supported, "  \
    "but atom-rich deep-horizon specs are bound-limited.")
#endif

/// Alias for a dense matrix of Count values, used to represent transition
/// matrices in automata for model counting.
using CountMatrix = Eigen::Matrix<Count, Eigen::Dynamic, Eigen::Dynamic>;

/// Alias for a dense vector of Count values, used to represent valuation
/// counts and trace counts in model counting computations.
using CountVector = Eigen::Matrix<Count, Eigen::Dynamic, 1>;

/// Checks for overflow when adding two Count values and stores the result
/// if no overflow occurs.
/// @param lhs Left-hand side operand
/// @param rhs Right-hand side operand
/// @param result Output parameter to store the sum if no overflow
/// @return true if overflow would occur, false otherwise
inline bool count_add_overflow(Count lhs, Count rhs, Count& result) {
    result = lhs + rhs;
    return !std::isfinite(result);
}

/// Checks for overflow when multiplying two Count values and stores the result
/// if no overflow occurs.
/// @param lhs Left-hand side operand
/// @param rhs Right-hand side operand
/// @param result Output parameter to store the product if no overflow
/// @return true if overflow would occur, false otherwise
inline bool count_mul_overflow(Count lhs, Count rhs, Count& result) {
    result = lhs * rhs;
    return !std::isfinite(result);
}

/// Converts a Count value to a decimal string representation.
/// @param value The Count value to convert
/// @return A string representation in decimal
inline std::string count_to_string(Count value) {
    // A Count near the top of long double's range needs ~4932 digits, so size
    // the buffer from snprintf rather than guessing.
    const int length = std::snprintf(nullptr, 0, "%.0Lf", value);
    assert(length > 0);
    std::string digits(static_cast<std::size_t>(length), '\0');
    std::snprintf(digits.data(), digits.size() + 1, "%.0Lf", value);
    return digits;
}

/// Parses a count value from a decimal string representation. Digits beyond
/// Count's 64-bit mantissa are rounded, not rejected.
/// @param text A string of decimal digits
/// @return The parsed Count value
inline Count parse_count_decimal_or_throw(std::string_view text) {
    assert(!text.empty());
    Count value = 0;
    for (const char character : text) {
        assert(std::isdigit(static_cast<unsigned char>(character)));
        const auto digit = static_cast<Count>(character - '0');
        value = (value * 10) + digit;
    }
    // Above 2^64 the accumulation loses precision; that is acceptable for
    // Ganak model counts used as weights. Saturating to infinity is not.
    assert(std::isfinite(value));
    return value;
}

/// An automaton representation for model counting. Encodes a finite-state
/// automaton that validates a requirement specification, along with valuation
/// counts for the propositional conditions and a transition matrix for
/// computing the number of satisfying traces via matrix exponentiation.
struct TransferSystem {
    /// The states of the automaton. The SPOT + Ganak path leaves these
    /// default-constructed and carrying no information -- it needs only the
    /// state count, which is the vector's size -- so only a
    /// test-constructed system populates them.
    std::vector<State> m_states;
    /// Counts of satisfying valuations for conditions. Always empty on the
    /// SPOT + Ganak path, which folds the valuation weights straight into
    /// m_transition_matrix at construction; only a test-constructed system
    /// supplies them separately.
    CountVector m_valuation_counts;
    /// Transition matrix T where T[i][j] is count from state i to j
    CountMatrix m_transition_matrix;
    /// Whether matrix represents weighted transitions. Always true on the
    /// SPOT + Ganak path; false only for a test-constructed 0/1 adjacency
    /// matrix awaiting m_valuation_counts.
    bool m_transition_matrix_is_weighted = false;
    /// Optional mask of valid final states (1 = valid, 0 = invalid).
    /// Empty means all states are valid final states.
    CountVector m_final_state_mask;
};

/// Returns the number of unique atoms across all four formulas of two
/// requirements. Used to establish a shared atom universe for semantic
/// similarity computations.
std::size_t count_joint_atoms(const Requirement& req1, const Requirement& req2);

/// Constructs a TransferSystem directly from an LTL formula string via
/// ltl2tgba, given the total atom universe size used for transition
/// weighting. Exposed so callers (e.g. semantic similarity) can cache the
/// resulting trace counts on the same (ltl, n_total_atoms) key used here,
/// rather than only on the requirement objects themselves.
TransferSystem build_transfer_system_from_ltl(const std::string& ltl,
                                              std::size_t n_total_atoms);

/// Constructs a TransferSystem for a requirement using SPOT's ltl2tgba to
/// generate a deterministic automaton from the requirement's LTL formula.
/// The canonical_valuation_counts parameter is accepted for API compatibility
/// but is ignored (weights come from SPOT + Ganak directly).
TransferSystem build_transfer_system(
    const Requirement& requirement,
    const CountVector& canonical_valuation_counts = CountVector());

/// Constructs a TransferSystem for a requirement, explicitly specifying the
/// total atom universe size used for transition weighting. Use this overload
/// when comparing trace counts across systems that must share the same universe
/// (e.g. when computing semantic similarity alongside a conjunction system).
TransferSystem build_transfer_system(const Requirement& requirement,
                                     std::size_t n_total_atoms);

/// Constructs a TransferSystem for the conjunction of two requirements.
/// Runs ltl2tgba on "(ltl1) & (ltl2)" and weights transitions via Ganak.
TransferSystem build_conjunction_transfer_system(
    const Requirement& requirement1, const Requirement& requirement2);

/// Returns the weighted transition matrix for a TransferSystem. If
/// m_transition_matrix_is_weighted is already true the matrix is returned
/// unchanged. Otherwise each column j is scaled by m_valuation_counts(j),
/// turning a 0/1 adjacency matrix into a valuation-weighted one.
CountMatrix weighted_transition_matrix(const TransferSystem& system);
