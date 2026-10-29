#pragma once

// The pairwise sweep behind the implication filter, shared by both front ends
// and by the streaming filter that runs it batch by batch during the search.
//
// A specification is subsumed when another dominates it: implies it without
// being implied back, or is equivalent to it and outranks it under
// `prefer_first`. What survives is the antichain of maximal specifications.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "filter/implication.hpp"
#include "fingerprint/lasso.hpp"
#include "fingerprint/prefilter.hpp"
#include "thread_pool.hpp"

namespace antichain {

// Orders the two members of an equivalence class so exactly one survives.
// Higher similarity to the original wins; `operator<` settles the rest.
// Similarity alone is not a total order -- two distinct specs routinely score
// identically against the original -- and a rule that is not total would leave
// the survivor decided by whichever pair the concurrent sweep happens to finish
// first, which is how the same seed would stop reproducing.
template <typename Spec>
bool prefer_first(const Spec& first, const Spec& second, double first_key,
                  double second_key) {
    if (first_key != second_key) {
        return first_key > second_key;
    }
    return second < first;
}

// The position of the first occurrence of each distinct entry of @p pop.
// Exact duplicates relate identically to every other spec, the implication
// check reading only the lowered formula, so one representative per group is
// all the sweep needs to see.
template <typename Spec>
std::vector<std::size_t> representatives_of(const std::vector<Spec>& pop) {
    std::unordered_map<Spec, std::size_t> position_of;
    std::vector<std::size_t> representatives;
    for (std::size_t idx = 0; idx < pop.size(); ++idx) {
        if (position_of.try_emplace(pop[idx], representatives.size()).second) {
            representatives.push_back(idx);
        }
    }
    return representatives;
}

// One call per spec rather than per pair: the key is a property of the spec,
// and the sweep is quadratic in the specs.
template <typename Spec, typename Key>
std::vector<double> keys_of(const std::vector<Spec>& specs, const Key& key) {
    std::vector<double> keys(specs.size(), 0.0);
    if (key) {
        for (std::size_t idx = 0; idx < specs.size(); ++idx) {
            keys[idx] = key(specs[idx]);
        }
    }
    return keys;
}

// True when a sampled word refutes `specs[lhs] -> specs[rhs]`. @p prints is
// either empty or one entry per spec, and an entry may itself be empty: the
// streaming filter samples each batch separately and a batch whose lowering
// the parser cannot read back has no table. All tables are drawn from the same
// words over the same signals, so entries from different batches compare.
inline bool refutes(const std::vector<fingerprint::PackedFingerprint>& prints,
                    std::size_t lhs, std::size_t rhs) {
    return !prints.empty() && !prints[lhs].empty() && !prints[rhs].empty() &&
           fingerprint::refutes_implication(prints[lhs], prints[rhs]);
}

namespace detail {

// Checks one unordered pair {a, b} in both directions and marks the dominated
// side, if any.
//
// Short-circuits if either endpoint is already subsumed: once a spec is known
// redundant, no further comparison against it can change the outcome. That
// needs the dominance relation to be transitive, or a skipped pair could strand
// a dominated spec unmarked. It stays transitive with the tie-break in: within
// a class the order is `prefer_first`, which is total, and across classes
// equivalent specs imply exactly the same things, so a strict edge into or out
// of one member is a strict edge for every member.
template <typename Spec, typename Implies>
void check_pair(const std::vector<Spec>& specs, const std::vector<double>& keys,
                const std::vector<fingerprint::PackedFingerprint>& prints,
                std::vector<std::atomic<std::uint8_t>>& subsumed,
                const Implies& implies, std::size_t a_pos, std::size_t b_pos) {
    if (subsumed[a_pos].load(std::memory_order_relaxed) != 0U ||
        subsumed[b_pos].load(std::memory_order_relaxed) != 0U) {
        ImplicationFilterStats::n_skipped.fetch_add(1,
                                                    std::memory_order_relaxed);
        return;
    }
    ImplicationFilterStats::n_comparisons.fetch_add(1,
                                                    std::memory_order_relaxed);
    const Spec& spec_a = specs[a_pos];
    const Spec& spec_b = specs[b_pos];
    // A timed-out check cannot establish dominance, so @p implies reads one as
    // false and an uncertain pair keeps both endpoints. An equivalence that
    // only one direction proves in time therefore reads as strict domination,
    // and one that neither proves keeps both.
    //
    // A refuted direction is settled as false with no solver call, which can
    // only replace an undecided verdict with the answer it was already given.
    const bool a_implies_b =
        !refutes(prints, a_pos, b_pos) && implies(spec_a, spec_b);
    const bool b_implies_a =
        !refutes(prints, b_pos, a_pos) && implies(spec_b, spec_a);
    if (a_implies_b && b_implies_a) {
        const bool keep_a =
            prefer_first(spec_a, spec_b, keys[a_pos], keys[b_pos]);
        subsumed[keep_a ? b_pos : a_pos].store(1, std::memory_order_relaxed);
        ImplicationFilterStats::n_equivalent_collapsed.fetch_add(
            1, std::memory_order_relaxed);
    } else if (a_implies_b) {
        subsumed[b_pos].store(1, std::memory_order_relaxed);
    } else if (b_implies_a) {
        subsumed[a_pos].store(1, std::memory_order_relaxed);
    }
}

}  // namespace detail

// Which of @p specs are subsumed by another, as one flag per spec. @p specs
// must be distinct, and its first @p n_settled entries must already be an
// antichain: no pair among them is asked, since if z dominated x and x
// dominated y with y and z both settled, z would dominate y. The batch filter
// passes 0; the streaming filter passes its current maximal set followed by a
// new batch, which is how merging batch by batch reaches the same antichain as
// one sweep over their union wherever no check times out.
//
// @p implies(a, b) is called concurrently from pool workers and returns
// whether a implies b, reading a timeout as false.
template <typename Spec, typename Implies>
std::vector<std::uint8_t> merge_subsumed(
    const std::vector<Spec>& specs, const std::vector<double>& keys,
    const std::vector<fingerprint::PackedFingerprint>& prints,
    std::size_t n_settled, const Implies& implies, TaskPriority priority,
    const std::function<void(std::size_t, std::size_t)>& on_progress) {
    const std::size_t n_specs = specs.size();
    assert(keys.size() == n_specs);
    assert(prints.empty() || prints.size() == n_specs);
    assert(n_settled <= n_specs);
    // A pair both of whose directions a word refutes is dropped here rather
    // than dispatched and returned from. Dispatching it costs more than the two
    // ANDs it saves: `run_bounded_async` bounds how many items are in flight,
    // so a region that is 96% instant tasks keeps refilling its window with
    // them and holds about one subprocess open at a time. Measured over 80
    // TLSF candidates, skipping inside the task left 1033 ltlfilt calls running
    // at 1.07x concurrency for 21.5s of wall, against the unfiltered sweep's
    // 3361 calls at 18x for 6.4s -- fewer calls and three times the wall.
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::size_t refuted_directions = 0;
    for (std::size_t i = 0; i < n_specs; ++i) {
        for (std::size_t j = std::max(i + 1, n_settled); j < n_specs; ++j) {
            const bool forward = refutes(prints, i, j);
            const bool backward = refutes(prints, j, i);
            refuted_directions += static_cast<std::size_t>(forward) +
                                  static_cast<std::size_t>(backward);
            if (!forward || !backward) {
                pairs.emplace_back(i, j);
            }
        }
    }
    ImplicationFilterStats::n_fingerprint_refuted.fetch_add(
        refuted_directions, std::memory_order_relaxed);
    std::vector<std::atomic<std::uint8_t>> subsumed(n_specs);
    for (std::atomic<std::uint8_t>& flag : subsumed) {
        flag.store(0, std::memory_order_relaxed);
    }
    std::size_t completed = 0;
    run_bounded_async(
        pairs.size(), dispatch_window(),
        [&specs, &keys, &prints, &subsumed, &implies, &pairs](std::size_t idx) {
            const std::size_t a_pos = pairs[idx].first;
            const std::size_t b_pos = pairs[idx].second;
            return [&specs, &keys, &prints, &subsumed, &implies, a_pos, b_pos] {
                detail::check_pair(specs, keys, prints, subsumed, implies,
                                   a_pos, b_pos);
            };
        },
        [&on_progress, &completed, total = pairs.size()](std::size_t) {
            if (on_progress) {
                on_progress(++completed, total);
            }
        },
        {}, priority);
    std::vector<std::uint8_t> result(n_specs, 0);
    for (std::size_t idx = 0; idx < n_specs; ++idx) {
        result[idx] = subsumed[idx].load(std::memory_order_relaxed);
    }
    return result;
}

// One flag per entry of @p pop, set where the entry is subsumed: by another
// entry, or by an earlier copy of itself, only the first of a group of equal
// entries being able to survive. The whole batch sweep, over the foreground
// queue.
template <typename Spec, typename Implies, typename Key>
std::vector<std::uint8_t> subsumed_in(
    const std::vector<Spec>& pop, const Implies& implies, const Key& key,
    const std::function<void(std::size_t, std::size_t)>& on_progress) {
    const std::vector<std::size_t> representatives = representatives_of(pop);
    ImplicationFilterStats::n_duplicates.fetch_add(
        pop.size() - representatives.size(), std::memory_order_relaxed);
    std::vector<Spec> rep_specs;
    rep_specs.reserve(representatives.size());
    for (const std::size_t index : representatives) {
        rep_specs.push_back(pop[index]);
    }
    const std::vector<std::uint8_t> rep_subsumed =
        merge_subsumed(rep_specs, keys_of(rep_specs, key),
                       fingerprint::prefilter::fingerprints_of(rep_specs), 0,
                       implies, TaskPriority::Foreground, on_progress);
    std::vector<std::uint8_t> result(pop.size(), 1);
    for (std::size_t rep_pos = 0; rep_pos < representatives.size(); ++rep_pos) {
        result[representatives[rep_pos]] = rep_subsumed[rep_pos];
    }
    return result;
}

// Resets every counter in ImplicationFilterStats, ahead of a sweep whose
// figures should stand alone.
inline void reset_stats() {
    ImplicationFilterStats::n_comparisons.store(0, std::memory_order_relaxed);
    ImplicationFilterStats::n_skipped.store(0, std::memory_order_relaxed);
    ImplicationFilterStats::n_duplicates.store(0, std::memory_order_relaxed);
    ImplicationFilterStats::n_timeouts.store(0, std::memory_order_relaxed);
    ImplicationFilterStats::n_equivalent_collapsed.store(
        0, std::memory_order_relaxed);
    ImplicationFilterStats::n_fingerprint_refuted.store(
        0, std::memory_order_relaxed);
    ImplicationFilterStats::n_residual_queries.store(0,
                                                     std::memory_order_relaxed);
    ImplicationFilterStats::n_residual_settled.store(0,
                                                     std::memory_order_relaxed);
}

}  // namespace antichain
