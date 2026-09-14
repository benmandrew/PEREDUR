#include "fitness/syntactic_similarity.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

#include "profile.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"

namespace {

// Geometric ratio r and small fixed weight w for the timing measure.
// μ(ForTicks{N}) = μ(WithinTicks{N}) = μ(AfterTicks{N}) = r^N
// μ(Immediately) = μ(NextTimepoint) = μ(Eventually) = μ(Always) = w
//
// r = 1/2 weights the Nth member of each infinite family by r^N, so every
// downset measure is a finite sum plus a convergent geometric tail. No
// normalisation is needed: a uniform rescaling of μ cancels in the Jaccard
// ratio below. The four non-family timings carry a small fixed w = 0.01 so
// that Eventually, which every downset contains, keeps each intersection
// non-empty without moving the ratio appreciably. Derived in the paper.
constexpr double k_timing_geo_ratio = 0.5;
constexpr double k_timing_discrete_weight = 0.01;

// Σ_{k=1}^{n} r^k
double geo_sum_up_to(int n_terms) {
    if (n_terms <= 0) {
        return 0.0;
    }
    return k_timing_geo_ratio * (1.0 - std::pow(k_timing_geo_ratio, n_terms)) /
           (1.0 - k_timing_geo_ratio);
}

// Σ_{k=min_exp}^{∞} r^k = r^min_exp / (1 - r)
double geo_sum_from(int min_exp) {
    return std::pow(k_timing_geo_ratio, min_exp) / (1.0 - k_timing_geo_ratio);
}

// Largest k such that ForTicks{k} ∈ ↓tim. Returns 0 if none.
int max_for_index(const Timing& tim) {
    if (const auto* for_ptr = std::get_if<timing::ForTicks>(&tim)) {
        return static_cast<int>(for_ptr->m_ticks);
    }
    return 0;
}

// Smallest k such that WithinTicks{k} ∈ ↓tim.
// Returns 0 (sentinel for "none") when ↓tim contains no WithinTicks elements.
int min_within_index(const Timing& tim) {
    return std::visit(
        [](const auto& val) -> int {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, timing::ForTicks> ||
                          std::is_same_v<T, timing::Immediately> ||
                          std::is_same_v<T, timing::NextTimepoint>) {
                return 1;
            } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
                return static_cast<int>(val.m_ticks);
            } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
                return static_cast<int>(val.m_ticks) + 1;
            } else {
                return 0;  // Eventually: no WithinTicks in ↓tim
            }
        },
        tim);
}

bool has_immediately(const Timing& tim) {
    return std::holds_alternative<timing::ForTicks>(tim) ||
           std::holds_alternative<timing::Immediately>(tim);
}

bool has_next_timepoint(const Timing& tim) {
    return std::holds_alternative<timing::ForTicks>(tim) ||
           std::holds_alternative<timing::NextTimepoint>(tim);
}

// Returns N if tim == AfterTicks{N}, otherwise -1.
int after_ticks_self(const Timing& tim) {
    if (const auto* aft = std::get_if<timing::AfterTicks>(&tim)) {
        return static_cast<int>(aft->m_ticks);
    }
    return -1;
}

bool is_always(const Timing& tim) {
    return std::holds_alternative<timing::Always>(tim);
}

// μ(↓Always) — Always is the top of the order: it implies (i.e. its downset
// contains) every timing except the AfterTicks family, which requires the
// response to be *false* at the initial ticks and so is incompatible with
// "always". ↓Always = {Always, Eventually, Immediately, NextTimepoint} ∪
// {WithinTicks{k≥1}} ∪ {ForTicks{k≥1}}. The two geometric tails are each
// Σ_{k=1}^∞ r^k = geo_sum_from(1).
double mu_downset_always() {
    return (4 * k_timing_discrete_weight) + (2 * geo_sum_from(1));
}

