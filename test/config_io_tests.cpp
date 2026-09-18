#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "config_io.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

Config config_capturing_warnings(const std::string& toml,
                                 std::string& warnings) {
    std::ostringstream captured;
    std::streambuf* const previous = std::cerr.rdbuf(captured.rdbuf());
    try {
        Config cfg = config_from_toml_string(toml);
        std::cerr.rdbuf(previous);
        warnings = captured.str();
        return cfg;
    } catch (...) {
        std::cerr.rdbuf(previous);
        warnings = captured.str();
        throw;
    }
}

std::string warnings_from(const std::string& toml) {
    std::string warnings;
    config_capturing_warnings(toml, warnings);
    return warnings;
}

void test_config_io_all_fields() {
    const std::string toml = R"(
[genetic]
generations     = 5
population_size = 100
crossover_rate  = 0.2
mutation_rate   = 0.8
accumulate_repairs = false

[fitness]
weight_syntactic = 0.3
weight_semantic  = 0.4
weight_status    = 0.6
status_grading      = "mrs"
mrs_admission_order = "degree"

[mutation]
p_trigger  = 0.3
p_response = 0.7
p_timing   = 0.1

[model_counting]
default_bound = 10

[filters]
run_implication = false

[runtime]
black_timeout_ms = 500
parallel         = 4
dashboard        = true
)";
    const Config cfg = config_from_toml_string(toml);
    expect(cfg.generations == 5,
           "config_io: generations should be parsed from TOML");
    expect(cfg.population_size == 100,
           "config_io: population_size should be parsed from TOML");
    expect(cfg.crossover_rate == 0.2,
           "config_io: crossover_rate should be parsed from TOML");
    expect(cfg.mutation_rate == 0.8,
           "config_io: mutation_rate should be parsed from TOML");
    expect(!cfg.accumulate_repairs,
           "config_io: accumulate_repairs should be parsed from TOML");
    expect(Config{}.accumulate_repairs,
           "config_io: accumulate_repairs defaults on since 2026-08-25; "
           "every archived config omits the key and now means something "
           "new, which the config vintage note records");
    expect(cfg.fitness_weight_syntactic == 0.3,
           "config_io: fitness weight_syntactic should be parsed from TOML");
    expect(cfg.fitness_weight_semantic == 0.4,
           "config_io: fitness weight_semantic should be parsed from TOML");
    expect(cfg.fitness_weight_status == 0.6,
           "config_io: fitness weight_status should be parsed from TOML");
    expect(cfg.p_trigger == 0.3,
           "config_io: mutation p_trigger should be parsed from TOML");
    expect(cfg.p_response == 0.7,
           "config_io: mutation p_response should be parsed from TOML");
    expect(cfg.p_timing == 0.1,
           "config_io: mutation p_timing should be parsed from TOML");
    expect(cfg.default_model_counting_bound == 10,
           "config_io: model_counting.default_bound should be parsed");
    expect(!cfg.run_implication_filter,
           "config_io: filters run_implication should be false");
    expect(cfg.black_timeout == std::chrono::milliseconds{500},
           "config_io: runtime black_timeout_ms should be parsed from TOML");
    expect(cfg.parallel == 4,
           "config_io: runtime parallel should be parsed from TOML");
    expect(cfg.dashboard,
           "config_io: runtime dashboard should be parsed from TOML");
    expect(!Config{}.dashboard,
           "config_io: the dashboard should be opt-in, so a config that does "
           "not mention it leaves progress output off");
}

void test_config_io_partial_overrides_defaults() {
    const std::string toml = R"(
[genetic]
generations = 5
)";
    const Config cfg = config_from_toml_string(toml);
    const Config defaults;
    expect(cfg.generations == 5,
           "config_io: partial TOML should override only specified fields");
    expect(cfg.population_size == defaults.population_size,
           "config_io: unspecified population_size should remain default");
    expect(cfg.crossover_rate == defaults.crossover_rate,
           "config_io: unspecified crossover_rate should remain default");
}

void test_config_io_missing_file_throws() {
    bool threw = false;
    try {
        config_from_toml("/tmp/peredur_test_nonexistent_config.toml");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("does not exist") != std::string::npos,
               "config_io: missing file error should mention 'does not exist'");
    }
    expect(threw, "config_io: missing file should throw");
}

