#include <cmath>
#include <string>
#include <string_view>

#include "config.hpp"
#include "fitness/status.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_fitness";

tlsf::Specification parse(const std::string& main_body,
                          const std::string& semantics = "Mealy") {
    return tlsf::parse("INFO { SEMANTICS: " + semantics + "; }\nMAIN {\n" +
                       main_body + "\n}\n");
}

TEST(test_syntactic_self_similarity_is_one) {
    const Config cfg;
    const tlsf::Specification spec =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(r -> F g); }");
    expect(tlsf_syntactic_similarity(spec, spec, cfg) == 1.0,
           "syntactic: a spec is syntactically identical to itself");
}

TEST(test_semantic_self_similarity_is_one) {
    Config cfg;
    cfg.default_model_counting_bound = 4;
    const tlsf::Specification spec =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(r -> F g); }");
    expect(tlsf_semantic_similarity(spec, spec, cfg) == 1.0,
           "semantic: identical section formulae contribute nothing, so self "
           "similarity is 1.0");
}

// The metric config must reach the TLSF path, not only the FRETISH one. A
// non-equivalent guarantee pair (G(r -> g) strictly implies G(r -> F g)) scores
// differently under the two metrics, so their cross-scores must diverge -- were
// the config ignored, the TLSF path would stay on direct and the two would be
// identical.
TEST(test_semantic_similarity_honours_configured_metric) {
    Config cfg;
    cfg.default_model_counting_bound = 4;
    const tlsf::Specification original =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(r -> F g); }");
    const tlsf::Specification candidate =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(r -> g); }");

    cfg.similarity_metric = SimilarityMetric::Direct;
    const double direct = tlsf_semantic_similarity(candidate, original, cfg);
    cfg.similarity_metric = SimilarityMetric::Logarithmic;
    const double logarithmic =
        tlsf_semantic_similarity(candidate, original, cfg);

    expect(direct >= 0.0 && direct <= 1.0 && logarithmic >= 0.0 &&
               logarithmic <= 1.0,
           "semantic: both metrics stay within [0, 1] on the TLSF path");
    expect(std::fabs(direct - logarithmic) > 1e-9,
           "semantic: the configured metric must reach the TLSF path, so the "
           "direct and logarithmic scores diverge for a non-equivalent pair");
}

TEST(test_status_realizable_is_one) {
    const Config cfg;
    const tlsf::Specification spec =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(g <-> r); }");
    expect(tlsf_status(spec, cfg) == 1.0,
           "status: a realizable spec scores 1.0");
}

TEST(test_status_individually_unsatisfiable_formula_is_zero) {
    // One section formula that cannot hold on its own: the component tier.
    const Config cfg;
    const tlsf::Specification spec =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { g & !g; }");
    expect(tlsf_status(spec, cfg) == k_status_component_unsatisfiable,
           "status: an individually unsatisfiable section formula scores the "
           "component tier");
}

TEST(test_status_jointly_unsatisfiable_guarantees_are_unrealizable) {
    // `g` and `!g` are each satisfiable alone, so no component tier fires.
    // Their conjunction is not realizable, which the realizability query
    // reports -- the guarantee-side satisfiability tier this used to occupy
    // was dropped as unpopulated.
    const Config cfg;
    const tlsf::Specification spec =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { g; !g; }");
    expect(tlsf_status(spec, cfg) == k_status_unrealizable,
           "status: jointly contradictory guarantees score the unrealizable "
           "tier");
}

// The ladder is a shared scale, so this pins that the TLSF front end reaches
// every level of it rather than re-testing status_score_aurus. The sides are
// assumption_ltl() against guarantee_ltl(), which are AuRUS's own environment
// and system formulae, so a side is unsatisfiable exactly where its own
// conjuncts contradict.
TEST(test_status_aurus_grades_by_which_side_survives) {
    Config cfg;
    cfg.status_grading = StatusGrading::Aurus;
    const std::string atoms = "INPUTS { r; } OUTPUTS { g; } ";
    expect(tlsf_status(parse(atoms + "ASSUME { r; !r; } GUARANTEE { g; !g; }"),
                       cfg) == k_status_component_unsatisfiable,
           "aurus: neither side satisfiable scores the bottom level");
    expect(tlsf_status(
               parse(atoms + "ASSUME { r; !r; } GUARANTEE { G(g <-> r); }"),
               cfg) == k_status_aurus_guarantees_only,
           "aurus: only the guarantee side satisfiable scores 0.05");
    expect(tlsf_status(parse(atoms + "GUARANTEE { g; !g; }"), cfg) ==
               k_status_aurus_assumptions_only,
           "aurus: only the assumption side satisfiable scores 0.1");
    expect(tlsf_status(parse(atoms + "ASSUME { G r; } GUARANTEE { G !r; }"),
                       cfg) == k_status_aurus_contradictory,
           "aurus: two sides that hold alone but not together score 0.2");
    expect(tlsf_status(parse(atoms + "GUARANTEE { G(g <-> r); }"), cfg) ==
               k_status_realizable,
           "aurus: a realizable spec scores 1.0");
}

// The divergence, on the TLSF path: `ASSUME { G F g }` is over the system's own
// output, so the system defeats it by never asserting g. The shared scale caps
// that at the unrealizable tier; the AuRUS ladder does not ask the question,
// because AuRUS does not.
TEST(test_status_aurus_does_not_penalise_an_ill_separated_spec) {
    Config cfg;
    const tlsf::Specification spec = parse(
        "INPUTS { r; } OUTPUTS { g; } ASSUME { G F g; } "
        "GUARANTEE { G (r -> g); }");
    cfg.status_grading = StatusGrading::Tiered;
    expect(tlsf_status(spec, cfg) == k_status_unrealizable,
           "aurus: the tiered scale caps an ill-separated spec at 0.5");
    cfg.status_grading = StatusGrading::Aurus;
    expect(tlsf_status(spec, cfg) == k_status_realizable,
           "aurus: the AuRUS ladder scores an ill-separated but realizable "
           "spec 1.0");
}

TEST(test_aggregate_scores_in_unit_interval) {
    Config cfg;
    cfg.default_model_counting_bound = 4;
    const tlsf::Specification spec =
        parse("INPUTS { r; } OUTPUTS { g; } GUARANTEE { G(g <-> r); }");
    const auto fitness = tlsf_get_fitness_function(spec, cfg);
    expect(!fitness.empty(),
           "aggregate: default weights yield a non-empty fitness function");
    const double score = fitness(spec);
    expect(score >= 0.0 && score <= 1.0,
           "aggregate: the weighted score stays within [0, 1]");
}

}  // namespace
