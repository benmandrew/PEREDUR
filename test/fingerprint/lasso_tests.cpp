#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fingerprint/lasso.hpp"
#include "prop_formula.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "fingerprint_lasso";

using fingerprint::LassoWord;

/// A word over `a` and `b` given as position strings: "10" means a holds at
/// position 0 and not at 1. Positions past `loop_start` repeat forever.
LassoWord word_of(const std::string& a_bits, const std::string& b_bits,
                  std::size_t loop_start) {
    expect(a_bits.size() == b_bits.size(), "test word sides disagree");
    LassoWord word;
    word.m_loop_start = loop_start;
    word.m_n_positions = a_bits.size();
    std::uint64_t a_mask = 0;
    std::uint64_t b_mask = 0;
    for (std::size_t pos = 0; pos < a_bits.size(); ++pos) {
        if (a_bits[pos] == '1') {
            a_mask |= std::uint64_t{1} << pos;
        }
        if (b_bits[pos] == '1') {
            b_mask |= std::uint64_t{1} << pos;
        }
    }
    word.m_signal_masks.emplace("a", a_mask);
    word.m_signal_masks.emplace("b", b_mask);
    return word;
}

bool accepts(const std::string& formula, const LassoWord& word) {
    const std::vector<bool> bits =
        fingerprint::fingerprint_of(Formula(formula), {word});
    return bits.front();
}

/// The same word in the syntax `ltlfilt --accept-word` reads.
std::string spot_word(const LassoWord& word) {
    std::vector<std::string> names;
    names.reserve(word.m_signal_masks.size());
    for (const auto& entry : word.m_signal_masks) {
        names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());
    std::string text;
    for (std::size_t pos = 0; pos < word.m_n_positions; ++pos) {
        if (pos > 0) {
            text += "; ";
        }
        if (pos == word.m_loop_start) {
            text += "cycle{";
        }
        for (std::size_t idx = 0; idx < names.size(); ++idx) {
            const std::uint64_t mask = word.m_signal_masks.at(names[idx]);
            text += (idx == 0 ? "" : " & ");
            text += ((mask >> pos) & std::uint64_t{1}) != 0 ? "" : "!";
            text += names[idx];
        }
    }
    return text + "}";
}

TEST(test_propositional_operators_read_position_zero) {
    const LassoWord word = word_of("10", "01", 1);
    expect(accepts("a", word), "a holds at position 0");
    expect(!accepts("b", word), "b does not hold at position 0");
    expect(accepts("a | b", word), "disjunction holds");
    expect(!accepts("a & b", word), "conjunction does not");
    expect(accepts("!b", word), "negation holds");
    expect(!accepts("a -> b", word),
           "implication fails where a holds and b "
           "does not");
}

TEST(test_next_steps_one_position) {
    const LassoWord word = word_of("10", "01", 1);
    expect(!accepts("X(a)", word), "a does not hold at position 1");
    expect(accepts("X(b)", word), "b holds at position 1");
    // Position 1 is the whole loop, so every further step stays there.
    expect(accepts("X(X(b))", word), "the loop repeats position 1");
}

TEST(test_globally_and_eventually_span_the_loop) {
    // a holds at position 0 only, and the loop is position 1 alone, so `G a`
    // fails while `F a` holds; b is the mirror.
    const LassoWord word = word_of("10", "01", 1);
    expect(!accepts("G(a)", word), "G a fails past the stem");
    expect(accepts("F(a)", word), "F a holds at position 0");
    expect(accepts("G(F(b))", word), "b recurs in the loop");
    expect(!accepts("G(F(a))", word), "a never recurs");
    const LassoWord always_a = word_of("11", "00", 0);
    expect(accepts("G(a)", always_a), "G a holds where a holds everywhere");
}

TEST(test_weak_and_strong_until_differ_on_an_unmet_obligation) {
    // a holds forever and b never does. `a U b` demands b arrive; `a W b`
    // does not, which is the only difference between them.
    const LassoWord word = word_of("11", "00", 0);
    expect(!accepts("(a) U (b)", word), "strong until needs b");
    expect(accepts("(a) W (b)", word), "weak until does not");
    const LassoWord b_arrives = word_of("10", "01", 1);
    expect(accepts("(a) U (b)", b_arrives), "b arrives at position 1");
}

