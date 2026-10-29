#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "filter/implication_check.hpp"
#include "fingerprint/lasso.hpp"
#include "fingerprint/prefilter.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/specification.hpp"
#include "tlsf_fixtures.hpp"

namespace {

constexpr std::string_view k_test_suite = "fingerprint_prefilter";

using fingerprint::LassoWord;
using fingerprint::PackedFingerprint;
namespace prefilter = fingerprint::prefilter;

// A budget no model in this suite comes near, so black's cold start on a slow
// runner cannot decide a result.
constexpr std::chrono::milliseconds k_budget{10000};

// "G a".
Requirement g_req(const std::string& atom) {
    return Requirement(Formula("true"), Formula(atom), timing::immediately());
}

// "G F a".
Requirement f_req(const std::string& atom) {
    return Requirement(Formula("true"), Formula(atom), timing::eventually());
}

// The word in the syntax `ltlfilt --accept-word` reads, over every atom the
// word names.
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

bool spot_accepts(const std::string& formula, const LassoWord& word) {
    const ProcessResult result = execute_and_capture(
        {ltlfilt_path(), "-f", formula, "--accept-word=" + spot_word(word)},
        std::chrono::seconds(30));
    if (result.m_timed_out || result.m_exit_code > 1) {
        fail("ltlfilt failed on " + formula + ": " + result.m_output);
    }
    return !result.m_output.empty();
}

TEST(test_parse_reads_loop_and_leaves_undef_unconstrained) {
    const std::optional<LassoModel> model = parse_black_model(
        R"({"result": "SAT", "k": 2, "model": {"size": 3, "loop": 1,
            "states": [{"a": "true", "b": "undef"},
                       {"a": "false", "b": "true"},
                       {"a": "true"}]}})");
    if (!model.has_value()) {
        fail("parse: a SAT model should parse");
    }
    expect(model->m_loop == 1, "parse: the loop index is black's own");
    expect(model->m_states.size() == 3, "parse: one entry per state");
    expect(model->m_states[0].count("b") == 0,
           "parse: undef leaves the atom unconstrained");
    expect(model->m_states[0].at("a") && !model->m_states[1].at("a") &&
               model->m_states[1].at("b"),
           "parse: true and false are read as written");

    // A loop at the model's size is a model with nothing left open, which
    // the last state repeated continues.
    const std::optional<LassoModel> open = parse_black_model(
        R"({"result": "SAT", "model": {"size": 1, "loop": 1,
            "states": [{"b": "true", "a": "undef"}]}})");
    expect(open.has_value() && open->m_loop == 0,
           "parse: a loop at the size repeats the last state");

    expect(!parse_black_model(R"({"result": "UNSAT", "k": 0})").has_value(),
           "parse: UNSAT has no model");
    expect(!parse_black_model("UNKNOWN (stopped at k = 4)").has_value(),
           "parse: non-JSON output has no model");
    expect(!parse_black_model(
                R"({"result": "SAT", "model": {"size": 1, "states": [{}]}})")
                .has_value(),
           "parse: a finite model has no lasso");
    expect(!parse_black_model(
                R"({"result": "SAT", "model": {"size": 1, "loop": 2,
                    "states": [{}]}})")
                .has_value(),
           "parse: a loop past the size is malformed");
}

