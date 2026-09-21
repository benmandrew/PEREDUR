#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "config.hpp"

// What the parser demands of a key's value. Each names the complaint it
// raises, which differ for the same bound because each key's message predates
// the table and is kept word for word.
enum class KeyCheck : std::uint8_t {
    None,
    Probability,  // "must be in [0, 1]"
    NonNegative,  // "must be >= 0"
    NotNegative,  // "must not be negative"
    Positive,     // "must be positive"
    AtLeastOne,   // "must be >= 1"
    // "must be at least 1", and the only message without the "config: " prefix
    WidthAtLeastOne,
};

using ConfigMember =
    std::variant<double Config::*, std::size_t Config::*, bool Config::*,
                 std::chrono::milliseconds Config::*, SelectionScheme Config::*,
                 TerminationMode Config::*, StatusGrading Config::*,
                 MrsAdmissionOrder Config::*, SimilarityMetric Config::*,
                 RepairMode Config::*>;

struct ConfigKey {
    const char* section;  // dotted, as in "tlsf.mutation"
    const char* key;
    ConfigMember member;
    KeyCheck check;
};

// Every TOML key, which the parser reads (src/config_io.cpp), declares to its
// unknown-key warning, and writes back into run.json (src/repair/manifest.cpp).
// Listed in read order, which decides the error reported for a config with
// more than one. A new key also needs schemas/config-schema.json and
// example-config.toml; scripts/check_config_schema.py reads this table to hold
// them to it.
inline constexpr std::array<ConfigKey, 44> k_config_keys{{
    {"genetic", "generations", &Config::generations, KeyCheck::Positive},
    {"genetic", "population_size", &Config::population_size,
     KeyCheck::Positive},
    {"genetic", "selection_rate", &Config::selection_rate,
     KeyCheck::Probability},
    {"genetic", "elitism_rate", &Config::elitism_rate, KeyCheck::Probability},
    {"genetic", "crossover_rate", &Config::crossover_rate,
     KeyCheck::Probability},
    {"genetic", "mutation_rate", &Config::mutation_rate, KeyCheck::Probability},
    {"genetic", "accumulate_repairs", &Config::accumulate_repairs,
     KeyCheck::None},
    {"genetic", "max_individuals", &Config::max_individuals,
     KeyCheck::NotNegative},
    {"genetic", "max_wall_s", &Config::max_wall_s, KeyCheck::NotNegative},
    {"genetic", "termination", &Config::termination, KeyCheck::None},
    {"genetic", "selection_scheme", &Config::selection_scheme, KeyCheck::None},
    {"fitness", "weight_syntactic", &Config::fitness_weight_syntactic,
     KeyCheck::NonNegative},
    {"fitness", "weight_semantic", &Config::fitness_weight_semantic,
     KeyCheck::NonNegative},
    {"fitness", "weight_status", &Config::fitness_weight_status,
     KeyCheck::NonNegative},
    {"fitness", "status_grading", &Config::status_grading, KeyCheck::None},
    {"fitness", "mrs_admission_order", &Config::mrs_admission_order,
     KeyCheck::None},
    {"mutation", "p_trigger", &Config::p_trigger, KeyCheck::Probability},
    {"mutation", "p_response", &Config::p_response, KeyCheck::Probability},
    {"mutation", "p_timing", &Config::p_timing, KeyCheck::Probability},
    {"mutation", "p_condition_type", &Config::p_condition_type,
     KeyCheck::Probability},
    {"mutation", "p_scope", &Config::p_scope, KeyCheck::Probability},
    {"mutation", "p_stop", &Config::p_stop, KeyCheck::Probability},
    {"mutation", "p_monotone", &Config::p_monotone, KeyCheck::Probability},
    {"mutation", "p_add_assumption", &Config::p_add_assumption,
     KeyCheck::Probability},
    {"mutation", "p_remove_guarantee", &Config::p_remove_guarantee,
     KeyCheck::Probability},
    {"mutation", "p_conditional_assumption", &Config::p_conditional_assumption,
     KeyCheck::Probability},
    {"model_counting", "default_bound", &Config::default_model_counting_bound,
     KeyCheck::Positive},
    {"model_counting", "metric", &Config::similarity_metric, KeyCheck::None},
    {"filters", "run_implication", &Config::run_implication_filter,
     KeyCheck::None},
    {"runtime", "black_timeout_ms", &Config::black_timeout,
     KeyCheck::NonNegative},
    {"runtime", "ltlsynt_timeout_ms", &Config::ltlsynt_timeout,
     KeyCheck::NonNegative},
    {"runtime", "ltl2tgba_timeout_ms", &Config::ltl2tgba_timeout,
     KeyCheck::NonNegative},
    {"runtime", "ltlfilt_timeout_ms", &Config::ltlfilt_timeout,
     KeyCheck::NonNegative},
    {"runtime", "parallel", &Config::parallel, KeyCheck::AtLeastOne},
    {"runtime", "dashboard", &Config::dashboard, KeyCheck::None},
    {"runtime", "max_scoring_failure_rate", &Config::max_scoring_failure_rate,
     KeyCheck::Probability},
    {"tlsf.mutation", "p_assumption", &Config::tlsf_p_assumption,
     KeyCheck::Probability},
    {"tlsf.mutation", "p_temporal", &Config::tlsf_p_temporal,
     KeyCheck::Probability},
    {"tlsf.mutation", "p_clone_assumption", &Config::tlsf_p_clone_assumption,
     KeyCheck::Probability},
    {"tlsf.mutation", "p_bare_assumption", &Config::tlsf_p_bare_assumption,
     KeyCheck::Probability},
    {"tlsf.mutation", "max_assumption_width",
     &Config::tlsf_max_assumption_width, KeyCheck::WidthAtLeastOne},
    {"tlsf", "repair_mode", &Config::repair_mode, KeyCheck::None},
    {"tlsf", "muc_max_iterations", &Config::muc_max_iterations,
     KeyCheck::Positive},
    {"tlsf", "muc_screen_depth", &Config::muc_screen_depth, KeyCheck::Positive},
}};

// The table names along a dotted section, outermost first.
inline std::vector<std::string_view> section_path(std::string_view dotted) {
    std::vector<std::string_view> names;
    for (;;) {
        const std::size_t dot = dotted.find('.');
        names.push_back(dotted.substr(0, dot));
        if (dot == std::string_view::npos) {
            return names;
        }
        dotted.remove_prefix(dot + 1);
    }
}
