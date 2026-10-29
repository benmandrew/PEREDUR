#pragma once

/// @file prefilter.hpp
/// @brief The sampled-word prefilter every implication sweep uses.
///
/// One definition of the sampling, because two fingerprints are only a test
/// of implication against each other when they were drawn from the same
/// words. The pairwise sweeps in filter.cpp on both paths and the equivalence
/// quotient in maximal.cpp all prefilter, and their fingerprints have to
/// agree.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fingerprint/lasso.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "thread_pool.hpp"
#include "tlsf/specification.hpp"

namespace fingerprint::prefilter {

/// Not a config key, deliberately: the prefilter refutes only pairs that are
/// genuinely non-implications, so the relation it computes is the one the
/// solver would have computed and there is no behaviour to gate. It is the
/// `run_bounded_async` launch-order argument again — a change that moves cost
/// and no output has one code path.
///
/// 256 words at a lasso of at most five positions refuted 95.9% of the ordered
/// pairs of a 421-candidate round-robin-arbiter-aurus set, leaving 4.07% for
/// the solver. Raising the count buys a tighter filter at a cost linear in it
/// and paid once per specification, against a quadratic number of pairs.
constexpr std::size_t k_words = 256;
constexpr std::uint64_t k_seed = 0;
constexpr std::size_t k_max_prefix = 2;
constexpr std::size_t k_max_cycle = 3;

/// The packed elements the sampled words fill. The model words follow them,
/// so the sampled bits of a fingerprint are the ones they were before model
/// words existed, and a whole element is needed for that.
constexpr std::size_t k_sampled_elements = k_words / 64;
static_assert(k_words % 64 == 0, "model words must start a fresh element");

/// black's budget for each specification's model. A model came back in a
/// median 22 ms on lift-plus-cruise-mini, but at a load average of 25 a 500 ms
/// budget lost up to 5 of its 70 models. black's own `-t` rounds up to whole
/// seconds, so this is the shortest budget both deadlines agree on. A model
/// that runs past it costs its word and nothing else, and the calls run in
/// parallel, once per specification.
constexpr std::chrono::milliseconds k_model_budget{1000};

/// @p model as a word over @p signals and the atoms the model names. An atom
/// the model leaves unconstrained at a state, black's `undef`, is false there,
/// as is every signal it never names: the model holds whatever those atoms
/// do, so any choice keeps it a model. Nullopt where the model is longer than
/// a word can be (`k_max_positions`).
std::optional<LassoWord> word_of_model(const LassoModel& model,
                                       const std::vector<std::string>& signals);

/// One fingerprint per entry of @p specs, in order.
///
/// Each fingerprint is the sampled words' bits followed by one bit for each
/// specification black found a model of within `k_model_budget`, in the order
/// of @p specs. Random words violate some guarantee of a specification that
/// constrains most of its signals at every step, so they refute nothing
/// there; a model of one candidate is a word that candidate accepts by
/// construction. Two fingerprints therefore compare only when drawn by one
/// call, over one list of specifications. Models are memoised on
/// `formula_key::canonical`, since a model names atoms, and black is asked in
/// parallel on @p priority's queue.
///
/// Empty where the specifications declare no signals to sample over, or where
/// one lowering is not a string this codebase's own parser reads back. Losing
/// the whole table costs speed and nothing else, where a partial one would
/// need a per-entry guard at every use.
std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<tlsf::Specification>& specs,
    TaskPriority priority = TaskPriority::Foreground);

/// The FRETISH twin. Modes join the sampled signals: a scoped requirement's
/// mode is an atom of the lowered formula that may sit in no atom list
/// (`Specification::m_modes`), and a signal no word names is false at every
/// position, so leaving it out would evaluate every scope against a trace
/// where its mode never holds.
std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<Specification>& specs,
    TaskPriority priority = TaskPriority::Foreground);

/// True when a model word refutes `lhs -> rhs` and no sampled word does: the
/// pairs the model words alone took from the solver.
bool refuted_by_model_alone(const PackedFingerprint& lhs,
                            const PackedFingerprint& rhs);

}  // namespace fingerprint::prefilter
