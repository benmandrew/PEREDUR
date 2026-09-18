#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "genetic/monotone.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "runner/black.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/mutation.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_monotone";

// Not a test: registered first so it runs before them. It raises the global
// SAT budget for the oracle queries below, and in the no-argument run the
// setting carries into every later suite, as it always has.
TEST(set_suite_sat_timeout) {
    global_sat_checker().set_timeout(std::chrono::milliseconds(5000));
}

const std::vector<std::string>& atom_pool() {
    static const std::vector<std::string> pool = {"a", "b", "c"};
    return pool;
}

// Whether `from` implies `dest`, asked as the unsatisfiability of
// `from & !dest`. nullopt (a timeout, or an operator black cannot decide) is
// reported separately by the callers rather than folded into a verdict: a
// monotonicity assertion that passes on an unanswered query asserts nothing.
std::optional<bool> implies(const Formula& from, const Formula& dest) {
    SatisfiabilityChecker& checker = global_sat_checker();
    const std::string query =
        "(" + from.to_string() + ") & !(" + dest.to_string() + ")";
    const std::optional<bool> sat =
        checker.check_satisfiability(query, QueryPolarity::ExpectUnsat);
    if (!sat.has_value()) {
        return std::nullopt;
    }
    return !*sat;
}

Formula formula_of(const std::string& text) {
    const tlsf::Specification spec = tlsf::parse(
        "INFO { SEMANTICS: Mealy; }\nMAIN {\nINPUTS { a; b; } "
        "OUTPUTS { c; }\nGUARANTEE { " +
        text + " }\n}\n");
    return spec.m_guarantee.front().m_formula;
}

// Hand-built subjects covering every node kind a rule fires at, plus the two
// polarity flips (a negation, and the antecedent of an implication) and a
// biconditional, whose children are sites for nothing.
std::vector<Formula> subjects() {
    return {
        formula_of("G (a -> c);"),
        formula_of("G ((a & b) -> F c);"),
        formula_of("(a | b) U c;"),
        formula_of("a W (b & c);"),
        formula_of("G (!(a & b) | c);"),
        formula_of("F (a <-> c);"),
        formula_of("G F c;"),
        formula_of("a;"),
        formula_of("a R c;"),
        formula_of("G X c;"),
    };
}

// The whole point of the arm: whichever node and rule are drawn, the result
// must sit on the same side of the implication order every time. A weakening
// that only usually weakens is the general rewriter with extra steps.
// The guard counts per direction, so neither can pass on unanswered queries.
void test_monotone_rewrite_direction_holds(MonotoneDirection direction) {
    const bool weaken = direction == MonotoneDirection::Weaken;
    const char* label =
        weaken ? "monotone: parent implies the weakened rewrite"
               : "monotone: the strengthened rewrite implies its parent";
    std::size_t answered = 0;
    for (const Formula& parent : subjects()) {
        for (std::size_t seed = 0; seed < 12; ++seed) {
            const RandomSource rng = make_random_source_from_seed(seed);
            const Formula child =
                monotone_rewrite(parent, direction, atom_pool(), rng);
            const std::optional<bool> held =
                weaken ? implies(parent, child) : implies(child, parent);
            if (!held.has_value()) {
                continue;
            }
            ++answered;
            expect(*held, label);
        }
    }
    // Guards against the suite passing because every query went unanswered.
    expect(answered > 32,
           "monotone: the implication oracle settled most of the queries");
}

TEST(test_monotone_rewrite_holds_in_both_directions) {
    for (const MonotoneDirection direction :
         {MonotoneDirection::Weaken, MonotoneDirection::Strengthen}) {
        test_monotone_rewrite_direction_holds(direction);
    }
}

// Weakening a biconditional to one of its implications is what puts
// ltl2dba-r-2's sole ideal in reach in a single move, where the temporal
// rewrite reaches it only by regenerating both children as well.
TEST(test_monotone_rewrite_reaches_the_biconditional_weakening) {
    const Formula parent = formula_of("G (c <-> a);");
    const Formula target = formula_of("G (c -> a);");
    bool reached = false;
    for (std::size_t seed = 0; seed < 200 && !reached; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        reached = monotone_rewrite(parent, MonotoneDirection::Weaken,
                                   atom_pool(), rng) == target;
    }
    expect(reached,
           "monotone: `<->` weakens to `->` with both children untouched");
}

// AddOperand is offered at every node, an atom included. Were it offered at a
// conjunction or a disjunction alone, an atom's only monotone move would be
// Constant, and growing a literal into a disjunction -- the shape every
// assumption-shaped ideal in the corpus is built from -- would be reachable
// only where a disjunction already stood.
TEST(test_monotone_rewrite_grows_an_atom) {
    const Formula parent = formula_of("a;");
    bool weakened = false;
    bool strengthened = false;
    for (std::size_t seed = 0; seed < 200; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const Formula child = monotone_rewrite(
            parent, MonotoneDirection::Weaken, atom_pool(), rng);
        if (child.kind() == Formula::Kind::Or) {
            const auto children = child.binary_children();
            weakened = children.has_value() && children->first == parent;
            if (weakened) {
                break;
            }
        }
    }
    for (std::size_t seed = 0; seed < 200; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const Formula child = monotone_rewrite(
            parent, MonotoneDirection::Strengthen, atom_pool(), rng);
        if (child.kind() == Formula::Kind::And) {
            const auto children = child.binary_children();
            strengthened = children.has_value() && children->first == parent;
            if (strengthened) {
                break;
            }
        }
    }
    expect(weakened, "monotone: an atom weakens to `a | l`");
    expect(strengthened, "monotone: an atom strengthens to `a & l`");
}

