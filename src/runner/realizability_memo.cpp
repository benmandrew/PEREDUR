#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "filter/implication_check.hpp"
#include "formula_key.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "runner/spot_internal.hpp"

using spot_internal::fretish_sides_of;
using spot_internal::includes_sorted;
using spot_internal::join_comma;
using spot_internal::query_scope;
using spot_internal::signature_of;

namespace {

// A query replacing more conjuncts than this is not ordered against an entry:
// past a few, the rule calls cost more than the scan saves.
constexpr std::size_t k_rule_difference_cap = 4;

// The ids of @p lhs missing from @p rhs, into @p out, or false once there are
// more than k_rule_difference_cap. A bit of the signature difference is at
// least one missing id, so the popcount rejects most entries before the walk.
bool bounded_difference(const std::vector<int>& lhs,
                        std::uint64_t lhs_signature,
                        const std::vector<int>& rhs,
                        std::uint64_t rhs_signature, std::vector<int>& out) {
    if (std::bitset<64>(lhs_signature & ~rhs_signature).count() >
        k_rule_difference_cap) {
        return false;
    }
    out.clear();
    auto cursor = rhs.begin();
    for (const int conjunct_id : lhs) {
        while (cursor != rhs.end() && *cursor < conjunct_id) {
            ++cursor;
        }
        if (cursor != rhs.end() && *cursor == conjunct_id) {
            continue;
        }
        if (out.size() == k_rule_difference_cap) {
            return false;
        }
        out.push_back(conjunct_id);
    }
    return true;
}

// Canonically, so that a re-spelling of a conjunct is still the same conjunct
// -- which is the whole reason two candidates differing in one slot are
// comparable at all. A tag before the unit separator is carried through
// verbatim: it says which section the formula sits in, and the same formula in
// two sections is two conjuncts.
std::string conjunct_key(const std::string& conjunct) {
    const std::size_t separator = conjunct.find('\x1f');
    return separator == std::string::npos
               ? formula_key::canonical(conjunct)
               : conjunct.substr(0, separator + 1) +
                     formula_key::canonical(conjunct.substr(separator + 1));
}

}  // namespace

namespace spot_internal {

// A 64-bit signature of an id set. A superset test needs every bit of the
// smaller side present in the larger, so `(small & ~large) != 0` rejects an
// entry outright and the sorted-vector walk only runs on what survives.
std::uint64_t signature_of(const std::vector<int>& ids) {
    std::uint64_t signature = 0;
    for (const int conjunct_id : ids) {
        signature |= std::uint64_t{1}
                     << (static_cast<unsigned>(conjunct_id) % 64U);
    }
    return signature;
}

// Both vectors are sorted and duplicate-free, so this is one merge.
bool includes_sorted(const std::vector<int>& superset,
                     const std::vector<int>& subset) {
    return std::includes(superset.begin(), superset.end(), subset.begin(),
                         subset.end());
}

// The sides check_realizability passes for @p specification. The pointers
// borrow from it.
SpecificationSides fretish_sides_of(const Specification& specification) {
    SpecificationSides sides;
    for (const Requirement& requirement : specification.m_assumptions) {
        if (!requirement.m_removed) {
            sides.m_assumptions.push_back(requirement.m_ltl);
            sides.m_assumption_requirements.push_back(&requirement);
        }
    }
    for (const Requirement& requirement : specification.m_guarantees) {
        if (!requirement.m_removed) {
            sides.m_guarantees.push_back(requirement.m_ltl);
            sides.m_guarantee_requirements.push_back(&requirement);
        }
    }
    return sides;
}

std::string query_scope(const SpecificationSides& sides,
                        const std::vector<std::string>& inputs,
                        const std::vector<std::string>& outputs) {
    return sides.m_scope + "\x1f" + join_comma(inputs) + "\x1f" +
           join_comma(outputs);
}

}  // namespace spot_internal

void RealizabilityChecker::record_witness(const std::string& cache_key,
                                          const std::string& scope,
                                          const SpecificationSides& sides) {
    std::scoped_lock lock(m_cache_mutex);
    if (!m_witness_keys.insert(cache_key).second) {
        return;
    }
    std::vector<int> assumptions =
        intern_side(sides.m_assumptions, sides.m_assumption_requirements);
    std::vector<int> guarantees =
        intern_side(sides.m_guarantees, sides.m_guarantee_requirements);
    const std::uint64_t assumption_signature = signature_of(assumptions);
    const std::uint64_t guarantee_signature = signature_of(guarantees);
    m_witnesses[scope].push_back(
        Decided{std::move(assumptions), std::move(guarantees),
                assumption_signature, guarantee_signature, false});
}

