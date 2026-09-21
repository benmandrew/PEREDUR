#pragma once

/// @file implication.hpp
/// @brief Population filters based on logical implication: deduplication
///        and the maximal-element (implication partial order) filter.

#include <atomic>
#include <cstddef>
#include <functional>

#include "config.hpp"
#include "genetic/generation.hpp"
#include "runner/black.hpp"

/// Counters for the most recent implication sweep, on either path, reset at
/// the start of each invocation of the returned FilterFunction. n_timeouts is
/// the exception: the FRETISH spec_implies increments it wherever it is
/// called, and tlsf_spec_implies does not count at all, so it reads 0 on the
/// TLSF path.
struct ImplicationFilterStats {
    /// Unordered pairs for which the dominance check actually ran.
    inline static std::atomic<std::size_t> n_comparisons{0};
    /// Unordered pairs skipped because one endpoint was already known
    /// subsumed by an earlier comparison.
    inline static std::atomic<std::size_t> n_skipped{0};
    /// Specs that were exact duplicates of an earlier spec in the
    /// population, and so were excluded from the pairwise sweep entirely
    /// (only their representative can survive).
    inline static std::atomic<std::size_t> n_duplicates{0};
    /// black calls that timed out (inconclusive) during this sweep.
    inline static std::atomic<std::size_t> n_timeouts{0};
    /// Ordered pairs a sampled lasso word refuted before any solver call: one
    /// side accepts a word the other rejects, which is a witness for
    /// `a & !b` and settles `a -> b` as false. Two of these per unordered
    /// pair, so a pair both directions of which are refuted costs no call at
    /// all. Both paths sample, over the specification's declared signals --
    /// inputs, outputs, and on the FRETISH path the modes a scope names.
    inline static std::atomic<std::size_t> n_fingerprint_refuted{0};
    /// Pairs found mutually equivalent, each of which dropped one side. This
    /// is the width of the population's equivalence classes, which nothing
    /// else records: a class of k members contributes k-1 here.
    inline static std::atomic<std::size_t> n_equivalent_collapsed{0};
};

/// Ranks candidates within one equivalence class, higher surviving. Returning
/// the same value for two specs is allowed; the filter breaks the remaining
/// ties on the specification's `operator<` so the survivor stays deterministic.
template <typename Spec>
using SimilarityKeyT = std::function<double(const Spec&)>;

/// The FRETISH similarity key.
using SimilarityKey = SimilarityKeyT<Specification>;

/// Returns a similarity key scoring each candidate by syntactic similarity to
/// @p original, so the member of an equivalence class that reads closest to the
/// specification under repair is the one written out. Instantiated for
/// Specification and tlsf::Specification.
template <typename Spec>
SimilarityKeyT<Spec> syntactic_similarity_key(Spec original, const Config& cfg);

/// Returns a filter that removes structurally equal duplicate specifications,
/// keeping the first occurrence of each distinct spec in the input order.
/// Instantiated for Specification (the default) and tlsf::Specification.
template <typename Spec = Specification>
FilterFunctionT<Spec> make_dedup_filter();

/// Returns a filter that keeps only the maximal specifications of the
/// population under the implication partial order. Instantiated for
/// Specification (the default) and tlsf::Specification, whose implication
/// check is tlsf_spec_implies.
///
/// Spec A strictly dominates spec B when A logically implies B (A & !B is
/// unsatisfiable) but B does not imply A. Mutually equivalent specifications
/// (A implies B and B implies A) contribute exactly one survivor, chosen by
/// @p similarity and, where that ties, by the specification's `operator<`.
///
/// Equivalent specifications are logically one repair written two ways, so
/// returning all of them charges a reader with a choice that is not one. The
/// survivor is picked rather than taken arbitrarily because the pairs run
/// concurrently, and "whichever finished first" would not reproduce under a
/// fixed seed.
///
/// A timed-out implication check reads as "does not imply", so an equivalence
/// neither direction proves in time keeps both sides. The collapse is
/// best-effort in the same way the dominance sweep is.
///
/// The unordered pairwise checks run in parallel, but far fewer than n*(n-1)/2
/// of them actually run: structurally equal specs collapse to one
/// representative before the sweep, and a pair is skipped outright once either
/// endpoint is already known subsumed.
/// @p checker must be thread-safe (SatisfiabilityChecker satisfies this).
/// The checker is captured by reference; it must outlive the returned
/// filter.
///
/// @param checker      Satisfiability checker for pairwise implication tests
/// @param similarity   Ranks the members of an equivalence class; an empty
///                     std::function ranks them all equal, leaving
///                     `operator<` to choose
/// @param on_progress  Optional callback invoked after each batch of pairs is
///                     checked; receives (done, total) pair counts
template <typename Spec = Specification>
FilterFunctionT<Spec> make_implication_filter(
    SatisfiabilityChecker& checker, SimilarityKeyT<Spec> similarity = nullptr,
    const GenerationProgressCallback& on_progress = nullptr);
