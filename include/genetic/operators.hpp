#pragma once

/// @file operators.hpp
/// @brief The GeneticOperators<Spec> bundle of crossover, mutation, and
///        simplification callables injected into the generic evolution loop.

#include <functional>

#include "config.hpp"
#include "genetic/random_source.hpp"

/// Bundles the per-element genetic operators the evolution loop applies to
/// offspring. @c simplify may be empty, in which case it is treated as the
/// identity. @c mutate also takes whether the slot's parent scored realizable,
/// which is the parent's verdict and not the offspring's: a crossover ahead of
/// the mutation may have changed it, and nothing is scored in between.
template <typename Spec>
struct GeneticOperators {
    std::function<Spec(const Spec&, const Spec&, const RandomSource&,
                       const Config&)>
        crossover;
    std::function<Spec(const Spec&, const RandomSource&, const Config&, bool)>
        mutate;
    std::function<Spec(Spec)> simplify;
};