bool RealizabilityChecker::proved_unrealizable(
    const Specification& specification) const {
    const SpecificationSides sides = fretish_sides_of(specification);
    const std::string scope = query_scope(
        sides, environment_signals(specification), specification.m_out_atoms);
    std::vector<std::string> assumption_keys;
    assumption_keys.reserve(sides.m_assumptions.size());
    for (const std::string& conjunct : sides.m_assumptions) {
        assumption_keys.push_back(conjunct_key(conjunct));
    }
    std::vector<std::string> guarantee_keys;
    guarantee_keys.reserve(sides.m_guarantees.size());
    for (const std::string& conjunct : sides.m_guarantees) {
        guarantee_keys.push_back(conjunct_key(conjunct));
    }

    std::scoped_lock lock(m_cache_mutex);
    const auto bucket = m_witnesses.find(scope);
    if (bucket == m_witnesses.end()) {
        return false;
    }
    // A conjunct never interned is in no witness. On the guarantee side that
    // only shrinks the set a witness must fall inside; on the assumption side
    // it leaves nothing that could include it.
    std::vector<int> assumptions;
    for (const std::string& key : assumption_keys) {
        const auto found = m_conjunct_ids.find(key);
        if (found == m_conjunct_ids.end()) {
            return false;
        }
        assumptions.push_back(found->second);
    }
    std::vector<int> guarantees;
    for (const std::string& key : guarantee_keys) {
        const auto found = m_conjunct_ids.find(key);
        if (found != m_conjunct_ids.end()) {
            guarantees.push_back(found->second);
        }
    }
    for (std::vector<int>* side : {&assumptions, &guarantees}) {
        std::sort(side->begin(), side->end());
        side->erase(std::unique(side->begin(), side->end()), side->end());
    }
    const std::uint64_t assumption_signature = signature_of(assumptions);
    const std::uint64_t guarantee_signature = signature_of(guarantees);
    return std::any_of(
        bucket->second.begin(), bucket->second.end(),
        [&](const Decided& witness) {
            return (assumption_signature & ~witness.m_assumption_signature) ==
                       0 &&
                   (witness.m_guarantee_signature & ~guarantee_signature) ==
                       0 &&
                   includes_sorted(witness.m_assumptions, assumptions) &&
                   includes_sorted(guarantees, witness.m_guarantees);
        });
}

void RealizabilityChecker::remember_verdict(
    const std::string& cache_key, const std::string& scope, bool have_sides,
    const std::vector<int>& assumption_ids,
    const std::vector<int>& guarantee_ids, const std::optional<bool>& verdict) {
    m_cache.emplace(cache_key, verdict);
    // Only a decided verdict joins the table. An undecided one is memoised
    // like any other, but it carries no direction, so nothing follows from it
    // about any other query.
    if (have_sides && verdict.has_value()) {
        m_decided[scope].push_back(
            Decided{assumption_ids, guarantee_ids, signature_of(assumption_ids),
                    signature_of(guarantee_ids), *verdict});
    }
}

