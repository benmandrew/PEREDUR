#pragma once

/// @file prefilter.hpp
/// @brief The sampled-word prefilter every implication sweep uses.
///
/// One definition of the sampling, because two fingerprints are only a test
/// of implication against each other when they were drawn from the same
/// words. The pairwise sweeps in filter.cpp on both paths and the equivalence
/// quotient in maximal.cpp all prefilter, and their fingerprints have to
/// agree.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fingerprint/lasso.hpp"
#include "requirement.hpp"
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

/// One fingerprint per entry of @p specs, in order.
///
/// Empty where the specifications declare no signals to sample over, or where
/// one lowering is not a string this codebase's own parser reads back. Losing
/// the whole table costs speed and nothing else, where a partial one would
/// need a per-entry guard at every use.
std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<tlsf::Specification>& specs);

/// The FRETISH twin. Modes join the sampled signals: a scoped requirement's
/// mode is an atom of the lowered formula that may sit in no atom list
/// (`Specification::m_modes`), and a signal no word names is false at every
/// position, so leaving it out would evaluate every scope against a trace
/// where its mode never holds.
std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<Specification>& specs);

}  // namespace fingerprint::prefilter
