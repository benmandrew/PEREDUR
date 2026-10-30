#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "internal.hpp"

// Formulae are built and taken apart directly on the node arena, never by
// rendering a string and reparsing it. Temporal formulae have no string round
// trip at all, since the propositional parser cannot read temporal syntax back
// — including a propositional operator over a temporal operand, e.g. !(X p).
// Propositional ones do, but the arena built here is byte-identical to the
// parser's layout (verified against the propositional test suite), which
// matters because Formula::hash and operator< compare the arena itself (see
// core.cpp). Everything below preserves that layout; the individual functions
// do not restate it.

namespace prop_formula_internal {

// Appends a unary node of @p type over @p child's arena, producing a new arena
// with the root last. Child indices in the source arena are unchanged (the
// child keeps positions [0, child.size)); the new root points at the old root.
std::vector<Node> build_unary_arena(NodeType type,
                                    const std::vector<Node>& child) {
    assert(!child.empty());
    std::vector<Node> nodes = child;
    const std::size_t child_root = nodes.size() - 1;
    nodes.push_back(Node{type, "", child_root, 0});
    return nodes;
}

// Concatenates @p left and @p right arenas and appends a binary node of
// @p type. The right arena's internal child indices are shifted by the left
// arena's size; leaf (Variable) indices stay 0.
std::vector<Node> build_binary_arena(NodeType type,
                                     const std::vector<Node>& left,
                                     const std::vector<Node>& right) {
    assert(!left.empty() && !right.empty());
    std::vector<Node> nodes = left;
    const std::size_t offset = nodes.size();
    const std::size_t left_root = offset - 1;
    for (Node node : right) {
        if (is_unary_node(node.m_type)) {
            node.m_left += offset;
        } else if (is_binary_node(node.m_type)) {
            node.m_left += offset;
            node.m_right += offset;
        }
        nodes.push_back(node);
    }
    const std::size_t right_root = nodes.size() - 1;
    nodes.push_back(Node{type, "", left_root, right_root});
    return nodes;
}

// Extracts the subtree rooted at @p root_index from @p nodes into a standalone
// arena (root last, post-order), remapping child indices.
std::vector<Node> extract_subtree(const std::vector<Node>& nodes,
                                  std::size_t root_index) {
    std::vector<Node> result;
    std::function<std::size_t(std::size_t)> visit =
        [&](std::size_t old_index) -> std::size_t {
        Node copy = nodes[old_index];
        if (is_unary_node(copy.m_type)) {
            copy.m_left = visit(nodes[old_index].m_left);
        } else if (is_binary_node(copy.m_type)) {
            const std::size_t new_left = visit(nodes[old_index].m_left);
            const std::size_t new_right = visit(nodes[old_index].m_right);
            copy.m_left = new_left;
            copy.m_right = new_right;
        } else {
            copy.m_left = 0;
            copy.m_right = 0;
        }
        result.push_back(copy);
        return result.size() - 1;
    };
    visit(root_index);
    return result;
}

}  // namespace prop_formula_internal

Formula Formula::make_atom(const std::string& atom) { return Formula(atom); }

Formula Formula::from_node_arena(
    std::vector<prop_formula_internal::Node> nodes) {
    Formula result;
    result.m_impl = std::make_shared<Impl>(std::move(nodes));
    return result;
}

Formula Formula::make_unary(Kind kind, const Formula& child) {
    // Built on the arena; see the note at the top of this file.
    assert(kind == Kind::Not || kind == Kind::Next ||
           kind == Kind::Eventually || kind == Kind::Globally);
    return Formula::from_node_arena(
        prop_formula_internal::build_unary_arena(kind, child.m_impl->m_nodes));
}

Formula Formula::make_binary(Kind kind, const Formula& left,
                             const Formula& right) {
    switch (kind) {
        case Kind::And:
        case Kind::Or:
        case Kind::Implies:
        case Kind::Iff:
        case Kind::Until:
        case Kind::Release:
        case Kind::WeakUntil:
            return Formula::from_node_arena(
                prop_formula_internal::build_binary_arena(
                    kind, left.m_impl->m_nodes, right.m_impl->m_nodes));
        case Kind::Atom:
        case Kind::Not:
        case Kind::Next:
        case Kind::Eventually:
        case Kind::Globally:
            assert(false);
            __builtin_unreachable();
    }
    __builtin_unreachable();
}

Formula::Kind Formula::kind() const {
    assert(!m_impl->m_nodes.empty());
    return m_impl->m_nodes.back().m_type;
}

std::optional<std::string> Formula::atom_name() const {
    const prop_formula_internal::Node& root = m_impl->m_nodes.back();
    if (root.m_type != prop_formula_internal::NodeType::Atom) {
        return std::nullopt;
    }
    return root.m_variable;
}

std::optional<Formula> Formula::unary_child() const {
    // Extracted from the arena; see the note at the top of this file.
    const prop_formula_internal::Node& root = m_impl->m_nodes.back();
    if (!prop_formula_internal::is_unary_node(root.m_type)) {
        return std::nullopt;
    }
    return from_node_arena(
        prop_formula_internal::extract_subtree(m_impl->m_nodes, root.m_left));
}

std::optional<std::pair<Formula, Formula>> Formula::binary_children() const {
    const prop_formula_internal::Node& root = m_impl->m_nodes.back();
    if (!prop_formula_internal::is_binary_node(root.m_type)) {
        return std::nullopt;
    }
    return std::make_pair(
        from_node_arena(prop_formula_internal::extract_subtree(m_impl->m_nodes,
                                                               root.m_left)),
        from_node_arena(prop_formula_internal::extract_subtree(m_impl->m_nodes,
                                                               root.m_right)));
}