std::vector<int> RealizabilityChecker::intern_side(
    const std::vector<std::string>& conjuncts,
    const std::vector<const Requirement*>& requirements) {
    assert(requirements.empty() || requirements.size() == conjuncts.size());
    std::vector<int> ids;
    ids.reserve(conjuncts.size());
    for (std::size_t position = 0; position < conjuncts.size(); ++position) {
        const std::string& conjunct = conjuncts[position];
        const Requirement* requirement =
            requirements.empty() ? nullptr : requirements[position];
        const std::string key = conjunct_key(conjunct);
        const auto found = m_conjunct_ids.find(key);
        const int conjunct_id = found != m_conjunct_ids.end()
                                    ? found->second
                                    : static_cast<int>(m_conjunct_ids.size());
        if (found == m_conjunct_ids.end()) {
            m_conjunct_ids.emplace(key, conjunct_id);
            m_conjunct_requirements.emplace_back();
            m_conjunct_formulas.push_back(key);
        }
        // Any requirement lowering to this conjunct stands for all of them,
        // the rules being sound for the formula whichever one is asked.
        auto& stored = m_conjunct_requirements[conjunct_id];
        if (!stored.has_value() && requirement != nullptr) {
            stored = *requirement;
        }
        ids.push_back(conjunct_id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::optional<bool> RealizabilityChecker::subsumed_verdict(
    const std::string& scope, const std::vector<int>& assumptions,
    const std::vector<int>& guarantees, std::uint64_t assumption_signature,
    std::uint64_t guarantee_signature) const {
    const auto bucket = m_decided.find(scope);
    if (bucket == m_decided.end()) {
        return std::nullopt;
    }
    for (const Decided& entry : bucket->second) {
        if (entry.m_realizable) {
            // realizable(A, G) => realizable(A', G') for A' superset A and
            // G' subset G: the same strategy still wins when the system owes
            // less and the environment promises more.
            if ((entry.m_assumption_signature & ~assumption_signature) == 0 &&
                (guarantee_signature & ~entry.m_guarantee_signature) == 0 &&
                includes_sorted(assumptions, entry.m_assumptions) &&
                includes_sorted(entry.m_guarantees, guarantees)) {
                return true;
            }
            continue;
        }
        // unrealizable(A, G) => unrealizable(A', G') in the other direction.
        if ((assumption_signature & ~entry.m_assumption_signature) == 0 &&
            (entry.m_guarantee_signature & ~guarantee_signature) == 0 &&
            includes_sorted(entry.m_assumptions, assumptions) &&
            includes_sorted(guarantees, entry.m_guarantees)) {
            return false;
        }
    }
    return std::nullopt;
}

bool RealizabilityChecker::rule_implies(int stronger, int weaker) {
    const std::uint64_t key = (static_cast<std::uint64_t>(stronger) << 32U) |
                              static_cast<std::uint32_t>(weaker);
    const auto found = m_rule_verdicts.find(key);
    if (found != m_rule_verdicts.end()) {
        return found->second;
    }
    const std::optional<Requirement>& strong =
        m_conjunct_requirements[stronger];
    const std::optional<Requirement>& weak = m_conjunct_requirements[weaker];
    const bool holds = strong.has_value() && weak.has_value() &&
                       requirement_implies(*strong, *weak);
    m_rule_verdicts.emplace(key, holds);
    return holds;
}

// The monotone argument of subsumed_verdict with the inclusions relaxed: a
// conjunct one side lacks may be implied by one the other side replaced it
// with. A query differs from an earlier one mostly by a mutated requirement or
// two, which inclusion never matches and the rules often order.
std::optional<bool> RealizabilityChecker::rule_subsumed_verdict(
    const std::string& scope, const std::vector<int>& assumptions,
    const std::vector<int>& guarantees) {
    const auto bucket = m_decided.find(scope);
    if (bucket == m_decided.end()) {
        return std::nullopt;
    }
    const std::uint64_t assumption_signature = signature_of(assumptions);
    const std::uint64_t guarantee_signature = signature_of(guarantees);
    // Every id in @p needed implied by one in @p offered.
    const auto covered = [this](const std::vector<int>& needed,
                                const std::vector<int>& offered) {
        return std::all_of(needed.begin(), needed.end(), [&](int weaker) {
            return std::any_of(
                offered.begin(), offered.end(),
                [&](int stronger) { return rule_implies(stronger, weaker); });
        });
    };
    std::vector<int> entry_assumptions;
    std::vector<int> query_assumptions;
    std::vector<int> entry_guarantees;
    std::vector<int> query_guarantees;
    for (const Decided& entry : bucket->second) {
        if (!bounded_difference(entry.m_assumptions,
                                entry.m_assumption_signature, assumptions,
                                assumption_signature, entry_assumptions) ||
            !bounded_difference(
                assumptions, assumption_signature, entry.m_assumptions,
                entry.m_assumption_signature, query_assumptions) ||
            !bounded_difference(entry.m_guarantees, entry.m_guarantee_signature,
                                guarantees, guarantee_signature,
                                entry_guarantees) ||
            !bounded_difference(guarantees, guarantee_signature,
                                entry.m_guarantees, entry.m_guarantee_signature,
                                query_guarantees)) {
            continue;
        }
        if (entry.m_realizable) {
            // The query's premise implies the entry's, and the entry's
            // guarantees imply the query's.
            if (covered(entry_assumptions, query_assumptions) &&
                covered(query_guarantees, entry_guarantees)) {
                return true;
            }
            continue;
        }
        if (covered(query_assumptions, entry_assumptions) &&
            covered(entry_guarantees, query_guarantees)) {
            return false;
        }
    }
    return std::nullopt;
}