void test_config_io_invalid_toml_throws() {
    bool threw = false;
    try {
        config_from_toml_string("this is not valid toml ===");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "config_io: invalid TOML should throw");
}

void test_config_io_out_of_range_probability_throws() {
    bool threw = false;
    try {
        config_from_toml_string("[mutation]\np_trigger = 1.5\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("p_trigger") != std::string::npos,
               "config_io: out-of-range error should name the field");
    }
    expect(threw, "config_io: out-of-range probability should throw");
}

void test_config_io_elitism_rate_parsed() {
    const Config cfg = config_from_toml_string(
        "[genetic]\nselection_rate = 0.6\nelitism_rate = 0.2\n");
    expect(cfg.elitism_rate == 0.2,
           "config_io: genetic.elitism_rate should be parsed from TOML");
}

void test_config_io_elitism_not_less_than_selection_throws() {
    bool threw = false;
    try {
        config_from_toml_string(
            "[genetic]\nselection_rate = 0.3\nelitism_rate = 0.3\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("elitism_rate") != std::string::npos,
               "config_io: constraint error should name elitism_rate");
    }
    expect(threw,
           "config_io: elitism_rate not less than selection_rate should throw");
}

// Pinned because the default is what an archived config inherits wherever it
// states no scheme. It has moved three times -- WeightedAverage, Nsga2 (renamed
// Nsga2Truncate), and Nsga2Apportion from 2026-08-14 -- and only the 64
// `sweep_B_pop50_takeoff_*` configs under 2026-07-13-fretish-sweeps are
// exposed.
// See "Config vintage" in experiments/README.md before moving it again.
void test_config_io_selection_scheme_defaults_to_nsga2_apportion() {
    const Config cfg = config_from_toml_string("");
    expect(cfg.selection_scheme == SelectionScheme::Nsga2Apportion,
           "config_io: selection_scheme should default to Nsga2Apportion");
}

// Pinned because the default is what every archived config inherits: the key
// did not exist before 2026-08-11, so no archived config can state it, and
// moving this back to Tiered silently re-reads every one of them under a
// different status objective. See "Config vintage" in experiments/README.md.
void test_config_io_status_grading_defaults_to_mrs() {
    const Config cfg = config_from_toml_string("");
    expect(cfg.status_grading == StatusGrading::Mrs,
           "config_io: status_grading should default to Mrs");
}

void test_config_io_status_grading_tiered_parsed() {
    const Config cfg =
        config_from_toml_string("[fitness]\nstatus_grading = \"tiered\"\n");
    expect(cfg.status_grading == StatusGrading::Tiered,
           "config_io: status_grading = \"tiered\" should parse as Tiered");
}

void test_config_io_status_grading_mrs_parsed() {
    const Config cfg =
        config_from_toml_string("[fitness]\nstatus_grading = \"mrs\"\n");
    expect(cfg.status_grading == StatusGrading::Mrs,
           "config_io: status_grading = \"mrs\" should parse as Mrs");
}

void test_config_io_mrs_admission_order_defaults_to_degree() {
    const Config cfg;
    expect(cfg.mrs_admission_order == MrsAdmissionOrder::Degree,
           "config_io: mrs_admission_order should default to Degree");
}

void test_config_io_mrs_admission_order_spec_parsed() {
    const Config cfg =
        config_from_toml_string("[fitness]\nmrs_admission_order = \"spec\"\n");
    expect(cfg.mrs_admission_order == MrsAdmissionOrder::Spec,
           "config_io: mrs_admission_order = \"spec\" should parse as Spec");
}

void test_config_io_mrs_admission_order_degree_parsed() {
    const Config cfg = config_from_toml_string(
        "[fitness]\nmrs_admission_order = \"degree\"\n");
    expect(cfg.mrs_admission_order == MrsAdmissionOrder::Degree,
           "config_io: mrs_admission_order = \"degree\" should parse as "
           "Degree");
}

void test_config_io_mrs_admission_order_rejects_unknown() {
    bool threw = false;
    try {
        config_from_toml_string(
            "[fitness]\nmrs_admission_order = \"rotate\"\n");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw,
           "config_io: an unknown mrs_admission_order should be rejected");
}

void test_config_io_status_grading_aurus_parsed() {
    const Config cfg =
        config_from_toml_string("[fitness]\nstatus_grading = \"aurus\"\n");
    expect(cfg.status_grading == StatusGrading::Aurus,
           "config_io: status_grading = \"aurus\" should parse as Aurus");
}

