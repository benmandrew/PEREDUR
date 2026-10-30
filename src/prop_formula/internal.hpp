#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "prop_formula.hpp"

namespace prop_formula_internal {

/// The arena's node type is the public Formula::Kind itself, so a node's type
/// needs no conversion to answer Formula::kind().
using NodeType = Formula::Kind;

/// True for the unary node types (one child in m_left).
inline bool is_unary_node(NodeType type) {
    return type == NodeType::Not || type == NodeType::Next ||
           type == NodeType::Eventually || type == NodeType::Globally;
}

/// True for the binary node types (children in m_left and m_right).
inline bool is_binary_node(NodeType type) {
    return type == NodeType::And || type == NodeType::Or ||
           type == NodeType::Implies || type == NodeType::Iff ||
           type == NodeType::Until || type == NodeType::Release ||
           type == NodeType::WeakUntil;
}

struct Node {
    NodeType m_type = NodeType::Atom;
    std::string m_variable;
    std::size_t m_left = 0;
    std::size_t m_right = 0;

    friend bool operator==(const Node& lhs, const Node& rhs) {
        return lhs.m_type == rhs.m_type && lhs.m_left == rhs.m_left &&
               lhs.m_right == rhs.m_right && lhs.m_variable == rhs.m_variable;
    }

    friend bool operator<(const Node& lhs, const Node& rhs) {
        if (lhs.m_type != rhs.m_type) {
            return lhs.m_type < rhs.m_type;
        }
        if (lhs.m_variable != rhs.m_variable) {
            return lhs.m_variable < rhs.m_variable;
        }
        if (lhs.m_left != rhs.m_left) {
            return lhs.m_left < rhs.m_left;
        }
        return lhs.m_right < rhs.m_right;
    }
};

struct DimacsCnf {
    int m_variable_count = 0;
    std::vector<std::vector<int>> m_clauses;
};

std::vector<Node> parse_formula(const std::string& formula);

/// As parse_formula, reporting a malformed string rather than asserting on it.
/// The asserts hold for every caller that builds its own input; this is for
/// the ones handed a string from outside, where a bad one is a missed cache
/// collapse rather than a defect.
std::optional<std::vector<Node>> try_parse_formula(const std::string& formula);
DimacsCnf encode_dimacs(const std::vector<Node>& nodes);
std::string node_to_string(const std::vector<Node>& nodes, std::size_t index);

/// Builds a new arena wrapping @p child under a unary node of @p type.
std::vector<Node> build_unary_arena(NodeType type,
                                    const std::vector<Node>& child);
/// Builds a new arena joining @p left and @p right under a binary @p type node.
std::vector<Node> build_binary_arena(NodeType type,
                                     const std::vector<Node>& left,
                                     const std::vector<Node>& right);
/// Copies the subtree rooted at @p root_index into a standalone arena.
std::vector<Node> extract_subtree(const std::vector<Node>& nodes,
                                  std::size_t root_index);

}  // namespace prop_formula_internal

struct Formula::Impl {
    std::vector<prop_formula_internal::Node> m_nodes;

    explicit Impl(const std::string& formula);

    /// Constructs directly from an already-built node arena (root last). Used
    /// by the temporal construction/extraction path, which assembles arenas by
    /// hand rather than by parsing a string.
    explicit Impl(std::vector<prop_formula_internal::Node> nodes)
        : m_nodes(std::move(nodes)) {}

    bool operator<(const Impl& rhs) const;
};
