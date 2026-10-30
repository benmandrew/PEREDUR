#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "internal.hpp"

namespace {

using prop_formula_internal::is_binary_node;
using prop_formula_internal::is_unary_node;
using prop_formula_internal::Node;
using prop_formula_internal::NodeType;

// Each rewritten child is sequenced into a local of its own rather than passed
// straight into make_unary/make_binary: a rewrite callback may draw from the
// RandomSource, argument evaluation order is unspecified, and gcc and clang
// pick opposite orders — so inlining the calls would stop a seed reproducing
// across compilers (8d1f9bd).
Formula rewrite_subtree(const std::vector<Node>& nodes, std::size_t index,
                        const Formula::RewriteCallback& rewrite_callback) {
    const Node& node = nodes[index];
    const Formula::Kind kind = node.m_type;

    Formula rewritten_subtree;
    if (node.m_type == NodeType::Atom) {
        rewritten_subtree = Formula::make_atom(node.m_variable);
    } else if (is_unary_node(node.m_type)) {
        const Formula child =
            rewrite_subtree(nodes, node.m_left, rewrite_callback);
        rewritten_subtree = Formula::make_unary(kind, child);
    } else {
        assert(is_binary_node(node.m_type));
        const Formula left =
            rewrite_subtree(nodes, node.m_left, rewrite_callback);
        const Formula right =
            rewrite_subtree(nodes, node.m_right, rewrite_callback);
        rewritten_subtree = Formula::make_binary(kind, left, right);
    }

    if (const std::optional<Formula> replacement =
            rewrite_callback(rewritten_subtree);
        replacement.has_value()) {
        return *replacement;
    }
    return rewritten_subtree;
}

}  // namespace

Formula Formula::rewrite_post_order(
    const RewriteCallback& rewrite_callback) const {
    if (!rewrite_callback) {
        return *this;
    }

    return rewrite_subtree(m_impl->m_nodes, m_impl->m_nodes.size() - 1,
                           rewrite_callback);
}