void test_config_io_status_grading_rejects_unknown() {
    bool threw = false;
    try {
        config_from_toml_string("[fitness]\nstatus_grading = \"greedy\"\n");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "config_io: an unknown status_grading should be rejected");
}

// Pinned because every archived config omits these keys and inherits whatever
// they mean. Generations with no caps is what a run did before they existed, so
// the defaults have to keep reproducing it.
void test_config_io_termination_defaults_to_generations() {
    const Config cfg = config_from_toml_string("");
    expect(cfg.termination == TerminationMode::Generations,
           "config_io: termination should default to Generations");
    expect(cfg.max_individuals == 0,
           "config_io: max_individuals should default to 0");
    expect(cfg.max_wall_s == 0, "config_io: max_wall_s should default to 0");
}

void test_config_io_termination_individuals_parsed() {
    const Config cfg = config_from_toml_string(
        "[genetic]\ntermination = \"individuals\"\nmax_individuals = 1000\n");
    expect(cfg.termination == TerminationMode::Individuals,
           "config_io: termination = \"individuals\" should parse as "
           "Individuals");
    expect(cfg.max_individuals == 1000,
           "config_io: max_individuals should parse");
}

void test_config_io_termination_rejects_unknown() {
    bool threw = false;
    try {
        config_from_toml_string("[genetic]\ntermination = \"wall\"\n");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "config_io: an unknown termination mode should throw");
}

// Rejected rather than read as unlimited: a run with no search budget is what
// the other mode is for, so a zero here is a typo rather than an intent.
void test_config_io_individuals_without_a_cap_throws() {
    bool threw = false;
    try {
        config_from_toml_string("[genetic]\ntermination = \"individuals\"\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("max_individuals") != std::string::npos,
               "config_io: the message should name max_individuals");
    }
    expect(threw,
           "config_io: termination = \"individuals\" with no cap should throw");
}

void test_config_io_negative_budgets_throw() {
    for (const char* toml : {"[genetic]\nmax_individuals = -1\n",
                             "[genetic]\nmax_wall_s = -1\n"}) {
        bool threw = false;
        try {
            config_from_toml_string(toml);
        } catch (const std::exception&) {
            threw = true;
        }
        expect(threw, "config_io: a negative budget should throw");
    }
}

void test_config_io_max_wall_s_parsed_under_either_mode() {
    const Config generations =
        config_from_toml_string("[genetic]\nmax_wall_s = 7200\n");
    expect(generations.max_wall_s == 7200,
           "config_io: max_wall_s should parse under the Generations mode");
    const Config individuals = config_from_toml_string(
        "[genetic]\ntermination = \"individuals\"\nmax_individuals = 1000\n"
        "max_wall_s = 7200\n");
    expect(individuals.max_wall_s == 7200,
           "config_io: max_wall_s should parse under the Individuals mode");
}

void test_config_io_selection_scheme_weighted_parsed() {
    const Config cfg =
        config_from_toml_string("[genetic]\nselection_scheme = \"weighted\"\n");
    expect(cfg.selection_scheme == SelectionScheme::WeightedAverage,
           "config_io: selection_scheme = \"weighted\" should parse as "
           "WeightedAverage");
}

void test_config_io_selection_scheme_nsga2_truncate_parsed() {
    const Config cfg = config_from_toml_string(
        "[genetic]\nselection_scheme = \"nsga2-truncate\"\n");
    expect(cfg.selection_scheme == SelectionScheme::Nsga2Truncate,
           "config_io: selection_scheme = \"nsga2-truncate\" should be parsed");
}

void test_config_io_selection_scheme_nsga2_apportion_parsed() {
    const Config cfg = config_from_toml_string(
        "[genetic]\nselection_scheme = \"nsga2-apportion\"\n");
    expect(cfg.selection_scheme == SelectionScheme::Nsga2Apportion,
           "config_io: selection_scheme = \"nsga2-apportion\" should be "
           "parsed");
}

// The two original spellings are rejected rather than aliased, and rejected by
// name: an archived config that sets one must fail loudly and say what to do,
// not run silently under a scheme this binary no longer calls by that name.
void expect_retired_spelling_rejected(const std::string& spelling) {
    bool threw = false;
    try {
        config_from_toml_string("[genetic]\nselection_scheme = \"" + spelling +
                                "\"\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find(spelling) != std::string::npos,
               "config_io: the error should quote the retired spelling " +
                   spelling);
        expect(msg.find("PROVENANCE.json") != std::string::npos,
               "config_io: the error should say how to reproduce an archived "
               "campaign that sets " +
                   spelling);
    }
    expect(threw, "config_io: the retired spelling " + spelling +
                      " should be rejected, not aliased");
}

