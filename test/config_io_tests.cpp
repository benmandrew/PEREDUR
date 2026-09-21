#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "config_io.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "config_io";

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

TEST(test_config_io_all_fields) {
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

TEST(test_config_io_partial_overrides_defaults) {
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

TEST(test_config_io_missing_file_throws) {
    expect_throws(
        [&] { config_from_toml("/tmp/peredur_test_nonexistent_config.toml"); },
        "config_io: missing file should throw", "does not exist");
}

TEST(test_config_io_invalid_toml_throws) {
    expect_throws(
        [&] { config_from_toml_string("this is not valid toml ==="); },
        "config_io: invalid TOML should throw");
}

TEST(test_config_io_out_of_range_probability_throws) {
    expect_throws(
        [&] { config_from_toml_string("[mutation]\np_trigger = 1.5\n"); },
        "config_io: out-of-range probability should throw", "p_trigger");
}

TEST(test_config_io_elitism_rate_parsed) {
    const Config cfg = config_from_toml_string(
        "[genetic]\nselection_rate = 0.6\nelitism_rate = 0.2\n");
    expect(cfg.elitism_rate == 0.2,
           "config_io: genetic.elitism_rate should be parsed from TOML");
}

TEST(test_config_io_elitism_not_less_than_selection_throws) {
    expect_throws(
        [&] {
            config_from_toml_string(
                "[genetic]\nselection_rate = 0.3\nelitism_rate = 0.3\n");
        },
        "config_io: elitism_rate not less than selection_rate should throw",
        "elitism_rate");
}

// An enum-valued key: its default, every spelling it accepts, and one it
// rejects. The default is read both from a bare Config and from a file that
// omits the key, since an archived config inherits it either way.
template <typename Enum>
struct EnumKey {
    std::string m_section;
    std::string m_key;
    Enum Config::* m_member = nullptr;
    Enum m_default{};
    std::vector<std::pair<std::string, Enum>> m_spellings;
    std::string m_unknown;
    bool m_error_names_key = false;
};

template <typename Enum>
void expect_enum_key(const EnumKey<Enum>& key) {
    const std::string name = "config_io: " + key.m_section + "." + key.m_key;
    const auto toml = [&](const std::string& spelling) {
        return "[" + key.m_section + "]\n" + key.m_key + " = \"" + spelling +
               "\"\n";
    };
    expect(Config{}.*key.m_member == key.m_default,
           name + " should default to the pinned value");
    expect(config_from_toml_string("").*key.m_member == key.m_default,
           name + " should keep its default when the file omits it");
    for (const auto& [spelling, value] : key.m_spellings) {
        std::string label = name;
        label.append(" = \"").append(spelling).append("\" should parse");
        expect(config_from_toml_string(toml(spelling)).*key.m_member == value,
               label);
    }
    expect_throws([&] { config_from_toml_string(toml(key.m_unknown)); },
                  name + " = \"" + key.m_unknown + "\" should be rejected",
                  key.m_error_names_key ? key.m_key : std::string{});
}

TEST(test_config_io_enum_keys) {
    // Pinned because the default is what an archived config inherits wherever
    // it states no scheme. It has moved three times -- WeightedAverage, Nsga2
    // (renamed Nsga2Truncate), and Nsga2Apportion from 2026-08-14 -- and only
    // the 64 `sweep_B_pop50_takeoff_*` configs under 2026-07-13-fretish-sweeps
    // are exposed.
    // See "Config vintage" in experiments/README.md before moving it again.
    expect_enum_key<SelectionScheme>(
        {"genetic",
         "selection_scheme",
         &Config::selection_scheme,
         SelectionScheme::Nsga2Apportion,
         {{"weighted", SelectionScheme::WeightedAverage},
          {"nsga2-truncate", SelectionScheme::Nsga2Truncate},
          {"nsga2-apportion", SelectionScheme::Nsga2Apportion}},
         "pareto",
         true});
    // Pinned because the default is what every archived config inherits: the
    // key did not exist before 2026-08-11, so no archived config can state it,
    // and moving this back to Tiered silently re-reads every one of them under
    // a different status objective. See "Config vintage" in
    // experiments/README.md.
    expect_enum_key<StatusGrading>({"fitness",
                                    "status_grading",
                                    &Config::status_grading,
                                    StatusGrading::Mrs,
                                    {{"tiered", StatusGrading::Tiered},
                                     {"mrs", StatusGrading::Mrs},
                                     {"aurus", StatusGrading::Aurus}},
                                    "greedy",
                                    false});
    expect_enum_key<MrsAdmissionOrder>({"fitness",
                                        "mrs_admission_order",
                                        &Config::mrs_admission_order,
                                        MrsAdmissionOrder::Degree,
                                        {{"spec", MrsAdmissionOrder::Spec},
                                         {"degree", MrsAdmissionOrder::Degree}},
                                        "rotate",
                                        false});
    expect_enum_key<SimilarityMetric>(
        {"model_counting",
         "metric",
         &Config::similarity_metric,
         SimilarityMetric::Logarithmic,
         {{"direct", SimilarityMetric::Direct},
          {"logarithmic", SimilarityMetric::Logarithmic}},
         "geometric",
         true});
    expect_enum_key<RepairMode>(
        {"tlsf",
         "repair_mode",
         &Config::repair_mode,
         RepairMode::Monolithic,
         {{"muc", RepairMode::Muc}, {"monolithic", RepairMode::Monolithic}},
         "iterative",
         true});
}

// Pinned because every archived config omits these keys and inherits whatever
// they mean. Generations with no caps is what a run did before they existed, so
// the defaults have to keep reproducing it.
TEST(test_config_io_termination_defaults_to_generations) {
    const Config cfg = config_from_toml_string("");
    expect(cfg.termination == TerminationMode::Generations,
           "config_io: termination should default to Generations");
    expect(cfg.max_individuals == 0,
           "config_io: max_individuals should default to 0");
    expect(cfg.max_wall_s == 0, "config_io: max_wall_s should default to 0");
}

TEST(test_config_io_termination_individuals_parsed) {
    const Config cfg = config_from_toml_string(
        "[genetic]\ntermination = \"individuals\"\nmax_individuals = 1000\n");
    expect(cfg.termination == TerminationMode::Individuals,
           "config_io: termination = \"individuals\" should parse as "
           "Individuals");
    expect(cfg.max_individuals == 1000,
           "config_io: max_individuals should parse");
}

TEST(test_config_io_termination_rejects_unknown) {
    expect_throws(
        [&] { config_from_toml_string("[genetic]\ntermination = \"wall\"\n"); },
        "config_io: an unknown termination mode should throw");
}

// Rejected rather than read as unlimited: a run with no search budget is what
// the other mode is for, so a zero here is a typo rather than an intent.
TEST(test_config_io_individuals_without_a_cap_throws) {
    expect_throws(
        [&] {
            config_from_toml_string(
                "[genetic]\ntermination = \"individuals\"\n");
        },
        "config_io: termination = \"individuals\" with no cap should throw",
        "max_individuals");
}

TEST(test_config_io_negative_budgets_throw) {
    for (const char* toml : {"[genetic]\nmax_individuals = -1\n",
                             "[genetic]\nmax_wall_s = -1\n"}) {
        expect_throws([&] { config_from_toml_string(toml); },
                      "config_io: a negative budget should throw");
    }
}

TEST(test_config_io_max_wall_s_parsed_under_either_mode) {
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

// The two original spellings are rejected rather than aliased, and rejected by
// name: an archived config that sets one must fail loudly and say what to do,
// not run silently under a scheme this binary no longer calls by that name.
void expect_retired_spelling_rejected(const std::string& spelling) {
    const std::string msg = expect_throws(
        [&] {
            config_from_toml_string("[genetic]\nselection_scheme = \"" +
                                    spelling + "\"\n");
        },
        "config_io: the retired spelling " + spelling +
            " should be rejected, not aliased",
        spelling);
    expect(msg.find("PROVENANCE.json") != std::string::npos,
           "config_io: the error should say how to reproduce an archived "
           "campaign that sets " +
               spelling);
}

TEST(test_config_io_selection_scheme_retired_nsga2_rejected) {
    expect_retired_spelling_rejected("nsga2");
}

TEST(test_config_io_selection_scheme_retired_replicate_rejected) {
    expect_retired_spelling_rejected("nsga2-replicate");
}

TEST(test_config_io_empty_string_gives_defaults) {
    const Config cfg = config_from_toml_string("");
    const Config defaults;
    expect(cfg.generations == defaults.generations,
           "config_io: empty TOML should give default generations");
    expect(cfg.population_size == defaults.population_size,
           "config_io: empty TOML should give default population_size");
}

TEST(test_config_io_muc_max_iterations_parsed) {
    const Config cfg =
        config_from_toml_string("[tlsf]\nmuc_max_iterations = 7\n");
    expect(cfg.muc_max_iterations == 7,
           "config_io: tlsf.muc_max_iterations should be parsed");
}

TEST(test_config_io_muc_max_iterations_nonpositive_throws) {
    expect_throws(
        [&] { config_from_toml_string("[tlsf]\nmuc_max_iterations = 0\n"); },
        "config_io: muc_max_iterations = 0 should throw");
}

TEST(test_config_io_muc_screen_depth_parsed) {
    const Config cfg =
        config_from_toml_string("[tlsf]\nmuc_screen_depth = 2\n");
    expect(cfg.muc_screen_depth == 2,
           "config_io: tlsf.muc_screen_depth should be parsed");
    expect(Config{}.muc_screen_depth == 3,
           "config_io: tlsf.muc_screen_depth defaults to 3");
}

TEST(test_config_io_muc_screen_depth_nonpositive_throws) {
    expect_throws(
        [&] { config_from_toml_string("[tlsf]\nmuc_screen_depth = 0\n"); },
        "config_io: muc_screen_depth = 0 should throw");
}

// Every key k_config_keys declares, none of which may warn "unknown key". A
// key absent from this TOML is not covered, so a new key belongs here as well
// as in src/config/keys.hpp.
TEST(test_config_io_known_keys_do_not_warn) {
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
muc_screen_depth   = 2

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
TEST(test_config_io_retired_key_warns_with_hint) {
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
TEST(test_config_io_removed_operator_keys_warn_and_are_ignored) {
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

TEST(test_config_io_unknown_section_warns) {
    const std::string warnings = warnings_from("[genetics]\ngenerations = 5\n");
    expect(warnings.find("unknown section [genetics]") != std::string::npos,
           "config_io: an unknown section should be named in a warning");
}

TEST(test_config_io_unknown_key_warns) {
    const std::string warnings = warnings_from("[genetic]\ngenerationss = 5\n");
    expect(
        warnings.find("unknown key genetic.generationss") != std::string::npos,
        "config_io: an unknown key should be warned about by its full path");
}

TEST(test_config_io_unknown_top_level_key_warns) {
    const std::string warnings = warnings_from("generations = 5\n");
    expect(warnings.find("unknown key generations") != std::string::npos,
           "config_io: a key outside any section should be warned about");
}

TEST(test_config_io_unknown_nested_key_warns) {
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

TEST(test_config_io_unknown_nested_section_warns) {
    const std::string warnings =
        warnings_from("[filters.interval]\ndedup = 2\n");
    expect(warnings.find("unknown section [filters.interval]") !=
               std::string::npos,
           "config_io: an unknown nested section should be warned about by its "
           "full path");
}

// A key in the wrong section is the typo the per-section spec exists to catch.
TEST(test_config_io_misplaced_key_warns) {
    const std::string warnings = warnings_from("[fitness]\ngenerations = 5\n");
    expect(
        warnings.find("unknown key fitness.generations") != std::string::npos,
        "config_io: a known key in the wrong section should be warned "
        "about");
}

TEST(test_config_io_unknown_key_still_applies_known_ones) {
    std::string warnings;
    const Config cfg = config_capturing_warnings(
        "[genetic]\ngenerations = 5\nnonsense = 1\n", warnings);
    expect(cfg.generations == 5,
           "config_io: an unknown key should not stop known keys applying");
}

}  // namespace
