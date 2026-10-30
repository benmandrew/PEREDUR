#include <cassert>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal.hpp"

namespace {

class TseitinEncoder {
   private:
    const std::vector<prop_formula_internal::Node>& m_nodes;
    int m_next_variable_id = 1;
    std::unordered_map<std::string, int> m_symbol_to_variable;
    std::unordered_map<std::size_t, int> m_node_literal_cache;
    std::vector<std::vector<int>> m_clauses;

   public:
    explicit TseitinEncoder(
        const std::vector<prop_formula_internal::Node>& nodes)
        : m_nodes(nodes) {}

    prop_formula_internal::DimacsCnf encode() {
        assert(!m_nodes.empty());
        const int root_literal =
            encode_node(static_cast<std::size_t>(m_nodes.size() - 1));
        m_clauses.push_back({root_literal});
        return prop_formula_internal::DimacsCnf{m_next_variable_id - 1,
                                                m_clauses};
    }

   private:
    // Every operator emits the full bi-implication, not the single direction
    // plain SAT would need: the output feeds Ganak, and only a definitional
    // encoding leaves each auxiliary variable functionally determined, so that
    // the CNF has the same model count as the original formula.
    int encode_node(std::size_t index) {
        auto cache_it = m_node_literal_cache.find(index);
        if (cache_it != m_node_literal_cache.end()) {
            return cache_it->second;
        }
        const prop_formula_internal::Node& node = m_nodes[index];
        int literal = 0;
        switch (node.m_type) {
            case prop_formula_internal::NodeType::Atom: {
                literal = get_or_create_symbol(node.m_variable);
                break;
            }
            case prop_formula_internal::NodeType::Not: {
                const int child = encode_node(node.m_left);
                literal = new_auxiliary();
                add_clause({-literal, -child});
                add_clause({literal, child});
                break;
            }
            case prop_formula_internal::NodeType::And: {
                const int left = encode_node(node.m_left);
                const int right = encode_node(node.m_right);
                literal = new_auxiliary();
                add_clause({-literal, left});
                add_clause({-literal, right});
                add_clause({literal, -left, -right});
                break;
            }
            case prop_formula_internal::NodeType::Or: {
                const int left = encode_node(node.m_left);
                const int right = encode_node(node.m_right);
                literal = new_auxiliary();
                add_clause({literal, -left});
                add_clause({literal, -right});
                add_clause({-literal, left, right});
                break;
            }
            case prop_formula_internal::NodeType::Implies: {
                const int left = encode_node(node.m_left);
                const int right = encode_node(node.m_right);
                literal = new_auxiliary();
                add_clause({-literal, -left, right});
                add_clause({left, literal});
                add_clause({-right, literal});
                break;
            }
            case prop_formula_internal::NodeType::Iff: {
                const int left = encode_node(node.m_left);
                const int right = encode_node(node.m_right);
                literal = new_auxiliary();
                add_clause({-literal, -left, right});
                add_clause({-literal, left, -right});
                add_clause({literal, left, right});
                add_clause({literal, -left, -right});
                break;
            }
            case prop_formula_internal::NodeType::Next:
            case prop_formula_internal::NodeType::Eventually:
            case prop_formula_internal::NodeType::Globally:
            case prop_formula_internal::NodeType::Until:
            case prop_formula_internal::NodeType::Release:
            case prop_formula_internal::NodeType::WeakUntil:
                // Tseitin CNF is defined only for propositional formulae.
                // Temporal operators must never reach here; callers guard with
                // Formula::is_propositional().
                assert(false);
                __builtin_unreachable();
        }
        m_node_literal_cache[index] = literal;
        return literal;
    }

    int get_or_create_symbol(const std::string& name) {
        auto found = m_symbol_to_variable.find(name);
        if (found != m_symbol_to_variable.end()) {
            return found->second;
        }

        const int variable = m_next_variable_id;
        m_symbol_to_variable[name] = variable;
        ++m_next_variable_id;
        return variable;
    }

    int new_auxiliary() {
        const int variable = m_next_variable_id;
        ++m_next_variable_id;
        return variable;
    }

    void add_clause(std::vector<int> clause) {
        m_clauses.push_back(std::move(clause));
    }
};

}  // namespace

namespace prop_formula_internal {

DimacsCnf encode_dimacs(const std::vector<Node>& nodes) {
    TseitinEncoder encoder(nodes);
    return encoder.encode();
}

}  // namespace prop_formula_internal