void test_config_io_selection_scheme_retired_nsga2_rejected() {
    expect_retired_spelling_rejected("nsga2");
}

void test_config_io_selection_scheme_retired_replicate_rejected() {
    expect_retired_spelling_rejected("nsga2-replicate");
}

void test_config_io_selection_scheme_invalid_throws() {
    bool threw = false;
    try {
        config_from_toml_string("[genetic]\nselection_scheme = \"pareto\"\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("selection_scheme") != std::string::npos,
               "config_io: invalid selection_scheme error should name the "
               "field");
    }
    expect(threw, "config_io: an unknown selection_scheme should throw");
}

void test_config_io_similarity_metric_defaults_to_logarithmic() {
    const Config cfg = config_from_toml_string("");
    expect(cfg.similarity_metric == SimilarityMetric::Logarithmic,
           "config_io: similarity_metric should default to Logarithmic");
}

void test_config_io_similarity_metric_direct_parsed() {
    const Config cfg =
        config_from_toml_string("[model_counting]\nmetric = \"direct\"\n");
    expect(cfg.similarity_metric == SimilarityMetric::Direct,
           "config_io: metric = \"direct\" should parse as Direct");
}

void test_config_io_similarity_metric_logarithmic_parsed() {
    const Config cfg =
        config_from_toml_string("[model_counting]\nmetric = \"logarithmic\"\n");
    expect(cfg.similarity_metric == SimilarityMetric::Logarithmic,
           "config_io: metric = \"logarithmic\" should parse as Logarithmic");
}

void test_config_io_similarity_metric_invalid_throws() {
    bool threw = false;
    try {
        config_from_toml_string("[model_counting]\nmetric = \"geometric\"\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("metric") != std::string::npos,
               "config_io: invalid metric error should name the field");
    }
    expect(threw, "config_io: an unknown similarity metric should throw");
}

void test_config_io_empty_string_gives_defaults() {
    const Config cfg = config_from_toml_string("");
    const Config defaults;
    expect(cfg.generations == defaults.generations,
           "config_io: empty TOML should give default generations");
    expect(cfg.population_size == defaults.population_size,
           "config_io: empty TOML should give default population_size");
}

void test_config_io_repair_mode_defaults_to_monolithic() {
    const Config cfg = config_from_toml_string("");
    expect(cfg.repair_mode == RepairMode::Monolithic,
           "config_io: repair_mode should default to Monolithic");
}

void test_config_io_repair_mode_muc_parsed() {
    const Config cfg =
        config_from_toml_string("[tlsf]\nrepair_mode = \"muc\"\n");
    expect(cfg.repair_mode == RepairMode::Muc,
           "config_io: repair_mode = \"muc\" should be parsed");
}

void test_config_io_repair_mode_monolithic_parsed() {
    const Config cfg =
        config_from_toml_string("[tlsf]\nrepair_mode = \"monolithic\"\n");
    expect(cfg.repair_mode == RepairMode::Monolithic,
           "config_io: repair_mode = \"monolithic\" should be parsed");
}

void test_config_io_repair_mode_invalid_throws() {
    bool threw = false;
    try {
        config_from_toml_string("[tlsf]\nrepair_mode = \"iterative\"\n");
    } catch (const std::exception& exc) {
        threw = true;
        const std::string msg(exc.what());
        expect(msg.find("repair_mode") != std::string::npos,
               "config_io: invalid repair_mode error should name the field");
    }
    expect(threw, "config_io: an unknown repair_mode should throw");
}

void test_config_io_muc_max_iterations_parsed() {
    const Config cfg =
        config_from_toml_string("[tlsf]\nmuc_max_iterations = 7\n");
    expect(cfg.muc_max_iterations == 7,
           "config_io: tlsf.muc_max_iterations should be parsed");
}

void test_config_io_muc_max_iterations_nonpositive_throws() {
    bool threw = false;
    try {
        config_from_toml_string("[tlsf]\nmuc_max_iterations = 0\n");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "config_io: muc_max_iterations = 0 should throw");
}

