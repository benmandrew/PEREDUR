#include "lasso.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "prop_formula/internal.hpp"

namespace fingerprint {

namespace {

using prop_formula_internal::Node;
using prop_formula_internal::NodeType;

/// The all-positions mask of a word.
std::uint64_t full_mask(std::size_t n_positions) {
    assert(n_positions > 0 && n_positions <= k_max_positions);
    return n_positions == k_max_positions
               ? ~std::uint64_t{0}
               : (std::uint64_t{1} << n_positions) - 1;
}

/// Bit i of the result is bit succ(i) of @p mask, where succ is the word's
/// successor: i + 1 everywhere but the last position, which loops back.
std::uint64_t shift_to_successor(std::uint64_t mask, std::size_t n_positions,
                                 std::size_t loop_start) {
    const std::size_t last = n_positions - 1;
    std::uint64_t shifted = (mask >> 1) & (full_mask(n_positions) >> 1);
    if (((mask >> loop_start) & std::uint64_t{1}) != 0) {
        shifted |= std::uint64_t{1} << last;
    }
    return shifted;
}

/// Iterates `step` from @p seed_value to its fixed point.
///
/// Both fixed points converge within one round per position: each iteration
/// resolves the obligation one step further along the lasso, and a lasso has
/// `m_n_positions` distinct steps before it repeats. The loop is written as a
/// fixed bound with an equality test rather than a bare `while (changed)` so
/// a rule that failed to be monotone could not spin.
template <typename Step>
std::uint64_t fixed_point(std::uint64_t seed_value, std::size_t n_positions,
                          Step step) {
    std::uint64_t current = seed_value;
    for (std::size_t round = 0; round <= n_positions; ++round) {
        const std::uint64_t next = step(current);
        if (next == current) {
            return current;
        }
        current = next;
    }
    return current;
}

std::uint64_t atom_mask(const std::string& name, const LassoWord& word) {
    if (name == "true") {
        return full_mask(word.m_n_positions);
    }
    const auto found = word.m_signal_masks.find(name);
    return found == word.m_signal_masks.end() ? std::uint64_t{0}
                                              : found->second;
}

/// The valuation of every node of @p nodes over @p word, indexed as the arena
/// is. Children sit below their parent in the arena, so one forward pass
/// suffices.
std::vector<std::uint64_t> evaluate_arena(const std::vector<Node>& nodes,
                                          const LassoWord& word) {
    const std::size_t positions = word.m_n_positions;
    const std::uint64_t all = full_mask(positions);
    const auto next = [&](std::uint64_t mask) {
        return shift_to_successor(mask, positions, word.m_loop_start);
    };

    std::vector<std::uint64_t> value(nodes.size(), 0);
    for (std::size_t idx = 0; idx < nodes.size(); ++idx) {
        const Node& node = nodes[idx];
        assert(!prop_formula_internal::is_unary_node(node.m_type) ||
               node.m_left < idx);
        assert(!prop_formula_internal::is_binary_node(node.m_type) ||
               (node.m_left < idx && node.m_right < idx));
        const std::uint64_t left =
            node.m_type == NodeType::Atom ? 0 : value[node.m_left];
        const std::uint64_t right =
            prop_formula_internal::is_binary_node(node.m_type)
                ? value[node.m_right]
                : 0;
        switch (node.m_type) {
            case NodeType::Atom:
                value[idx] = atom_mask(node.m_variable, word);
                break;
            case NodeType::Not:
                value[idx] = ~left & all;
                break;
            case NodeType::And:
                value[idx] = left & right;
                break;
            case NodeType::Or:
                value[idx] = left | right;
                break;
            case NodeType::Implies:
                value[idx] = (~left & all) | right;
                break;
            case NodeType::Iff:
                value[idx] = ~(left ^ right) & all;
                break;
            case NodeType::Next:
                value[idx] = next(left);
                break;
            // F phi = phi | X F phi, a least fixed point, so it starts empty
            // and grows; G phi = phi & X G phi is the greatest and starts
            // full. The pair of weak operators below differ from their strong
            // twins in exactly that seed.
            case NodeType::Eventually:
                value[idx] = fixed_point(0, positions, [&](std::uint64_t cur) {
                    return left | next(cur);
                });
                break;
            case NodeType::Globally:
                value[idx] = fixed_point(
                    all, positions,
                    [&](std::uint64_t cur) { return left & next(cur); });
                break;
            case NodeType::Until:
                value[idx] = fixed_point(0, positions, [&](std::uint64_t cur) {
                    return right | (left & next(cur));
                });
                break;
            case NodeType::WeakUntil:
                value[idx] =
                    fixed_point(all, positions, [&](std::uint64_t cur) {
                        return right | (left & next(cur));
                    });
                break;
            case NodeType::Release:
                value[idx] =
                    fixed_point(all, positions, [&](std::uint64_t cur) {
                        return right & (left | next(cur));
                    });
                break;
        }
    }
    return value;
}

}  // namespace

std::vector<LassoWord> sample_words(const std::vector<std::string>& signals,
                                    std::size_t n_words, std::uint64_t seed,
                                    std::size_t max_prefix,
                                    std::size_t max_cycle) {
    assert(max_cycle >= 1);
    assert(max_prefix + max_cycle <= k_max_positions);
    std::vector<std::string> ordered = signals;
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

    // The signal names go into the seed, so two families sharing a seed and a
    // signal count still draw different words, and one family draws the same
    // words whichever tool's candidates are being scored.
    std::seed_seq::result_type mixed = 0;
    std::vector<std::seed_seq::result_type> entropy{
        static_cast<std::seed_seq::result_type>(seed),
        static_cast<std::seed_seq::result_type>(seed >> 32)};
    for (const std::string& name : ordered) {
        for (const char letter : name) {
            mixed = (mixed * 31) + static_cast<unsigned char>(letter);
        }
        entropy.push_back(mixed);
    }
    std::seed_seq sequence(entropy.begin(), entropy.end());
    std::mt19937_64 rng(sequence);

    std::uniform_int_distribution<std::size_t> prefix_dist(0, max_prefix);
    std::uniform_int_distribution<std::size_t> cycle_dist(1, max_cycle);
    std::bernoulli_distribution coin(0.5);

    std::vector<LassoWord> words;
    words.reserve(n_words);
    for (std::size_t index = 0; index < n_words; ++index) {
        LassoWord word;
        word.m_loop_start = prefix_dist(rng);
        word.m_n_positions = word.m_loop_start + cycle_dist(rng);
        for (const std::string& name : ordered) {
            std::uint64_t mask = 0;
            for (std::size_t pos = 0; pos < word.m_n_positions; ++pos) {
                if (coin(rng)) {
                    mask |= std::uint64_t{1} << pos;
                }
            }
            word.m_signal_masks.emplace(name, mask);
        }
        words.push_back(std::move(word));
    }
    return words;
}

std::vector<bool> fingerprint_of(const Formula& formula,
                                 const std::vector<LassoWord>& words) {
    // The round trip through the rendered string is how a Formula's arena is
    // reached from outside the class, and is the same one formula_key's
    // renaming already relies on.
    return fingerprint_of(formula.to_string(), words);
}

std::vector<bool> fingerprint_of(const std::string& ltl,
                                 const std::vector<LassoWord>& words) {
    // Parsed once rather than once per word: the arena is what evaluation
    // walks, and a fingerprint is hundreds of words over one formula.
    const std::optional<std::vector<Node>> nodes =
        prop_formula_internal::try_parse_formula(ltl);
    if (!nodes.has_value()) {
        throw std::invalid_argument("cannot parse formula: " + ltl);
    }
    std::vector<bool> bits;
    bits.reserve(words.size());
    for (const LassoWord& word : words) {
        const std::vector<std::uint64_t> value = evaluate_arena(*nodes, word);
        bits.push_back((value.back() & std::uint64_t{1}) != 0);
    }
    return bits;
}

PackedFingerprint pack(const std::vector<bool>& bits) {
    PackedFingerprint packed((bits.size() + 63) / 64, 0);
    for (std::size_t index = 0; index < bits.size(); ++index) {
        if (bits[index]) {
            packed[index / 64] |= std::uint64_t{1} << (index % 64);
        }
    }
    return packed;
}

bool refutes_implication(const PackedFingerprint& lhs,
                         const PackedFingerprint& rhs) {
    assert(lhs.size() == rhs.size());
    for (std::size_t word = 0; word < lhs.size(); ++word) {
        if ((lhs[word] & ~rhs[word]) != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace fingerprint
