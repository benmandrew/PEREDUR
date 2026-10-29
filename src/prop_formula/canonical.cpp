#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "prop_formula.hpp"

namespace {

// And, Or and Iff are all commutative, so their operand order carries no
// meaning and every ordering of one formula is a separate cache key. Implies
// and the three temporal binaries are not, and are left alone.
bool is_commutative(Formula::Kind kind) {
    return kind == Formula::Kind::And || kind == Formula::Kind::Or ||
           kind == Formula::Kind::Iff;
}

// Collects the operands of a maximal chain of @p kind, so that `a & (b & c)`
// and `(a & b) & c` yield the same three operands. Only And and Or are
// flattened; see canonical_node for why Iff is not.
void flatten(const Formula& formula, Formula::Kind kind,
             std::vector<Formula>& out) {
    if (formula.kind() == kind) {
        if (const auto children = formula.binary_children()) {
            flatten(children->first, kind, out);
            flatten(children->second, kind, out);
            return;
        }
    }
    out.push_back(formula);
}

// Rebuilds a left-leaning chain, matching how the parser associates, so that
// parse(render(canonical(f))) is canonical(f) rather than a re-association of
// it.
Formula chain(Formula::Kind kind, const std::vector<Formula>& operands) {
    Formula result = operands.front();
    for (std::size_t i = 1; i < operands.size(); ++i) {
        result = Formula::make_binary(kind, result, operands[i]);
    }
    return result;
}

}  // namespace

Formula Formula::canonical() const {
    if (kind() == Kind::Atom) {
        return *this;
    }
    if (const auto child = unary_child()) {
        const Formula inner = child->canonical();
        // !!phi -> phi. The other three unary kinds have no such fold: X X phi
        // is a genuinely different formula, and G G phi / F F phi belong to
        // simplify(), which is a rewriter rather than a normal form.
        if (kind() == Kind::Not && inner.kind() == Kind::Not) {
            if (const auto grandchild = inner.unary_child()) {
                return *grandchild;
            }
        }
        return make_unary(kind(), inner);
    }
    const auto children = binary_children();
    // Every kind is an atom, a unary or a binary, and the two branches above
    // took the first two, so this is engaged. Checked rather than asserted so
    // that a kind added without a branch here degrades to a no-op key rather
    // than to undefined behaviour.
    if (!children) {
        return *this;
    }
    if (!is_commutative(kind())) {
        return make_binary(kind(), children->first.canonical(),
                           children->second.canonical());
    }
    // Iff is commutative and associative, but not idempotent -- `a <-> a` is
    // the constant true rather than `a` -- so its operands are ordered and
    // neither flattened nor deduplicated. Deduplicating there would change
    // what the formula says.
    if (kind() == Kind::Iff) {
        const Formula left = children->first.canonical();
        const Formula right = children->second.canonical();
        const bool ordered = left < right;
        const Formula& first = ordered ? left : right;
        const Formula& second = ordered ? right : left;
        return make_binary(kind(), first, second);
    }
    // The whole chain at once. Canonicalising each side first would flatten,
    // sort and rebuild every operand below at each level of a left-leaning
    // chain, n^2 operand copies for n conjuncts. A canonical operand is
    // flattened again, since `!!(a & b)` comes back as an And. Sorting and
    // deduplicating a union does not depend on how it was grouped, so the
    // result is the one the per-level rebuild gave.
    std::vector<Formula> chain_operands;
    flatten(*this, kind(), chain_operands);
    std::vector<Formula> operands;
    for (const Formula& operand : chain_operands) {
        flatten(operand.canonical(), kind(), operands);
    }
    std::sort(operands.begin(), operands.end());
    operands.erase(std::unique(operands.begin(), operands.end()),
                   operands.end());
    return chain(kind(), operands);
}
