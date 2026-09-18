#include "filter/implication.hpp"

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/antichain.hpp"
#include "filter/implication_check.hpp"
#include "fitness/syntactic_similarity.hpp"
#include "requirement.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/specification.hpp"

namespace {

// The per-front-end halves of the templates below. The FRETISH check counts
// its timeouts in ImplicationFilterStats and the TLSF one does not.
std::optional<bool> implies(const Specification& from,
                            const Specification& dest,
                            SatisfiabilityChecker& checker) {
    return spec_implies(from, dest, checker);
}

std::optional<bool> implies(const tlsf::Specification& from,
                            const tlsf::Specification& dest,
                            SatisfiabilityChecker& checker) {
    return tlsf_spec_implies(from, dest, checker);
}

double similarity_to(const Specification& spec, const Specification& original,
                     const Config& cfg) {
    return syntactic_similarity(spec, original, cfg);
}

double similarity_to(const tlsf::Specification& spec,
                     const tlsf::Specification& original, const Config& cfg) {
    return tlsf_syntactic_similarity(spec, original, cfg);
}

}  // namespace

template <typename Spec>
FilterFunctionT<Spec> make_dedup_filter() {
    return {"dedup",
            [](std::vector<Spec> pop) {
                std::unordered_set<Spec> seen;
                seen.reserve(pop.size());
                std::vector<Spec> survivors;
                survivors.reserve(pop.size());
                for (Spec& spec : pop) {
                    // The set has to own a copy to key on; the survivor is
                    // then moved, so a kept candidate costs one copy rather
                    // than two.
                    if (seen.insert(spec).second) {
                        survivors.push_back(std::move(spec));
                    }
                }
                return survivors;
            },
            FilterKind::Preference};
}

template <typename Spec>
FilterFunctionT<Spec> make_implication_filter(
    SatisfiabilityChecker& checker, SimilarityKeyT<Spec> similarity,
    const GenerationProgressCallback& on_progress) {
    return {"implication",
            [&checker, similarity = std::move(similarity),
             on_progress](std::vector<Spec> pop) {
                antichain::reset_stats();
                if (pop.size() <= 1) {
                    return pop;
                }
                const std::vector<uint8_t> subsumed = antichain::subsumed_in(
                    pop,
                    [&checker](const Spec& lhs, const Spec& rhs) {
                        return implies(lhs, rhs, checker).value_or(false);
                    },
                    similarity, on_progress);
                std::vector<Spec> maximal;
                for (std::size_t i = 0; i < pop.size(); ++i) {
                    if (subsumed[i] == 0U) {
                        maximal.push_back(std::move(pop[i]));
                    }
                }
                return maximal;
            },
            FilterKind::Preference};
}

template <typename Spec>
SimilarityKeyT<Spec> syntactic_similarity_key(Spec original,
                                              const Config& cfg) {
    // Both captured by value: the returned key outlives this call, and the
    // filters it goes into are held for the whole run.
    return [original = std::move(original), cfg](const Spec& spec) mutable {
        return similarity_to(spec, original, cfg);
    };
}

template FilterFunctionT<Specification> make_dedup_filter<Specification>();
template FilterFunctionT<tlsf::Specification>
make_dedup_filter<tlsf::Specification>();
template FilterFunctionT<Specification> make_implication_filter<Specification>(
    SatisfiabilityChecker&, SimilarityKeyT<Specification>,
    const GenerationProgressCallback&);
template FilterFunctionT<tlsf::Specification>
make_implication_filter<tlsf::Specification>(
    SatisfiabilityChecker&, SimilarityKeyT<tlsf::Specification>,
    const GenerationProgressCallback&);
template SimilarityKeyT<Specification> syntactic_similarity_key<Specification>(
    Specification, const Config&);
template SimilarityKeyT<tlsf::Specification>
syntactic_similarity_key<tlsf::Specification>(tlsf::Specification,
                                              const Config&);