// The conversion's one choice, undef read as false, is checked against SPOT
// rather than against this codebase's evaluator alone, and the formulae are
// picked so black leaves atoms undef: a prefix X skips, an atom only one
// branch reads, a weak until that has to be rewritten first.
TEST(test_model_words_satisfy_their_formula) {
    const std::vector<std::string> formulae{
        "X(X(X((a) & (b))))",
        "(G((a) -> (X(b)))) & (F(c)) & (G(F(!(a))))",
        "(!(a)) & (X(a)) & (X(X(!(a)))) & (X(X(X(G((!(a)) & (b))))))",
        "((a) W (b)) & (G(!(b)))",
        "(G(a)) | (F(b))",
        "(a) & (G((a) -> (X(!(a))))) & (G((!(a)) -> (X(a))))",
        "true",
    };
    const std::vector<std::string> signals{"a", "b", "c"};
    std::size_t undef_seen = 0;
    for (const std::string& formula : formulae) {
        const std::optional<LassoModel> model = black_model(formula, k_budget);
        if (!model.has_value()) {
            fail("model: black found none for " + formula);
        }
        // An atom of the formula a state does not name is one black left
        // undef there. The signals are single letters `true` does not hold.
        for (const auto& state : model->m_states) {
            for (const std::string& atom : signals) {
                undef_seen += static_cast<std::size_t>(formula.find(atom) !=
                                                           std::string::npos &&
                                                       state.count(atom) == 0);
            }
        }
        const std::optional<LassoWord> word =
            prefilter::word_of_model(*model, signals);
        if (!word.has_value()) {
            fail("model: no word for " + formula);
        }
        expect(fingerprint::fingerprint_of(formula, {*word}).front(),
               "model: the evaluator rejects the model word of " + formula);
        expect(spot_accepts(formula, *word),
               "model: SPOT rejects the model word of " + formula);
    }
    expect(undef_seen > 0,
           "model: no atom was left undef, so the choice of "
           "false was never exercised");
    expect(!black_model("(a) & (!(a))", k_budget).has_value(),
           "model: an unsatisfiable formula has none");
}

// Eight outputs held at every step: a random word keeps all eight on at every
// position with odds of 2^-8 per position, so no sampled word is accepted by
// any of these and the sampled bits refute nothing.
std::vector<Requirement> frame() {
    std::vector<Requirement> reqs;
    reqs.reserve(8);
    for (int idx = 0; idx < 8; ++idx) {
        reqs.push_back(g_req("o" + std::to_string(idx)));
    }
    return reqs;
}

std::vector<std::string> frame_outputs() {
    std::vector<std::string> outs{"p", "q"};
    for (int idx = 0; idx < 8; ++idx) {
        outs.push_back("o" + std::to_string(idx));
    }
    return outs;
}

Specification framed(const std::vector<Requirement>& extra) {
    std::vector<Requirement> reqs = frame();
    reqs.insert(reqs.end(), extra.begin(), extra.end());
    return Specification({}, reqs, {"i"}, frame_outputs());
}

bool sampled_refutes(const PackedFingerprint& lhs,
                     const PackedFingerprint& rhs) {
    const PackedFingerprint lhs_sampled(
        lhs.begin(), lhs.begin() + prefilter::k_sampled_elements);
    const PackedFingerprint rhs_sampled(
        rhs.begin(), rhs.begin() + prefilter::k_sampled_elements);
    return fingerprint::refutes_implication(lhs_sampled, rhs_sampled);
}

// The family carries implications in both directions, G p & G q above G p
// above G F p, and incomparable pairs. Every pair a model word refutes goes
// to SPOT, which must read it as a non-implication; every implication SPOT
// finds must be unrefuted; and the model words must refute pairs the sampled
// words could not, or the check above would pass on an empty tail.
TEST(test_model_words_refute_only_non_implications) {
    const std::vector<Specification> specs{
        framed({g_req("p")}),
        framed({g_req("q")}),
        framed({g_req("p"), g_req("q")}),
        framed({f_req("p")}),
        framed({g_req("!p")}),
        framed({}),
    };
    const std::vector<PackedFingerprint> prints =
        prefilter::fingerprints_of(specs);
    expect(prints.size() == specs.size(), "prefilter: no table was drawn");

    SatisfiabilityChecker checker;
    checker.set_timeout(k_budget);
    checker.set_spot_budget(k_budget);
    std::size_t model_refuted = 0;
    std::size_t implications = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        expect(prints[i].size() > prefilter::k_sampled_elements,
               "prefilter: no model word joined the table");
        for (std::size_t j = 0; j < specs.size(); ++j) {
            if (i == j) {
                continue;
            }
            expect(!sampled_refutes(prints[i], prints[j]),
                   "prefilter: a sampled word was accepted under the frame");
            const bool refuted =
                fingerprint::refutes_implication(prints[i], prints[j]);
            const std::optional<bool> implies =
                spec_implies(specs[i], specs[j], checker);
            if (!implies.has_value()) {
                fail("prefilter: SPOT left a pair open");
            }
            if (refuted) {
                ++model_refuted;
                expect(!*implies,
                       "prefilter: a model word refuted an implication");
                expect(prefilter::refuted_by_model_alone(prints[i], prints[j]),
                       "prefilter: a model refutation was not attributed");
            }
            if (*implies) {
                ++implications;
                expect(!refuted, "prefilter: an implication was refuted");
            }
        }
    }
    expect(implications >= 4, "prefilter: the family lost its implications");
    // Whatever black picks for the atoms a model leaves free, the model of
    // G !p refutes G !p into G p, G p & G q and G F p, and each of theirs
    // refutes the converse.
    expect(model_refuted >= 6, "prefilter: the model words refuted too few");
}

