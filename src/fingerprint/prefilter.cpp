#include "fingerprint/prefilter.hpp"

#include <exception>
#include <string>
#include <vector>

namespace fingerprint::prefilter {

namespace {

// The two front ends differ only in where the signals and the lowering come
// from, and both have to draw from one word set, so the walk is written once.
template <typename Spec, typename Signals, typename Lower>
std::vector<PackedFingerprint> fingerprints_over(const std::vector<Spec>& specs,
                                                 const Sampling& sampling,
                                                 Signals signals_of,
                                                 Lower lower) {
    if (specs.empty()) {
        return {};
    }
    const std::vector<std::string> signals = signals_of(specs.front());
    if (signals.empty()) {
        return {};
    }
    const std::vector<LassoWord> words =
        sample_words(signals, sampling.m_words, sampling.m_seed,
                     sampling.m_max_prefix, sampling.m_max_cycle);
    std::vector<PackedFingerprint> prints;
    prints.reserve(specs.size());
    for (const Spec& spec : specs) {
        try {
            prints.push_back(pack(fingerprint_of(lower(spec), words)));
        } catch (const std::exception&) {
            return {};
        }
    }
    return prints;
}

}  // namespace

std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<tlsf::Specification>& specs, const Sampling& sampling) {
    return fingerprints_over(
        specs, sampling,
        [](const tlsf::Specification& spec) {
            std::vector<std::string> signals = spec.m_inputs;
            signals.insert(signals.end(), spec.m_outputs.begin(),
                           spec.m_outputs.end());
            return signals;
        },
        [](const tlsf::Specification& spec) { return spec.to_ltl_formula(); });
}

std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<Specification>& specs, const Sampling& sampling) {
    return fingerprints_over(
        specs, sampling,
        [](const Specification& spec) {
            std::vector<std::string> signals = environment_signals(spec);
            signals.insert(signals.end(), spec.m_out_atoms.begin(),
                           spec.m_out_atoms.end());
            return signals;
        },
        [](const Specification& spec) { return spec.to_ltl(); });
}

}  // namespace fingerprint::prefilter