// Every key k_config_keys declares, none of which may warn "unknown key". A
// key absent from this TOML is not covered, so a new key belongs here as well
// as in src/config/keys.hpp.
void test_config_io_known_keys_do_not_warn() {
    const std::string toml = R"(
[genetic]
generations      = 5
population_size  = 100
selection_rate   = 0.5
elitism_rate     = 0.1
crossover_rate   = 0.2
mutation_rate    = 0.8
selection_scheme = "nsga2-truncate"
accumulate_repairs = true

[fitness]
weight_syntactic = 0.3
weight_semantic  = 0.4
weight_status    = 0.6

[mutation]
p_trigger                = 0.3
p_response               = 0.7
p_timing                 = 0.1
p_add_assumption         = 0.05
p_conditional_assumption = 0.25
p_remove_guarantee       = 0.05
p_monotone               = 0.25

[tlsf]
repair_mode        = "muc"
muc_max_iterations = 32

[tlsf.mutation]
p_assumption       = 0.3
p_temporal         = 0.2
p_clone_assumption = 0.25

[model_counting]
default_bound = 10
metric        = "direct"

[filters]
run_implication = false

[runtime]
black_timeout_ms             = 500
ltlsynt_timeout_ms           = 30000
ltl2tgba_timeout_ms          = 1000
ltlfilt_timeout_ms           = 2000
parallel                     = 4
max_scoring_failure_rate     = 0.05
dashboard                    = true
)";
    const std::string warnings = warnings_from(toml);
    expect(warnings.empty(),
           "config_io: a config of known keys should warn about none, got: " +
               warnings);
}

// A removed key is warned about with a hint saying why it is gone, rather than
// as a bare typo: an archived config's omitted keys take the binary's current
// default, so a removed key is the one case the config cannot be reinterpreted.
void test_config_io_retired_key_warns_with_hint() {
    const std::string warnings =
        warnings_from("[mutation]\nstrengthen_assumptions = false\n");
    expect(warnings.find("unknown key mutation.strengthen_assumptions") !=
               std::string::npos,
           "config_io: a removed key should be named by its full path");
    expect(warnings.find("removed:") != std::string::npos,
           "config_io: a removed key's warning should say why it is gone, "
           "got: " +
               warnings);
}

// The keys removed with their operators or gates take the same path: each is
// warned about by its full path with a removal hint, and ignored rather than
// rejected, so an archived config that sets one still loads.
void test_config_io_removed_operator_keys_warn_and_are_ignored() {
    const std::vector<std::pair<std::string, std::string>> removed = {
        {"filters", "run_vacuity = false"},
        {"mutation", "allow_output_assumptions = false"},
        {"tlsf.mutation", "p_remove_assumption = 0.5"},
        {"tlsf.mutation", "p_burst_continue = 0.5"},
        {"runtime", "ganak_timeout_ms = 4000"},
        {"runtime", "max_concurrent_realizability = 6"},
    };
    for (const auto& [section, line] : removed) {
        const std::string key = line.substr(0, line.find(' '));
        std::string path = section;
        path += '.';
        path += key;
        std::string toml = "[";
        toml += section;
        toml += "]\n";
        toml += line;
        toml += '\n';
        const std::string warnings = warnings_from(toml);
        std::string prefix = "config_io: removed key ";
        prefix += path;
        expect(warnings.find("unknown key " + path) != std::string::npos,
               prefix + " should be named by its full path");
        std::string hint_message = prefix;
        hint_message += " should carry a removal hint, got: ";
        hint_message += warnings;
        expect(warnings.find("(removed") != std::string::npos, hint_message);
    }
}

void test_config_io_unknown_section_warns() {
    const std::string warnings = warnings_from("[genetics]\ngenerations = 5\n");
    expect(warnings.find("unknown section [genetics]") != std::string::npos,
           "config_io: an unknown section should be named in a warning");
}

void test_config_io_unknown_key_warns() {
    const std::string warnings = warnings_from("[genetic]\ngenerationss = 5\n");
    expect(
        warnings.find("unknown key genetic.generationss") != std::string::npos,
        "config_io: an unknown key should be warned about by its full path");
}

void test_config_io_unknown_top_level_key_warns() {
    const std::string warnings = warnings_from("generations = 5\n");
    expect(warnings.find("unknown key generations") != std::string::npos,
           "config_io: a key outside any section should be warned about");
}

