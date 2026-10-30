#pragma once

#include <cassert>

#include "genetic/random_source.hpp"
#include "prop_formula.hpp"

// A uniformly drawn binary connective, for the FRETISH crossover and mutation
// operators. One draw. The TLSF operators draw from their own six-way set.
inline Formula::Kind pick_binary_kind(const RandomSource& random_source) {
    const int selector = static_cast<int>(random_source.next_index(4));
    switch (selector) {
        case 0:
            return Formula::Kind::And;
        case 1:
            return Formula::Kind::Or;
        case 2:
            return Formula::Kind::Implies;
        case 3:
            return Formula::Kind::Iff;
        default:
            assert(false);
            __builtin_unreachable();
    }
}