// AddOperand takes its connective from the direction, not from the node it
// fires at. Reading it off the node was equivalent only while the rule was
// offered at And and Or alone, where the two agree; at any other kind, and at
// an And node being weakened, they disagree.
TEST(test_add_operand_follows_the_direction_not_the_node) {
    const Formula parent = formula_of("(a & b);");
    bool disjoined = false;
    for (std::size_t seed = 0; seed < 200 && !disjoined; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const Formula child = monotone_rewrite(
            parent, MonotoneDirection::Weaken, atom_pool(), rng);
        const auto children = child.binary_children();
        disjoined = child.kind() == Formula::Kind::Or && children.has_value() &&
                    children->first == parent;
    }
    expect(disjoined,
           "monotone: weakening a conjunction adds a disjunct, not a conjunct");
}

// Whether some seed under 200 rewrites `parent` into `target`.
bool reaches(const Formula& parent, MonotoneDirection direction,
             const Formula& target) {
    for (std::size_t seed = 0; seed < 200; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        if (monotone_rewrite(parent, direction, atom_pool(), rng) == target) {
            return true;
        }
    }
    return false;
}

// Release carried no monotone rule at all while its duals U and W each carry
// one, and Next carried none, so both were dead ends for the arm whose job is
// to stay on the implication order. `φ R ψ ≡ G ψ | (ψ U (ψ ∧ φ))` puts ψ at
// time 0 under either disjunct, and `F φ` holds wherever `X φ` does.
TEST(test_extra_rules_reach_the_weakenings) {
    expect(reaches(formula_of("a R c;"), MonotoneDirection::Weaken,
                   formula_of("c;")),
           "monotone: `a R c` weakens to `c`");
    expect(reaches(formula_of("X c;"), MonotoneDirection::Weaken,
                   formula_of("F c;")),
           "monotone: `X c` weakens to `F c`");
}

// The strengthening half. `G ψ` is a disjunct of `φ R ψ`, `G φ` unfolds to
// `φ & X G φ`, and both `a & b` and `!a & !b` entail `a <-> b` -- the last
// being the direction Iff had no rule for, its weakening to one implication
// having been the whole menu.
TEST(test_extra_rules_reach_the_strengthenings) {
    expect(reaches(formula_of("a R c;"), MonotoneDirection::Strengthen,
                   formula_of("G c;")),
           "monotone: `a R c` strengthens to `G c`");
    expect(reaches(formula_of("X c;"), MonotoneDirection::Strengthen,
                   formula_of("G c;")),
           "monotone: `X c` strengthens to `G c`");
    const Formula iff = formula_of("a <-> c;");
    expect(
        reaches(iff, MonotoneDirection::Strengthen, formula_of("a & c;")) &&
            reaches(iff, MonotoneDirection::Strengthen, formula_of("!a & !c;")),
        "monotone: `a <-> c` strengthens to both polarities of the "
        "agreement");
}

// A zero probability must cost no draw, or every campaign archived before the
// arm existed stops reproducing against a current binary. The guard reads the
// key before touching the RandomSource, so the count below is the cost of this
// mutation as it stood before the arm; a change to it means the short circuit
// was lost, or the surrounding grammar moved and the number wants re-pinning.
TEST(test_zero_probability_costs_no_draw) {
    constexpr std::size_t k_expected_draws = 95;
    const tlsf::Specification original = tlsf::parse(
        "INFO { SEMANTICS: Mealy; }\nMAIN {\nINPUTS { a; } "
        "OUTPUTS { c; }\nGUARANTEE { G (a -> F c); }\n}\n");
    Config off;
    off.p_monotone = 0.0;
    // Pinned rather than left to the defaults, the discipline golden_config()
    // follows in the determinism suite: this golden is an absolute draw count,
    // so an operator added later at a non-zero default moves it and the
    // failure reads as a regression in the key actually under test. Each of
    // these is a no-op at the value set here and costs no draw.
    off.tlsf_max_assumption_width = 1;
    off.tlsf_p_bare_assumption = 0.0;
    std::size_t drawn = 0;
    for (std::size_t seed = 0; seed < 8; ++seed) {
        const auto counter = std::make_shared<std::size_t>(0);
        std::mt19937 engine(seed);
        RandomSource source([engine, counter](std::size_t bound) mutable {
            ++*counter;
            return static_cast<std::size_t>(
                bounded_uniform(engine, static_cast<std::uint32_t>(bound)));
        });
        (void)tlsf_mutate(original, source, off);
        drawn += *counter;
    }
    expect(drawn == k_expected_draws,
           "monotone: p_monotone = 0 cost " + std::to_string(drawn) +
               " draws, expected " + std::to_string(k_expected_draws));
}

// The arm is not inert when it is on: some offspring must differ from what the
// same seed produces with it off.
TEST(test_non_zero_probability_changes_offspring) {
    const tlsf::Specification original = tlsf::parse(
        "INFO { SEMANTICS: Mealy; }\nMAIN {\nINPUTS { a; } "
        "OUTPUTS { c; }\nGUARANTEE { G (a -> F c); }\n}\n");
    Config off;
    off.p_monotone = 0.0;
    Config armed_cfg = off;
    armed_cfg.p_monotone = 1.0;
    bool differ = false;
    for (std::size_t seed = 0; seed < 40 && !differ; ++seed) {
        const RandomSource baseline = make_random_source_from_seed(seed);
        const RandomSource armed = make_random_source_from_seed(seed);
        differ = !(tlsf_mutate(original, baseline, off) ==
                   tlsf_mutate(original, armed, armed_cfg));
    }
    expect(differ, "monotone: a non-zero probability does change offspring");
}

}  // namespace