// μ(↓tim) — measure of the downward closure of tim in the timing partial order.
double mu_downset(const Timing& tim) {
    if (is_always(tim)) {
        return mu_downset_always();
    }
    double result = k_timing_discrete_weight;  // Eventually ∈ ↓tim always
    result += geo_sum_up_to(max_for_index(tim));
    if (has_immediately(tim)) {
        result += k_timing_discrete_weight;
    }
    if (has_next_timepoint(tim)) {
        result += k_timing_discrete_weight;
    }
    const int min_w = min_within_index(tim);
    if (min_w > 0) {
        result += geo_sum_from(min_w);
    }
    const int aft = after_ticks_self(tim);
    if (aft >= 0) {
        result += std::pow(k_timing_geo_ratio, aft);
    }
    return result;
}

// μ(↓tim1 ∩ ↓tim2) — element e is in intersection iff e ≤ tim1 and e ≤ tim2.
double mu_intersection(const Timing& tim1, const Timing& tim2) {
    if (is_always(tim1) && is_always(tim2)) {
        return mu_downset_always();
    }
    if (is_always(tim1) || is_always(tim2)) {
        // ↓Always is everything but the AfterTicks family, so ↓Always ∩ ↓other
        // is ↓other with its AfterTicks self-element (the only AfterTicks that
        // can appear in a downset) removed.
        const Timing& other = is_always(tim1) ? tim2 : tim1;
        double result = mu_downset(other);
        const int aft = after_ticks_self(other);
        if (aft >= 0) {
            result -= std::pow(k_timing_geo_ratio, aft);
        }
        return result;
    }
    double result = k_timing_discrete_weight;  // Eventually always in both
    result += geo_sum_up_to(std::min(max_for_index(tim1), max_for_index(tim2)));
    if (has_immediately(tim1) && has_immediately(tim2)) {
        result += k_timing_discrete_weight;
    }
    if (has_next_timepoint(tim1) && has_next_timepoint(tim2)) {
        result += k_timing_discrete_weight;
    }
    const int min_w1 = min_within_index(tim1);
    const int min_w2 = min_within_index(tim2);
    if (min_w1 > 0 && min_w2 > 0) {
        result += geo_sum_from(std::max(min_w1, min_w2));
    }
    // AfterTicks{k} is only in ↓AfterTicks{k}, so it appears in the
    // intersection only when both timings are the same AfterTicks{k}.
    const int aft1 = after_ticks_self(tim1);
    const int aft2 = after_ticks_self(tim2);
    if (aft1 >= 0 && aft1 == aft2) {
        result += std::pow(k_timing_geo_ratio, aft1);
    }
    return result;
}

// Jaccard similarity on downward closures:
//   synSim_time(tim, tim') = μ(↓tim ∩ ↓tim') / μ(↓tim ∪ ↓tim')
double timing_syntactic_similarity(const Timing& tim, const Timing& tim_other) {
    const double mu_tim = mu_downset(tim);
    const double mu_other = mu_downset(tim_other);
    const double mu_inter = mu_intersection(tim, tim_other);
    const double mu_union = mu_tim + mu_other - mu_inter;
    return mu_inter / mu_union;
}

// --- Condition type --------------------------------------------------------

// Jaccard on downward closures, as the timing term is, over the two-element
// implication order: Continual implies Trigger for every scope and every
// timing, because a trigger fires on the rising edges of its condition and
// those are a subset of the timepoints where the condition holds. The two
// coincide at `always` under a plain scope and at `eventually` under an `only`
// one; the term scores the fields rather than their lowerings, so it does not
// special-case either. So down-Continual is both values
// and down-Trigger is itself alone, giving 1/2 for a pair that differs and 1
// for a pair that agrees. Nothing is chosen here: the order fixes both values.
double condition_type_syntactic_similarity(ConditionType lhs,
                                           ConditionType rhs) {
    return lhs == rhs ? 1.0 : 0.5;
}