// Model words are appended after the sampled ones, so the sampled bits read
// exactly what the prefilter computed before model words existed.
TEST(test_sampled_bits_are_unchanged) {
    const std::vector<std::string> ins{"a", "b"};
    const std::vector<std::string> outs{"c"};
    const std::vector<Specification> specs{
        Specification({}, {g_req("c")}, ins, outs),
        Specification({}, {f_req("c")}, ins, outs),
        Specification({g_req("a")}, {g_req("c"), f_req("b")}, ins, outs),
    };
    const std::vector<PackedFingerprint> prints =
        prefilter::fingerprints_of(specs);
    std::vector<std::string> signals = ins;
    signals.insert(signals.end(), outs.begin(), outs.end());
    const std::vector<LassoWord> words = fingerprint::sample_words(
        signals, prefilter::k_words, prefilter::k_seed, prefilter::k_max_prefix,
        prefilter::k_max_cycle);
    expect(prints.size() == specs.size(), "prefilter: no table was drawn");
    for (std::size_t idx = 0; idx < specs.size(); ++idx) {
        const PackedFingerprint sampled = fingerprint::pack(
            fingerprint::fingerprint_of(specs[idx].to_ltl(), words));
        expect(sampled.size() == prefilter::k_sampled_elements,
               "prefilter: the sampled words fill whole elements");
        expect(std::equal(sampled.begin(), sampled.end(), prints[idx].begin()),
               "prefilter: model words moved a sampled bit");
        expect(prints[idx].size() == sampled.size() + 1,
               "prefilter: three model words take one element");
    }
}

// The TLSF route, under strict semantics so the ASSERT section lowers to a W
// and the model comes back through the weak-operator rewrite. No ASSUME
// section, whose violation would hand every candidate the same vacuous model.
TEST(test_tlsf_model_words_refute_only_non_implications) {
    std::string frame_body = "  INPUTS { i; }\n  OUTPUTS { p; q;";
    std::string held;
    for (int idx = 0; idx < 8; ++idx) {
        frame_body += " o" + std::to_string(idx) + ";";
        held += "    G o" + std::to_string(idx) + ";\n";
    }
    frame_body += " }\n";
    const auto spec_with = [&](const std::string& extra) {
        return parse_main(frame_body +
                              "  ASSERT { G (i -> X i); }\n  GUARANTEE {\n" +
                              held + extra + "  }\n",
                          "Mealy,Strict");
    };
    const std::vector<tlsf::Specification> specs{
        spec_with("    G p;\n"),
        spec_with("    G q;\n"),
        spec_with("    G p;\n    G q;\n"),
        spec_with("    G !p;\n"),
    };
    expect(specs.front().to_ltl().find('W') != std::string::npos,
           "tlsf prefilter: the strict lowering should carry a weak until");
    const std::vector<PackedFingerprint> prints =
        prefilter::fingerprints_of(specs);
    expect(prints.size() == specs.size(), "tlsf prefilter: no table");

    SatisfiabilityChecker checker;
    checker.set_timeout(k_budget);
    checker.set_spot_budget(k_budget);
    std::size_t refuted = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        for (std::size_t j = 0; j < specs.size(); ++j) {
            if (i == j ||
                !fingerprint::refutes_implication(prints[i], prints[j])) {
                continue;
            }
            ++refuted;
            const std::optional<bool> implies =
                tlsf_spec_implies(specs[i], specs[j], checker);
            expect(implies.has_value() && !*implies,
                   "tlsf prefilter: refuted a pair SPOT reads as implying");
        }
    }
    // G p and G !p against each other, and G p & G q into G !p and back.
    expect(refuted >= 4, "tlsf prefilter: the model words refuted too few");
}

}  // namespace
