#include "exhaustive_count.hpp"

#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "prop_formula.hpp"

namespace {

using Word = std::uint64_t;

// Assignments per machine word, and so the atom index at which a column stops
// varying inside a word and starts varying between words.
constexpr std::size_t k_word_bits = 64;
constexpr std::size_t k_word_shift = 6;

// Bit j of an atom's column is the value that atom takes under assignment j,
// assignment j being the number whose bit i is the value of atom i. The column
// is built rather than walked, so the whole truth table is evaluated a word of
// assignments at a time.
std::vector<Word> atom_column(std::size_t index, std::size_t n_words) {
    // Below the word width the pattern repeats inside every word, with period
    // 2^(index+1) bits.
    static constexpr std::array<Word, k_word_shift> k_patterns = {
        0xAAAAAAAAAAAAAAAAULL, 0xCCCCCCCCCCCCCCCCULL, 0xF0F0F0F0F0F0F0F0ULL,
        0xFF00FF00FF00FF00ULL, 0xFFFF0000FFFF0000ULL, 0xFFFFFFFF00000000ULL};
    if (index < k_word_shift) {
        // Braces would select the initializer_list constructor and build a
        // two-element vector.
        // NOLINTNEXTLINE(modernize-return-braced-init-list)
        return std::vector<Word>(n_words, k_patterns[index]);
    }
    // At or above it the bit is constant across a word, so a word is all ones
    // or all zeros according to its own index.
    std::vector<Word> column(n_words, 0);
    for (std::size_t word = 0; word < n_words; ++word) {
        const bool set = ((word >> (index - k_word_shift)) & 1U) != 0;
        column[word] = set ? ~Word{0} : Word{0};
    }
    return column;
}

struct Scan {
    std::map<std::string, std::size_t> m_indices;
    bool m_temporal = false;
};

Scan scan_formula(const Formula& formula) {
    Scan scan;
    (void)formula.rewrite_post_order(
        [&scan](const Formula& sub) -> std::optional<Formula> {
            switch (sub.kind()) {
                case Formula::Kind::Atom: {
                    // The constants parse as ordinary atoms and the model
                    // counter this stands in for treats them as ordinary
                    // variables, so they are counted over here as well.
                    const std::optional<std::string> name = sub.atom_name();
                    assert(name.has_value());
                    const std::size_t next = scan.m_indices.size();
                    scan.m_indices.emplace(*name, next);
                    break;
                }
                case Formula::Kind::Not:
                case Formula::Kind::And:
                case Formula::Kind::Or:
                case Formula::Kind::Implies:
                case Formula::Kind::Iff:
                    break;
                default:
                    scan.m_temporal = true;
                    break;
            }
            return std::nullopt;
        });
    return scan;
}

std::vector<Word> evaluate(const Formula& formula,
                           const std::map<std::string, std::size_t>& indices,
                           std::size_t n_words) {
    if (const std::optional<std::string> atom = formula.atom_name()) {
        const auto found = indices.find(*atom);
        assert(found != indices.end());
        return atom_column(found->second, n_words);
    }
    if (const std::optional<Formula> child = formula.unary_child()) {
        assert(formula.kind() == Formula::Kind::Not);
        std::vector<Word> column = evaluate(*child, indices, n_words);
        for (Word& word : column) {
            word = ~word;
        }
        return column;
    }
    const std::optional<std::pair<Formula, Formula>> operands =
        formula.binary_children();
    // Every kind this is reached with is one of the six scan_formula accepts,
    // so a formula that is neither an atom nor unary is binary.
    if (!operands) {
        assert(false);
        // Braces would select the initializer_list constructor and build a
        // two-element vector.
        // NOLINTNEXTLINE(modernize-return-braced-init-list)
        return std::vector<Word>(n_words, 0);
    }
    std::vector<Word> left = evaluate(operands->first, indices, n_words);
    const std::vector<Word> right =
        evaluate(operands->second, indices, n_words);
    switch (formula.kind()) {
        case Formula::Kind::And:
            for (std::size_t i = 0; i < n_words; ++i) {
                left[i] &= right[i];
            }
            break;
        case Formula::Kind::Or:
            for (std::size_t i = 0; i < n_words; ++i) {
                left[i] |= right[i];
            }
            break;
        case Formula::Kind::Implies:
            for (std::size_t i = 0; i < n_words; ++i) {
                left[i] = ~left[i] | right[i];
            }
            break;
        default:
            assert(formula.kind() == Formula::Kind::Iff);
            for (std::size_t i = 0; i < n_words; ++i) {
                left[i] = ~(left[i] ^ right[i]);
            }
            break;
    }
    return left;
}

}  // namespace

std::optional<Count> count_models_exhaustively(const std::string& formula) {
    const std::optional<Formula> parsed = Formula::try_parse(formula);
    if (!parsed) {
        return std::nullopt;
    }
    const Scan scan = scan_formula(*parsed);
    if (scan.m_temporal) {
        return std::nullopt;
    }
    const std::size_t n_atoms = scan.m_indices.size();
    if (n_atoms > k_exhaustive_count_max_atoms) {
        return std::nullopt;
    }
    const std::size_t n_words =
        n_atoms > k_word_shift ? (std::size_t{1} << (n_atoms - k_word_shift))
                               : 1;
    const std::vector<Word> models = evaluate(*parsed, scan.m_indices, n_words);
    // Below the word width the single word holds 2^n assignments in its low
    // bits and nothing in the rest, which were computed over positions that do
    // not exist. Every operation above is bitwise and so position-independent,
    // which is what lets one mask at the root stand in for masking each of
    // them.
    const Word live = n_atoms < k_word_shift
                          ? (Word{1} << (Word{1} << n_atoms)) - 1
                          : ~Word{0};
    std::uint64_t total = 0;
    for (std::size_t word = 0; word < n_words; ++word) {
        total += std::bitset<k_word_bits>(models[word] & live).count();
    }
    return static_cast<Count>(total);
}