// --- Scope -----------------------------------------------------------------
//
// A scope names the set of timepoints its requirement's obligation applies to,
// and the similarity of two scopes is the overlap of those two sets. Relative
// to one mode, a trace divides into four regions: the prefix before the mode
// first holds, the points where it holds, the gaps between mode intervals, and
// the suffix after it last holds. Every scope is a union of them.
//
// This is not the implication order the mutation arm walks (scope_order in
// src/genetic/mutation.cpp). That order is timing-dependent, because a scope
// boundary relaxes a bounded obligation and tightens an unbounded one, so it is
// a property of the scope *and* the timing. A similarity between two scopes has
// to be a property of the scopes alone, and their regions are exactly that.
enum ScopeRegion : std::uint8_t {
    k_region_before = 1U << 0U,   // before the mode first holds
    k_region_in = 1U << 1U,       // where the mode holds
    k_region_between = 1U << 2U,  // the gaps between mode intervals
    k_region_after = 1U << 3U,    // after the mode last holds
    k_region_all =
        k_region_before | k_region_in | k_region_between | k_region_after,
};

// The regions a scope's obligation applies to. The three "only" scopes carry
// the *negation* of the obligation, and they carry it outside their named
// interval, which is why each is the complement of the plain scope it is named
// after.
std::uint8_t scope_regions(ScopeKind kind) {
    switch (kind) {
        case ScopeKind::Global:
            return k_region_all;
        case ScopeKind::In:
            return k_region_in;
        case ScopeKind::NotIn:
        case ScopeKind::OnlyIn:
            return k_region_all & ~k_region_in;
        case ScopeKind::Before:
            return k_region_before;
        case ScopeKind::After:
            return k_region_after;
        case ScopeKind::OnlyBefore:
            return k_region_all & ~k_region_before;
        case ScopeKind::OnlyAfter:
            return k_region_all & ~k_region_after;
    }
    assert(false);
    __builtin_unreachable();
}

std::size_t popcount(unsigned bits) {
    std::size_t count = 0;
    for (; bits != 0U; bits >>= 1U) {
        count += bits & 1U;
    }
    return count;
}

// The region bits of @p scope, shifted into the upper nibble for an "only"
// scope so that a plain scope and an "only" one never share a bit.
unsigned tagged_regions(const Scope& scope) {
    const unsigned bits = scope_regions(scope.m_kind);
    return is_only_scope(scope.m_kind) ? bits << 4U : bits;
}

// Jaccard overlap of the two region sets, with polarity folded in rather than
// averaged alongside: an "only" scope's regions are tagged apart from a plain
// scope's, so `except in m` and `only in m` share every region and still score
// zero. They make opposite claims about the same timepoints, and crediting the
// shared region would read them as near-identical.
double region_similarity(const Scope& lhs, const Scope& rhs) {
    const unsigned lhs_bits = tagged_regions(lhs);
    const unsigned rhs_bits = tagged_regions(rhs);
    const std::size_t union_size = popcount(lhs_bits | rhs_bits);
    if (union_size == 0) {
        return 1.0;
    }
    return static_cast<double>(popcount(lhs_bits & rhs_bits)) /
           static_cast<double>(union_size);
}

// Jaccard on regions, averaged with whether the two scopes are relative to the
// same mode. The mode is an opaque atom, so it is compared for equality rather
// than for structure — there is nothing inside it to be partly similar to. Two
// Global scopes name no mode and agree trivially, which is what makes this
// read 1.0 across a specification that uses no scopes.
double scope_syntactic_similarity(const Scope& lhs, const Scope& rhs) {
    const double same_mode = lhs.m_mode == rhs.m_mode ? 1.0 : 0.0;
    return (region_similarity(lhs, rhs) + same_mode) / 2.0;
}

