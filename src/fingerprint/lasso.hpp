#pragma once

/// @file lasso.hpp
/// @brief LTL evaluation over ultimately periodic words, for behavioural
///        fingerprinting of repairs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "prop_formula.hpp"

namespace fingerprint {

/// The largest lasso a word may span. Every position of a word is one bit of
/// a `std::uint64_t` valuation mask, so the two together must fit in 64.
constexpr std::size_t k_max_positions = 64;

/// An ultimately periodic word: `m_n_positions` states, the last of which
/// loops back to `m_loop_start`. Each signal carries the mask of positions it
/// holds at rather than each position carrying a set of signals, because
/// evaluation walks the syntax tree once and folds whole masks, where the
/// row-wise shape would walk it once per position.
struct LassoWord {
    std::size_t m_loop_start = 0;
    std::size_t m_n_positions = 1;
    std::unordered_map<std::string, std::uint64_t> m_signal_masks;
};

/// The words one family is fingerprinted on.
///
/// Derived from @p signals, @p n_words and @p seed alone, and from the
/// signals in sorted order, so two tools' candidates for one family are
/// scored on the same words without a word file travelling between the hosts
/// that scored them. A fingerprint is only comparable against another drawn
/// from an identical call.
std::vector<LassoWord> sample_words(const std::vector<std::string>& signals,
                                    std::size_t n_words, std::uint64_t seed,
                                    std::size_t max_prefix,
                                    std::size_t max_cycle);

/// One bit per word, set where @p formula holds at position 0 of that word.
///
/// An atom no word names is false at every position. That covers the `false`
/// constant and a signal the specification declares without a word carrying
/// it; `true` is recognised by name, as it is everywhere else in the codebase
/// (is_constant_atom in src/requirement.cpp).
///
/// @throws std::invalid_argument if @p formula does not render to a string
///         this codebase's own parser reads back.
std::vector<bool> fingerprint_of(const Formula& formula,
                                 const std::vector<LassoWord>& words);

/// The same, over an LTL string in SPOT syntax. The FRETISH lowering is
/// assembled as a string by `requirement_to_ltl` rather than held as a
/// `Formula`, so that path has nothing to render and reaches evaluation here.
///
/// @throws std::invalid_argument if @p ltl is not a string this codebase's own
///         parser reads back.
std::vector<bool> fingerprint_of(const std::string& ltl,
                                 const std::vector<LassoWord>& words);

/// A fingerprint packed for the pairwise test, 64 words to an element.
using PackedFingerprint = std::vector<std::uint64_t>;

/// @p bits packed low word first.
PackedFingerprint pack(const std::vector<bool>& bits);

/// True when @p lhs accepts a word @p rhs rejects.
///
/// That word is a witness for `lhs & !rhs`, so it refutes `lhs -> rhs` with no
/// solver call. The test is necessary and not sufficient: a false result means
/// no sampled word separated the two, which is evidence of implication and not
/// proof of it, so the caller must still ask the solver. Refutation is exact
/// either way -- every bit comes from evaluating the lowering itself.
bool refutes_implication(const PackedFingerprint& lhs,
                         const PackedFingerprint& rhs);

}  // namespace fingerprint
