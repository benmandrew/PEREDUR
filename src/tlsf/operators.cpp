#include "tlsf/operators.hpp"

#include <cstddef>
#include <vector>

#include "config.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "tlsf/crossover.hpp"
#include "tlsf/mutation.hpp"

tlsf::Specification tlsf_simplify(tlsf::Specification spec) {
    for (tlsf::Section* section : tlsf::mutable_sections_of(spec)) {
        for (tlsf::SectionEntry& entry : *section) {
            // Deleted conjuncts are left as they are. Nothing reads their
            // content again, so rewriting it buys nothing.
            if (entry.m_removed) {
                continue;
            }
            entry.m_formula.simplify();
        }
    }
    return spec;
}

const GeneticOperators<tlsf::Specification>& tlsf_operators() {
    static const GeneticOperators<tlsf::Specification> ops{
        [](const tlsf::Specification& parent_a,
           const tlsf::Specification& parent_b,
           const RandomSource& random_source, const Config&) {
            return tlsf_crossover(parent_a, parent_b, random_source);
        },
        [](const tlsf::Specification& spec, const RandomSource& random_source,
           const Config& cfg, bool /*parent_realizable*/) {
            return tlsf_mutate(spec, random_source, cfg);
        },
        tlsf_simplify};
    return ops;
}