Formula conjoin_field(const Specification& spec, Formula Requirement::* field) {
    std::optional<Formula> conj;
    auto accumulate = [&](const std::vector<Requirement>& reqs) {
        for (const Requirement& req : reqs) {
            if (req.m_removed) {
                continue;
            }
            if (!conj) {
                conj = req.*field;
            } else {
                conj =
                    Formula::make_binary(Formula::Kind::And, *conj, req.*field);
            }
        }
    };
    accumulate(spec.m_assumptions);
    accumulate(spec.m_guarantees);
    // Every requirement removed leaves nothing to fold. That is reachable only
    // through the removal operator, and the unit of conjunction is the honest
    // answer for it; a specification with content always folds to something.
    if (!conj.has_value()) {
        return Formula("true");
    }
    return *conj;
}

Formula conjoin_triggers(const Specification& spec) {
    return conjoin_field(spec, &Requirement::m_condition);
}

Formula conjoin_responses(const Specification& spec) {
    return conjoin_field(spec, &Requirement::m_response);
}

double average_timing_similarity(const Specification& spec1,
                                 const Specification& spec2) {
    // The two specifications need not have the same number of assumptions or
    // guarantees: the p_add_assumption mutation grows a candidate's assumption
    // list relative to the original it is scored against. Pair requirements by
    // index over the counts they share, treat each unmatched surplus
    // requirement as contributing zero similarity, and normalise by the larger
    // structure so a size difference lowers the score. This reduces to the
    // exact per-index average when the counts match. Indexing by spec1's counts
    // (as before) would read past the end of spec2 whenever spec1 has more
    // requirements, which is undefined behaviour once NDEBUG disables the
    // asserts that used to guard it.
    const std::size_t common_assumptions =
        std::min(spec1.m_assumptions.size(), spec2.m_assumptions.size());
    const std::size_t common_guarantees =
        std::min(spec1.m_guarantees.size(), spec2.m_guarantees.size());
    const std::size_t total =
        std::max(spec1.m_assumptions.size(), spec2.m_assumptions.size()) +
        std::max(spec1.m_guarantees.size(), spec2.m_guarantees.size());
    if (total == 0) {
        return 0.0;
    }
    // A slot removed on one side alone has no timing to compare against, and
    // removal is the largest change that slot can undergo, so it scores zero.
    // Removed on both sides, the slot matches.
    const auto pair_similarity = [](const Requirement& lhs,
                                    const Requirement& rhs) {
        if (lhs.m_removed || rhs.m_removed) {
            return lhs.m_removed && rhs.m_removed ? 1.0 : 0.0;
        }
        return timing_syntactic_similarity(lhs.m_timing, rhs.m_timing);
    };
    double sum = 0.0;
    for (std::size_t i = 0; i < common_assumptions; ++i) {
        sum += pair_similarity(spec1.m_assumptions[i], spec2.m_assumptions[i]);
    }
    for (std::size_t i = 0; i < common_guarantees; ++i) {
        sum += pair_similarity(spec1.m_guarantees[i], spec2.m_guarantees[i]);
    }
    return sum / static_cast<double>(total);
}

}  // namespace

// The scope counterpart of average_timing_similarity, pairing by index on the
// same terms and for the same reason: slot i of a candidate descends from slot
// i of the original, so that is the only pairing that compares a requirement
// with what it came from.
double average_scope_similarity(const Specification& spec1,
                                const Specification& spec2) {
    const std::size_t common_assumptions =
        std::min(spec1.m_assumptions.size(), spec2.m_assumptions.size());
    const std::size_t common_guarantees =
        std::min(spec1.m_guarantees.size(), spec2.m_guarantees.size());
    const std::size_t total =
        std::max(spec1.m_assumptions.size(), spec2.m_assumptions.size()) +
        std::max(spec1.m_guarantees.size(), spec2.m_guarantees.size());
    if (total == 0) {
        return 0.0;
    }
    const auto pair_similarity = [](const Requirement& lhs,
                                    const Requirement& rhs) {
        if (lhs.m_removed || rhs.m_removed) {
            return lhs.m_removed && rhs.m_removed ? 1.0 : 0.0;
        }
        return scope_syntactic_similarity(lhs.m_scope, rhs.m_scope);
    };
    double sum = 0.0;
    for (std::size_t i = 0; i < common_assumptions; ++i) {
        sum += pair_similarity(spec1.m_assumptions[i], spec2.m_assumptions[i]);
    }
    for (std::size_t i = 0; i < common_guarantees; ++i) {
        sum += pair_similarity(spec1.m_guarantees[i], spec2.m_guarantees[i]);
    }
    return sum / static_cast<double>(total);
}

