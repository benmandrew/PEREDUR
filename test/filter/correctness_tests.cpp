#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "filter/correctness.hpp"
#include "genetic/generation.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"
#include "tlsf/evolve.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

std::vector<std::string> correctness_stage_names(
    const std::vector<FilterFunction>& filters) {
    std::vector<std::string> names;
    for (const FilterFunction& filter : filters) {
        if (filter.kind() == FilterKind::Correctness) {
            names.push_back(filter.name());
        }
    }
    return names;
}

std::vector<std::string> tlsf_correctness_stage_names(
    const std::vector<FilterFunctionT<tlsf::Specification>>& filters) {
    std::vector<std::string> names;
    for (const FilterFunctionT<tlsf::Specification>& filter : filters) {
        if (filter.kind() == FilterKind::Correctness) {
            names.push_back(filter.name());
        }
    }
    return names;
}

template <typename Spec>
bool has_check_named(const std::vector<CorrectnessCheckT<Spec>>& checks,
                     const std::string& name) {
    return std::any_of(checks.begin(), checks.end(),
                       [&name](const CorrectnessCheckT<Spec>& check) {
                           return check.name == name;
                       });
}

Specification fretish_spec() {
    return Specification(
        {},
        {Requirement(Formula("true"), Formula("grant"), timing::immediately())},
        {"req"}, {"grant"});
}

tlsf::Specification tlsf_spec(const std::string& main_body) {
    return tlsf::parse("INFO { SEMANTICS: Mealy; }\nMAIN {\n" + main_body +
                       "\n}\n");
}

// The tripwire. Every correctness stage the generation chain runs must have a
// row in the table the final gate reads, or the property is enforced during the
// search and abandoned at the output -- issue #73, which reached the output
// through elites and the seed population.
//
// One direction only, deliberately. A row without a stage is a property checked
// at the gate alone, which is a supported configuration (well-separation's row
// is one) and is where a future change may deliberately move a check.
void test_every_correctness_stage_has_a_gate_check() {
    const Config cfg;
    const Specification original = fretish_spec();
    const std::vector<std::string> stages = correctness_stage_names(
        get_filter_functions(original, global_sat_checker()));
    expect(!stages.empty(),
           "correctness: the generation chain should run correctness "
           "stages");
    const std::vector<CorrectnessCheck> checks =
        correctness_checks(global_sat_checker(), global_real_checker());
    for (const std::string& stage : stages) {
        expect(has_check_named(checks, stage),
               "correctness: generation stage '" + stage +
                   "' has no matching check in correctness_checks, so nothing "
                   "re-screens it before output");
    }
}

void test_every_tlsf_correctness_stage_has_a_gate_check() {
    const Config cfg;
    const tlsf::Specification original =
        tlsf_spec("INPUTS { a; } OUTPUTS { b; } GUARANTEE { G (a -> b); }");
    const std::vector<std::string> stages = tlsf_correctness_stage_names(
        tlsf::internal::build_per_gen_filters(original));
    expect(!stages.empty(),
           "correctness: the TLSF generation chain should run correctness "
           "stages");
    const std::vector<CorrectnessCheckT<tlsf::Specification>> checks =
        correctness_checks<tlsf::Specification>(global_sat_checker(),
                                                global_real_checker());
    for (const std::string& stage : stages) {
        expect(has_check_named(checks, stage),
               "correctness: TLSF generation stage '" + stage +
                   "' has no matching check in the TLSF correctness_checks");
    }
}

// The two paths' tables must agree on names as well as on content: the
// dashboard, the filter report and run_experiments.py all key on the stage
// name, and a repair is screened by whichever table its front end reads.
void test_both_paths_name_the_same_checks_in_the_same_order() {
    const std::vector<CorrectnessCheck> fretish =
        correctness_checks(global_sat_checker(), global_real_checker());
    const std::vector<CorrectnessCheckT<tlsf::Specification>> tlsf_checks =
        correctness_checks<tlsf::Specification>(global_sat_checker(),
                                                global_real_checker());
    expect(fretish.size() == tlsf_checks.size(),
           "correctness: both paths should carry the same number of checks");
    for (std::size_t idx = 0; idx < fretish.size(); ++idx) {
        expect(fretish[idx].name == tlsf_checks[idx].name,
               "correctness: check " + std::to_string(idx) +
                   " should carry the same name on both paths");
    }
}

// Well-separation goes last. It is the one check that can be cold at the gate:
// realizability is a scored objective and vacuity's queries are keyed per
// requirement, but nothing warms an ltlsynt well-separation query when its
// per-generation stage is off.
void test_well_separation_is_the_last_check() {
    const std::vector<CorrectnessCheck> checks =
        correctness_checks(global_sat_checker(), global_real_checker());
    expect(!checks.empty() && checks.back().name == "not-well-separated",
           "correctness: not-well-separated should be the last check, so a "
           "rejected candidate never pays for its query");
}

// A row without a per-generation stage still sits in the table, which is what
// makes the gate unconditional: the stage is search pressure, never output
// correctness. Well-separation is that row.
void test_gate_only_row_has_no_stage_but_keeps_its_check() {
    const Config cfg;
    const Specification original = fretish_spec();
    const std::vector<std::string> stages = correctness_stage_names(
        get_filter_functions(original, global_sat_checker()));
    expect(std::find(stages.begin(), stages.end(), "not-well-separated") ==
               stages.end(),
           "correctness: well-separation should run no per-generation stage");
    expect(has_check_named(
               correctness_checks(global_sat_checker(), global_real_checker()),
               "not-well-separated"),
           "correctness: the gate should still carry the well-separation "
           "check");
}

// The gate's own predicate, on the case that reaches it: a specification that
// is realizable only because the system can defeat its own assumption. It is
// not vacuous -- `G b` is satisfiable and `G (a -> b)` is not valid -- so the
// vacuity check keeps it and only well-separation rejects it.
void test_gate_rejects_a_not_well_separated_specification() {
    const tlsf::Specification spec = tlsf_spec(
        "INPUTS { a; } OUTPUTS { b; } ASSUME { G b; } "
        "GUARANTEE { G (a -> b); }");
    const std::vector<CorrectnessCheckT<tlsf::Specification>> checks =
        correctness_checks<tlsf::Specification>(global_sat_checker(),
                                                global_real_checker());
    const std::optional<std::string> failed = first_failing_check(spec, checks);
    expect(failed.has_value() && *failed == "not-well-separated",
           "correctness: a specification the system can satisfy by forcing its "
           "own assumption to fail should be rejected by the gate, and by the "
           "well-separation check specifically");
}

void test_gate_keeps_a_well_separated_specification() {
    const tlsf::Specification spec = tlsf_spec(
        "INPUTS { a; } OUTPUTS { b; } ASSUME { G F a; } "
        "GUARANTEE { G (a -> b); }");
    const std::vector<CorrectnessCheckT<tlsf::Specification>> checks =
        correctness_checks<tlsf::Specification>(global_sat_checker(),
                                                global_real_checker());
    expect(!first_failing_check(spec, checks).has_value(),
           "correctness: a specification with an input-only assumption should "
           "pass every gate check");
}

}  // namespace

void run_correctness_tests() {
    test_every_correctness_stage_has_a_gate_check();
    test_every_tlsf_correctness_stage_has_a_gate_check();
    test_both_paths_name_the_same_checks_in_the_same_order();
    test_well_separation_is_the_last_check();
    test_gate_only_row_has_no_stage_but_keeps_its_check();
    test_gate_rejects_a_not_well_separated_specification();
    test_gate_keeps_a_well_separated_specification();
}
