#include "fingerprint/prefilter.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "filter/implication.hpp"
#include "formula_key.hpp"
#include "profile.hpp"
#include "runner/black.hpp"
#include "thread_pool.hpp"

namespace fingerprint::prefilter {

namespace {

// Process-wide, so the streaming filter, which redraws its table over the
// maximal set at every batch, asks black once per specification. A timeout
// is remembered as no model, or every batch would spend the budget again.
class ModelMemo {
   public:
    std::optional<std::optional<LassoModel>> find(const std::string& key) {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_models.find(key);
        if (found == m_models.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    // The first insertion under a key wins and is returned, so two spellings
    // of one formula read one model whichever black answered first.
    std::optional<LassoModel> insert(const std::string& key,
                                     const std::optional<LassoModel>& model) {
        const std::scoped_lock lock(m_mutex);
        return m_models.try_emplace(key, model).first->second;
    }

   private:
    std::mutex m_mutex;
    std::unordered_map<std::string, std::optional<LassoModel>> m_models;
};

ModelMemo& model_memo() {
    static ModelMemo memo;
    return memo;
}

// One model per lowering, in order. The misses are solved in parallel and
// stored in index order, so which spelling's model a shared key keeps does not
// depend on which black call finished first. No `RandomSource` is touched.
std::vector<std::optional<LassoModel>> models_of(
    const std::vector<std::string>& lowerings, TaskPriority priority) {
    std::vector<std::optional<LassoModel>> models(lowerings.size());
    std::vector<std::size_t> misses;
    for (std::size_t idx = 0; idx < lowerings.size(); ++idx) {
        std::optional<std::optional<LassoModel>> known =
            model_memo().find(formula_key::canonical(lowerings[idx]));
        if (known.has_value()) {
            models[idx] = std::move(*known);
        } else {
            misses.push_back(idx);
        }
    }
    std::vector<std::optional<LassoModel>> solved(misses.size());
    run_bounded_async(
        misses.size(), dispatch_window(),
        [&lowerings, &misses](std::size_t slot) {
            return [&formula = lowerings[misses[slot]]] {
                return black_model(formula, k_model_budget);
            };
        },
        [&solved](std::size_t slot, std::optional<LassoModel> model) {
            solved[slot] = std::move(model);
        },
        {}, priority);
    for (std::size_t slot = 0; slot < misses.size(); ++slot) {
        const std::size_t idx = misses[slot];
        models[idx] = model_memo().insert(
            formula_key::canonical(lowerings[idx]), solved[slot]);
    }
    return models;
}

// The two front ends differ only in where the signals and the lowering come
// from, and both have to draw from one word set, so the walk is written once.
template <typename Spec, typename Signals, typename Lower>
std::vector<PackedFingerprint> fingerprints_over(const std::vector<Spec>& specs,
                                                 Signals signals_of,
                                                 Lower lower,
                                                 TaskPriority priority) {
    PEREDUR_PROFILE_SCOPE("prefilter/fingerprints");
    if (specs.empty()) {
        return {};
    }
    const std::vector<std::string> signals = signals_of(specs.front());
    if (signals.empty()) {
        return {};
    }
    std::vector<std::string> lowerings;
    lowerings.reserve(specs.size());
    for (const Spec& spec : specs) {
        lowerings.push_back(lower(spec));
    }
    // The sampled bits first, which also proves every lowering reparses
    // before black is asked about any of them.
    const std::vector<LassoWord> sampled =
        sample_words(signals, k_words, k_seed, k_max_prefix, k_max_cycle);
    std::vector<PackedFingerprint> prints;
    prints.reserve(specs.size());
    for (const std::string& lowering : lowerings) {
        try {
            prints.push_back(pack(fingerprint_of(lowering, sampled)));
        } catch (const std::exception&) {
            return {};
        }
    }
    assert(prints.front().size() == k_sampled_elements);

    std::vector<LassoWord> model_words;
    for (const std::optional<LassoModel>& model :
         models_of(lowerings, priority)) {
        if (!model.has_value()) {
            continue;
        }
        std::optional<LassoWord> word = word_of_model(*model, signals);
        if (word.has_value()) {
            model_words.push_back(std::move(*word));
        }
    }
    ImplicationFilterStats::n_model_words.fetch_add(model_words.size(),
                                                    std::memory_order_relaxed);
    if (model_words.empty()) {
        return prints;
    }
    for (std::size_t idx = 0; idx < specs.size(); ++idx) {
        // Every lowering already parsed above, so this cannot throw.
        const PackedFingerprint tail =
            pack(fingerprint_of(lowerings[idx], model_words));
        prints[idx].insert(prints[idx].end(), tail.begin(), tail.end());
    }
    return prints;
}

}  // namespace

std::optional<LassoWord> word_of_model(
    const LassoModel& model, const std::vector<std::string>& signals) {
    const std::size_t n_states = model.m_states.size();
    if (n_states == 0 || n_states > k_max_positions ||
        model.m_loop >= n_states) {
        return std::nullopt;
    }
    LassoWord word;
    word.m_loop_start = model.m_loop;
    word.m_n_positions = n_states;
    for (const std::string& signal : signals) {
        word.m_signal_masks.emplace(signal, 0);
    }
    for (std::size_t pos = 0; pos < n_states; ++pos) {
        for (const auto& [atom, holds] : model.m_states[pos]) {
            word.m_signal_masks[atom] |= holds ? std::uint64_t{1} << pos : 0;
        }
    }
    return word;
}

std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<tlsf::Specification>& specs, TaskPriority priority) {
    return fingerprints_over(
        specs,
        [](const tlsf::Specification& spec) {
            std::vector<std::string> signals = spec.m_inputs;
            signals.insert(signals.end(), spec.m_outputs.begin(),
                           spec.m_outputs.end());
            return signals;
        },
        [](const tlsf::Specification& spec) { return spec.to_ltl(); },
        priority);
}

std::vector<PackedFingerprint> fingerprints_of(
    const std::vector<Specification>& specs, TaskPriority priority) {
    return fingerprints_over(
        specs,
        [](const Specification& spec) {
            std::vector<std::string> signals = environment_signals(spec);
            signals.insert(signals.end(), spec.m_out_atoms.begin(),
                           spec.m_out_atoms.end());
            return signals;
        },
        [](const Specification& spec) { return spec.to_ltl(); }, priority);
}

bool refuted_by_model_alone(const PackedFingerprint& lhs,
                            const PackedFingerprint& rhs) {
    assert(lhs.size() == rhs.size());
    const std::size_t sampled = std::min(lhs.size(), k_sampled_elements);
    for (std::size_t element = 0; element < sampled; ++element) {
        if ((lhs[element] & ~rhs[element]) != 0) {
            return false;
        }
    }
    for (std::size_t element = sampled; element < lhs.size(); ++element) {
        if ((lhs[element] & ~rhs[element]) != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace fingerprint::prefilter