TEST(test_release_holds_until_released) {
    // b holds everywhere, so `a R b` holds whether or not a ever does.
    const LassoWord b_always = word_of("00", "11", 0);
    expect(accepts("(a) R (b)", b_always), "b holding forever releases");
    const LassoWord b_lapses = word_of("00", "10", 1);
    expect(!accepts("(a) R (b)", b_lapses),
           "b lapsing with no a to release it fails");
}

TEST(test_unknown_atom_is_false_everywhere) {
    const LassoWord word = word_of("11", "11", 0);
    expect(!accepts("c", word), "an atom no word names is false");
    expect(accepts("!c", word), "and its negation true");
    expect(accepts("true", word), "the true constant holds");
}

TEST(test_sampling_is_a_function_of_its_arguments) {
    const std::vector<std::string> signals{"r_1", "g_0", "r_0"};
    const std::vector<LassoWord> first =
        fingerprint::sample_words(signals, 32, 7, 2, 3);
    // Reordered, so a word set cannot depend on the declaration order of the
    // signals: the two tools' candidates are scored on one word set and reach
    // it through different readers.
    const std::vector<std::string> shuffled{"g_0", "r_0", "r_1"};
    const std::vector<LassoWord> second =
        fingerprint::sample_words(shuffled, 32, 7, 2, 3);
    expect(first.size() == 32, "the requested word count is what is drawn");
    for (std::size_t idx = 0; idx < first.size(); ++idx) {
        expect(first[idx].m_loop_start == second[idx].m_loop_start &&
                   first[idx].m_n_positions == second[idx].m_n_positions &&
                   first[idx].m_signal_masks == second[idx].m_signal_masks,
               "signal order changed the words drawn");
    }
    const std::vector<LassoWord> other_seed =
        fingerprint::sample_words(signals, 32, 8, 2, 3);
    bool any_difference = false;
    for (std::size_t idx = 0; idx < first.size(); ++idx) {
        any_difference = any_difference || first[idx].m_signal_masks !=
                                               other_seed[idx].m_signal_masks;
    }
    expect(any_difference, "a different seed drew the same words");
}

TEST(test_hex_round_trips_the_bit_order) {
    expect(fingerprint::to_hex({true, false, false, false}) == "1",
           "word 0 is the low bit");
    expect(fingerprint::to_hex({false, false, false, true}) == "8",
           "word 3 is the high bit of the digit");
    expect(fingerprint::to_hex({true, true, true, true, true}) == "1f",
           "a fifth word opens a second digit");
    expect(fingerprint::to_hex({}).empty(), "no words is no digits");
    expect(fingerprint::hamming_distance({true, false}, {false, false}) == 1,
           "one differing position");
}

/// The differential the rest of this suite rests on: every row is checked
/// against SPOT rather than against the table above, because a hand-written
/// expectation and a hand-written evaluator can be wrong in the same way.
TEST(test_agrees_with_ltlfilt) {
    const std::vector<std::string> formulae{
        "a",
        "!a",
        "a & b",
        "a | b",
        "a -> b",
        "a <-> b",
        "X(a)",
        "X(X(b))",
        "G(a)",
        "F(b)",
        "G(F(a))",
        "F(G(b))",
        "G((a) -> (F(b)))",
        "(a) U (b)",
        "(a) W (b)",
        "(a) R (b)",
        "(G(a)) -> (F(b))",
        "X((a) U (b))",
        "G((a) U (b))",
        "((a) U (b)) | (G(!(b)))",
    };
    const std::vector<LassoWord> words =
        fingerprint::sample_words({"a", "b"}, 24, 3, 2, 3);

    std::size_t checked = 0;
    for (const std::string& text : formulae) {
        for (const LassoWord& word : words) {
            const std::string rendered = spot_word(word);
            const ProcessResult result = execute_and_capture(
                {ltlfilt_path(), "-f", text, "--accept-word=" + rendered},
                std::chrono::seconds(30));
            std::string where = text;
            where += " / ";
            where += rendered;
            if (result.m_timed_out || result.m_exit_code > 1) {
                std::string message = "ltlfilt failed on ";
                message += where;
                message += ": ";
                message += result.m_output;
                fail(message);
            }
            const bool spot_accepts = !result.m_output.empty();
            if (accepts(text, word) != spot_accepts) {
                std::string message = "disagreed with ltlfilt on ";
                message += where;
                message += ": ltlfilt says ";
                message += spot_accepts ? "accept" : "reject";
                fail(message);
            }
            ++checked;
        }
    }
    expect(checked == formulae.size() * words.size(),
           "not every row was checked");
}

}  // namespace
