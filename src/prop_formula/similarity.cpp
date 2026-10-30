#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hash_combine.hpp"
#include "internal.hpp"

namespace {

struct SubformulaKey {
    prop_formula_internal::NodeType m_type =
        prop_formula_internal::NodeType::Atom;
    std::size_t m_left = 0;
    std::size_t m_right = 0;
    std::string_view m_variable;
};

struct SubformulaKeyHash {
    std::size_t operator()(const SubformulaKey& key) const {
        std::size_t seed = std::hash<int>{}(static_cast<int>(key.m_type));
        seed = hash_combine(seed, std::hash<std::size_t>{}(key.m_left));
        seed = hash_combine(seed, std::hash<std::size_t>{}(key.m_right));
        seed =
            hash_combine(seed, std::hash<std::string_view>{}(key.m_variable));
        return seed;
    }
};

struct SubformulaKeyEqual {
    bool operator()(const SubformulaKey& lhs, const SubformulaKey& rhs) const {
        return lhs.m_type == rhs.m_type && lhs.m_left == rhs.m_left &&
               lhs.m_right == rhs.m_right && lhs.m_variable == rhs.m_variable;
    }
};

std::unordered_map<std::size_t, std::size_t> collect_subformula_ids(
    const std::vector<prop_formula_internal::Node>& input_nodes,
    std::unordered_map<SubformulaKey, std::size_t, SubformulaKeyHash,
                       SubformulaKeyEqual>& id_pool,
    std::size_t& next_id) {
    std::vector<std::size_t> ids(input_nodes.size(), 0);
    std::unordered_map<std::size_t, std::size_t> counts;
    counts.reserve(input_nodes.size());
    for (std::size_t index = 0; index < input_nodes.size(); ++index) {
        const prop_formula_internal::Node& node = input_nodes[index];
        SubformulaKey key{};
        key.m_type = node.m_type;
        switch (node.m_type) {
            case prop_formula_internal::NodeType::Atom:
                key.m_variable = node.m_variable;
                break;
            case prop_formula_internal::NodeType::Not:
            case prop_formula_internal::NodeType::Next:
            case prop_formula_internal::NodeType::Eventually:
            case prop_formula_internal::NodeType::Globally:
                key.m_left = ids[node.m_left];
                break;
            case prop_formula_internal::NodeType::And:
            case prop_formula_internal::NodeType::Or:
            case prop_formula_internal::NodeType::Implies:
            case prop_formula_internal::NodeType::Iff:
            case prop_formula_internal::NodeType::Until:
            case prop_formula_internal::NodeType::Release:
            case prop_formula_internal::NodeType::WeakUntil:
                key.m_left = ids[node.m_left];
                key.m_right = ids[node.m_right];
                break;
        }
        const auto [pool_iter, inserted] = id_pool.emplace(key, next_id);
        if (inserted) {
            ids[index] = next_id;
            ++next_id;
        } else {
            ids[index] = pool_iter->second;
        }
        ++counts[ids[index]];
    }
    return counts;
}

std::size_t count_shared_subformulae(
    const std::vector<prop_formula_internal::Node>& nodes,
    const std::vector<prop_formula_internal::Node>& other_nodes) {
    std::unordered_map<SubformulaKey, std::size_t, SubformulaKeyHash,
                       SubformulaKeyEqual>
        id_pool;
    std::size_t next_id = 1;
    id_pool.reserve((nodes.size() + other_nodes.size()) * 2);
    const auto counts = collect_subformula_ids(nodes, id_pool, next_id);
    const auto other_counts =
        collect_subformula_ids(other_nodes, id_pool, next_id);
    std::size_t shared_subformulae = 0;
    for (const auto& [signature, count] : counts) {
        const auto other_it = other_counts.find(signature);
        if (other_it == other_counts.end()) {
            continue;
        }
        shared_subformulae += std::min(count, other_it->second);
    }
    return shared_subformulae;
}

}  // namespace

double Formula::syntactic_similarity(const Formula& other) const {
    if (this->n_subformulae() == 0 || other.n_subformulae() == 0) {
        return 1.0;
    }
    const auto shared_subformulae =
        static_cast<double>(this->shared_subformulae(other));
    const double first =
        shared_subformulae / static_cast<double>(this->n_subformulae());
    const double second =
        shared_subformulae / static_cast<double>(other.n_subformulae());
    // Harmonic mean: if this is a small subformula fully contained in a much
    // larger other (or vice versa), the arithmetic mean would float at 0.5
    // regardless of size difference, since the contained side's ratio is
    // trivially 1. Disjoint formulas give first == second == 0, which the
    // formula below would otherwise divide as 0/0.
    if (first == 0.0 && second == 0.0) {
        return 0.0;
    }
    return (2.0 * first * second) / (first + second);
}

size_t Formula::n_subformulae() const { return m_impl->m_nodes.size(); }

size_t Formula::shared_subformulae(const Formula& other) const {
    return count_shared_subformulae(m_impl->m_nodes, other.m_impl->m_nodes);
}
