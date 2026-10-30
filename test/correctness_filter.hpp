#pragma once

// The per-generation filter for one correctness check, built the way
// get_filter_functions builds it: from the check of that name in
// correctness_checks, through make_predicate_filter. A test of a filter
// therefore tests the one the search runs.

#include <string>

#include "filter/correctness.hpp"
#include "genetic/generation.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "test_support.hpp"

template <typename Spec = Specification>
FilterFunctionT<Spec> correctness_filter(const std::string& name,
                                         SatisfiabilityChecker& sat,
                                         RealizabilityChecker& real) {
    for (const CorrectnessCheckT<Spec>& check :
         correctness_checks<Spec>(sat, real)) {
        if (check.name == name) {
            return make_predicate_filter<Spec>(check.name, check.admissible);
        }
    }
    fail("correctness_filter: no correctness check is named " + name);
}
