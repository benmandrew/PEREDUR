#include <optional>
#include <string>

#include "prop_formula.hpp"

namespace {

bool is_true_formula(const Formula& fml) {
    return fml.kind() == Formula::Kind::Atom && fml.atom_name() == "true";
}

bool is_false_formula(const Formula& fml) {
    return fml.kind() == Formula::Kind::Atom && fml.atom_name() == "false";
}

// True when one of @p lhs / @p rhs is the syntactic negation of the other,
// i.e. they form a complementary pair phi / !phi.
bool is_negation_of(const Formula& lhs, const Formula& rhs) {
    if (lhs.kind() == Formula::Kind::Not) {
        const auto child = lhs.unary_child();
        if (child && *child == rhs) {
            return true;
        }
    }
    if (rhs.kind() == Formula::Kind::Not) {
        const auto child = rhs.unary_child();
        if (child && *child == lhs) {
            return true;
        }
    }
    return false;
}

// Negates @p fml, collapsing !!A -> A and folding the boolean constants, so
// that simplification never introduces a fresh double negation that a single
// post-order pass would leave behind.
Formula negate(const Formula& fml) {
    if (fml.kind() == Formula::Kind::Not) {
        if (const auto child = fml.unary_child()) {
            return *child;
        }
    }
    if (is_true_formula(fml)) {
        return Formula("false");
    }
    if (is_false_formula(fml)) {
        return Formula{};
    }
    return Formula::make_unary(Formula::Kind::Not, fml);
}

std::optional<Formula> simplify_not(const Formula& node) {
    const auto child = node.unary_child();
    if (!child) {
        return std::nullopt;
    }
    if (child->kind() == Formula::Kind::Not) {
        return child->unary_child();
    }
    if (is_true_formula(*child)) {
        return Formula("false");
    }
    if (is_false_formula(*child)) {
        return Formula{};
    }
    return std::nullopt;
}

std::optional<Formula> simplify_and(const Formula& lhs, const Formula& rhs) {
    if (lhs == rhs) {
        return lhs;
    }
    if (is_true_formula(lhs)) {
        return rhs;
    }
    if (is_true_formula(rhs)) {
        return lhs;
    }
    // Annihilation with false and contradiction A & !A both collapse to false.
    if (is_false_formula(lhs) || is_false_formula(rhs) ||
        is_negation_of(lhs, rhs)) {
        return Formula("false");
    }
    return std::nullopt;
}

std::optional<Formula> simplify_or(const Formula& lhs, const Formula& rhs) {
    if (lhs == rhs) {
        return lhs;
    }
    if (is_true_formula(lhs) || is_true_formula(rhs)) {
        return Formula{};
    }
    // Excluded middle A | !A collapses to true.
    if (is_negation_of(lhs, rhs)) {
        return Formula{};
    }
    // Identity with false: A | false -> A, false | A -> A.
    if (is_false_formula(rhs)) {
        return lhs;
    }
    if (is_false_formula(lhs)) {
        return rhs;
    }
    return std::nullopt;
}

std::optional<Formula> simplify_implies(const Formula& lhs,
                                        const Formula& rhs) {
    if (lhs == rhs) {
        return Formula{};
    }
    if (is_true_formula(rhs)) {
        return Formula{};
    }
    if (is_true_formula(lhs)) {
        return rhs;
    }
    // false -> A is vacuously true; A -> false is !A.
    if (is_false_formula(lhs)) {
        return Formula{};
    }
    if (is_false_formula(rhs)) {
        return negate(lhs);
    }
    return std::nullopt;
}

std::optional<Formula> simplify_iff(const Formula& lhs, const Formula& rhs) {
    if (lhs == rhs) {
        return Formula{};
    }
    if (is_true_formula(lhs)) {
        return rhs;
    }
    if (is_true_formula(rhs)) {
        return lhs;
    }
    // A <-> false is !A; complementary operands A <-> !A are contradictory.
    if (is_false_formula(lhs)) {
        return negate(rhs);
    }
    if (is_false_formula(rhs)) {
        return negate(lhs);
    }
    if (is_negation_of(lhs, rhs)) {
        return Formula("false");
    }
    return std::nullopt;
}

// G G φ ≡ G φ and F F φ ≡ F φ. Nothing else folded these, so every one the
// operators built survived into the written repair: the 2026-08-14-aurus-h2h
// corpus carries 102 `G(G(` nestings and 99 `F(F(` against zero in any of the
// 25 inputs. They cost size in every n_subformulae-based score, budget against
// the bloat cap, and states in the automata ltl2tgba and Ganak build.
std::optional<Formula> simplify_idempotent_unary(const Formula& node) {
    auto child = node.unary_child();
    if (!child || child->kind() != node.kind()) {
        return std::nullopt;
    }
    return child;
}

// φ U φ, φ W φ and φ R φ are all equivalent to φ: each says φ holds now, or
// holds until itself, which is the same demand. The propositional folder
// already collapsed the ∧ and ∨ forms of a self-join, which is why the corpus
// holds 33 W and 23 U self-joins and no ∧ or ∨ ones.
std::optional<Formula> simplify_self_join(const Formula& lhs,
                                          const Formula& rhs) {
    if (lhs == rhs) {
        return lhs;
    }
    return std::nullopt;
}

// The boolean constants fold through every temporal operator, and nothing here
// did that: simplify() folded them propositionally only, so the `Constant`
// monotone rewrite's own output left `G(false)`, `X(true)` and
// `(true) W (false)` standing as distinct spellings of one constant. Measured
// over 14 specifications, 30.4% of the formulae reaching ltlfilt and
// 43.7% of those reaching ltlsynt mention a constant, and each distinct
// spelling buys its own exec of both. This is not a wall-time change -- a
// paired A/B over 33 cases read null, the fold moving the search on the TLSF
// path faster than it saves anything -- so the case for it is that a guarantee
// which folds to `true` is a gutted guarantee whether or not anything notices.
std::optional<Formula> simplify_temporal_unary(const Formula& node) {
    const auto child = node.unary_child();
    if (!child) {
        return std::nullopt;
    }
    // X, F and G all preserve both constants: a constant holds at every
    // timepoint or at none.
    const Formula& operand = *child;
    if (is_true_formula(operand) || is_false_formula(operand)) {
        return operand;
    }
    return std::nullopt;
}

// phi U psi, phi W psi and phi R psi with a constant operand. Each identity is
// read off the fixpoint expansion; W and R are the two that do not simply
// annihilate, since phi W false == G phi and false R psi == G psi.
std::optional<Formula> simplify_temporal_binary(Formula::Kind kind,
                                                const Formula& lhs,
                                                const Formula& rhs) {
    const bool lhs_true = is_true_formula(lhs);
    const bool lhs_false = is_false_formula(lhs);
    const bool rhs_true = is_true_formula(rhs);
    const bool rhs_false = is_false_formula(rhs);
    switch (kind) {
        case Formula::Kind::Until:
            // phi U true == true; phi U false == false; false U psi == psi;
            // true U psi == F psi.
            if (rhs_true || rhs_false || lhs_false) {
                return rhs;
            }
            if (lhs_true) {
                return Formula::make_unary(Formula::Kind::Eventually, rhs);
            }
            return std::nullopt;
        case Formula::Kind::WeakUntil:
            // phi W true == true; true W psi == true; false W psi == psi;
            // phi W false == G phi.
            if (rhs_true) {
                return rhs;
            }
            if (lhs_true) {
                return lhs;
            }
            if (lhs_false) {
                return rhs;
            }
            if (rhs_false) {
                return Formula::make_unary(Formula::Kind::Globally, lhs);
            }
            return std::nullopt;
        case Formula::Kind::Release:
            // phi R true == true; phi R false == false; true R psi == psi;
            // false R psi == G psi.
            if (rhs_true || rhs_false || lhs_true) {
                return rhs;
            }
            if (lhs_false) {
                return Formula::make_unary(Formula::Kind::Globally, rhs);
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

std::optional<Formula> simplify_node(const Formula& node) {
    if (node.kind() == Formula::Kind::Not) {
        return simplify_not(node);
    }
    if (node.kind() == Formula::Kind::Globally ||
        node.kind() == Formula::Kind::Eventually ||
        node.kind() == Formula::Kind::Next) {
        if (const auto folded = simplify_temporal_unary(node)) {
            return folded;
        }
        if (node.kind() == Formula::Kind::Next) {
            return std::nullopt;
        }
        return simplify_idempotent_unary(node);
    }
    const auto children = node.binary_children();
    if (!children) {
        return std::nullopt;
    }
    const Formula& lhs = children->first;
    const Formula& rhs = children->second;
    switch (node.kind()) {
        case Formula::Kind::And:
            return simplify_and(lhs, rhs);
        case Formula::Kind::Or:
            return simplify_or(lhs, rhs);
        case Formula::Kind::Implies:
            return simplify_implies(lhs, rhs);
        case Formula::Kind::Iff:
            return simplify_iff(lhs, rhs);
        case Formula::Kind::Until:
        case Formula::Kind::Release:
        case Formula::Kind::WeakUntil:
            if (const auto folded =
                    simplify_temporal_binary(node.kind(), lhs, rhs)) {
                return folded;
            }
            return simplify_self_join(lhs, rhs);
        case Formula::Kind::Atom:
        case Formula::Kind::Not:
        // Handled above, before the binary-children guard rejects them. X
        // has no idempotence to exploit -- X X φ is a genuinely different
        // formula from X φ -- but it does fold a constant operand.
        case Formula::Kind::Next:
        case Formula::Kind::Eventually:
        case Formula::Kind::Globally:
            break;
    }
    return std::nullopt;
}

std::optional<Formula> strip_double_negation(const Formula& node) {
    if (node.kind() != Formula::Kind::Not) {
        return std::nullopt;
    }
    const auto child = node.unary_child();
    if (!child || child->kind() != Formula::Kind::Not) {
        return std::nullopt;
    }
    return child->unary_child();
}

}  // namespace

void Formula::remove_double_negation() {
    *this = this->rewrite_post_order(strip_double_negation);
}

void Formula::simplify() { *this = this->rewrite_post_order(simplify_node); }