void test_config_io_unknown_nested_key_warns() {
    const std::string warnings = warnings_from(
        "[tlsf.mutation]\np_assumption_ = 0.3\np_temporal_ = 0.2\n");
    expect(warnings.find("unknown key tlsf.mutation.p_assumption_") !=
               std::string::npos,
           "config_io: an unknown key in a nested section should be warned "
           "about by its full path");
    expect(warnings.find("unknown key tlsf.mutation.p_temporal_") !=
               std::string::npos,
           "config_io: a second unknown key in [tlsf.mutation] should also be "
           "warned about by its full path");
}

void test_config_io_unknown_nested_section_warns() {
    const std::string warnings =
        warnings_from("[filters.interval]\ndedup = 2\n");
    expect(warnings.find("unknown section [filters.interval]") !=
               std::string::npos,
           "config_io: an unknown nested section should be warned about by its "
           "full path");
}

// A key in the wrong section is the typo the per-section spec exists to catch.
void test_config_io_misplaced_key_warns() {
    const std::string warnings = warnings_from("[fitness]\ngenerations = 5\n");
    expect(
        warnings.find("unknown key fitness.generations") != std::string::npos,
        "config_io: a known key in the wrong section should be warned "
        "about");
}

void test_config_io_unknown_key_still_applies_known_ones() {
    std::string warnings;
    const Config cfg = config_capturing_warnings(
        "[genetic]\ngenerations = 5\nnonsense = 1\n", warnings);
    expect(cfg.generations == 5,
           "config_io: an unknown key should not stop known keys applying");
}

}  // namespace

void run_config_io_tests() {
    test_config_io_all_fields();
    test_config_io_partial_overrides_defaults();
    test_config_io_missing_file_throws();
    test_config_io_invalid_toml_throws();
    test_config_io_out_of_range_probability_throws();
    test_config_io_elitism_rate_parsed();
    test_config_io_elitism_not_less_than_selection_throws();
    test_config_io_selection_scheme_defaults_to_nsga2_apportion();
    test_config_io_status_grading_defaults_to_mrs();
    test_config_io_status_grading_tiered_parsed();
    test_config_io_status_grading_mrs_parsed();
    test_config_io_status_grading_aurus_parsed();
    test_config_io_status_grading_rejects_unknown();
    test_config_io_mrs_admission_order_defaults_to_degree();
    test_config_io_mrs_admission_order_spec_parsed();
    test_config_io_mrs_admission_order_degree_parsed();
    test_config_io_mrs_admission_order_rejects_unknown();
    test_config_io_termination_defaults_to_generations();
    test_config_io_termination_individuals_parsed();
    test_config_io_termination_rejects_unknown();
    test_config_io_individuals_without_a_cap_throws();
    test_config_io_negative_budgets_throw();
    test_config_io_max_wall_s_parsed_under_either_mode();
    test_config_io_selection_scheme_weighted_parsed();
    test_config_io_selection_scheme_nsga2_truncate_parsed();
    test_config_io_selection_scheme_nsga2_apportion_parsed();
    test_config_io_selection_scheme_retired_nsga2_rejected();
    test_config_io_selection_scheme_retired_replicate_rejected();
    test_config_io_selection_scheme_invalid_throws();
    test_config_io_similarity_metric_defaults_to_logarithmic();
    test_config_io_similarity_metric_direct_parsed();
    test_config_io_similarity_metric_logarithmic_parsed();
    test_config_io_similarity_metric_invalid_throws();
    test_config_io_empty_string_gives_defaults();
    test_config_io_repair_mode_defaults_to_monolithic();
    test_config_io_repair_mode_muc_parsed();
    test_config_io_repair_mode_monolithic_parsed();
    test_config_io_repair_mode_invalid_throws();
    test_config_io_muc_max_iterations_parsed();
    test_config_io_muc_max_iterations_nonpositive_throws();
    test_config_io_known_keys_do_not_warn();
    test_config_io_retired_key_warns_with_hint();
    test_config_io_removed_operator_keys_warn_and_are_ignored();
    test_config_io_unknown_section_warns();
    test_config_io_unknown_key_warns();
    test_config_io_unknown_top_level_key_warns();
    test_config_io_unknown_nested_key_warns();
    test_config_io_unknown_nested_section_warns();
    test_config_io_misplaced_key_warns();
    test_config_io_unknown_key_still_applies_known_ones();
}