// The condition-type counterpart of average_timing_similarity, pairing by index
// on the same terms and for the same reason.
double average_condition_type_similarity(const Specification& spec1,
                                         const Specification& spec2) {
    const std::size_t common_assumptions =
        std::min(spec1.m_assumptions.size(), spec2.m_assumptions.size());
    const std::size_t common_guarantees =
        std::min(spec1.m_guarantees.size(), spec2.m_guarantees.size());
    const std::size_t total =
        std::max(spec1.m_assumptions.size(), spec2.m_assumptions.size()) +
        std::max(spec1.m_guarantees.size(), spec2.m_guarantees.size());
    if (total == 0) {
        return 0.0;
    }
    const auto pair_similarity = [](const Requirement& lhs,
                                    const Requirement& rhs) {
        if (lhs.m_removed || rhs.m_removed) {
            return lhs.m_removed && rhs.m_removed ? 1.0 : 0.0;
        }
        return condition_type_syntactic_similarity(lhs.m_condition_type,
                                                   rhs.m_condition_type);
    };
    double sum = 0.0;
    for (std::size_t i = 0; i < common_assumptions; ++i) {
        sum += pair_similarity(spec1.m_assumptions[i], spec2.m_assumptions[i]);
    }
    for (std::size_t i = 0; i < common_guarantees; ++i) {
        sum += pair_similarity(spec1.m_guarantees[i], spec2.m_guarantees[i]);
    }
    return sum / static_cast<double>(total);
}

double syntactic_similarity(const Requirement& requirement,
                            const Requirement& other_requirement,
                            [[maybe_unused]] const Config& cfg) {
    double condition_similarity = requirement.m_condition.syntactic_similarity(
        other_requirement.m_condition);
    double response_similarity = requirement.m_response.syntactic_similarity(
        other_requirement.m_response);
    double timing_similarity = timing_syntactic_similarity(
        requirement.m_timing, other_requirement.m_timing);
    double scope_similarity = scope_syntactic_similarity(
        requirement.m_scope, other_requirement.m_scope);
    double condition_type_similarity = condition_type_syntactic_similarity(
        requirement.m_condition_type, other_requirement.m_condition_type);
    return (condition_similarity + response_similarity + timing_similarity +
            scope_similarity + condition_type_similarity) /
           5.0;
}

double syntactic_similarity(const Specification& specification,
                            const Specification& other_specification,
                            [[maybe_unused]] const Config& cfg) {
    PEREDUR_PROFILE_SCOPE("fitness/syntactic_similarity_spec");
    assert((!specification.m_assumptions.empty() ||
            !specification.m_guarantees.empty()) &&
           (!other_specification.m_assumptions.empty() ||
            !other_specification.m_guarantees.empty()));
    const double trigger_similarity =
        conjoin_triggers(specification)
            .syntactic_similarity(conjoin_triggers(other_specification));
    const double response_similarity =
        conjoin_responses(specification)
            .syntactic_similarity(conjoin_responses(other_specification));
    double timing_similarity =
        average_timing_similarity(specification, other_specification);
    double scope_similarity =
        average_scope_similarity(specification, other_specification);
    double condition_type_similarity =
        average_condition_type_similarity(specification, other_specification);
    return (trigger_similarity + response_similarity + timing_similarity +
            scope_similarity + condition_type_similarity) /
           5.0;
}
