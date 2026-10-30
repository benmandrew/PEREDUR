#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

#include "internal.hpp"

namespace prop_formula_internal {

namespace {

std::string unary_operator_symbol(NodeType type) {
    switch (type) {
        case NodeType::Not:
            return "!";
        case NodeType::Next:
            return "X";
        case NodeType::Eventually:
            return "F";
        case NodeType::Globally:
            return "G";
        case NodeType::Atom:
        case NodeType::And:
        case NodeType::Or:
        case NodeType::Implies:
        case NodeType::Iff:
        case NodeType::Until:
        case NodeType::Release:
        case NodeType::WeakUntil:
            break;
    }
    assert(false);
    __builtin_unreachable();
}

std::string binary_operator_symbol(NodeType type) {
    switch (type) {
        case NodeType::And:
            return "&";
        case NodeType::Or:
            return "|";
        case NodeType::Implies:
            return "->";
        case NodeType::Iff:
            return "<->";
        case NodeType::Until:
            return "U";
        case NodeType::Release:
            return "R";
        case NodeType::WeakUntil:
            return "W";
        case NodeType::Atom:
        case NodeType::Not:
        case NodeType::Next:
        case NodeType::Eventually:
        case NodeType::Globally:
            break;
    }
    assert(false);
    __builtin_unreachable();
}

}  // namespace

// Every operand is parenthesised unconditionally, so the rendering carries no
// precedence assumptions that the parser would have to agree with.
std::string node_to_string(const std::vector<Node>& nodes, std::size_t index) {
    const Node& node = nodes[index];
    if (node.m_type == NodeType::Atom) {
        return node.m_variable;
    }
    if (is_unary_node(node.m_type)) {
        const std::string child = node_to_string(nodes, node.m_left);
        return unary_operator_symbol(node.m_type) + "(" + child + ")";
    }
    assert(is_binary_node(node.m_type));
    const std::string left = node_to_string(nodes, node.m_left);
    const std::string right = node_to_string(nodes, node.m_right);
    return "(" + left + ") " + binary_operator_symbol(node.m_type) + " (" +
           right + ")";
}

}  // namespace prop_formula_internal

std::string Formula::to_string() const {
    if (m_impl->m_nodes.empty()) {
        return "";
    }

    return prop_formula_internal::node_to_string(m_impl->m_nodes,
                                                 m_impl->m_nodes.size() - 1);
}
