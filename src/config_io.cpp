#include "config_io.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

#include "config/enum_names.hpp"
#include "runner/black.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/spot.hpp"

namespace {

/// Names the replacement for a selection_scheme spelling retired by the
/// 2026-08-06 rename, as a clause to fold into the error, or "" for any other
/// value. The retired spellings are matched here rather than listed in
/// EnumNames<SelectionScheme>, which both a reader and
/// scripts/check_config_schema.py take to be the values the parser accepts --
/// these are rejected, and the schema's enum must not list them.
///
/// Rejected rather than aliased: every archived campaign config pins one of
/// them, so each fails against a current binary, deliberately. Reproduce those
/// at the commit their PROVENANCE.json names, which is what the vendored
/// per-campaign scripts/ exists for. Archived *results* are the separate half
/// and still join, via canonical_scheme() in the harness scripts.
std::string retired_scheme_hint(const std::string& value) {
    std::string replacement;
    if (value == "nsga2") {
        replacement = "nsga2-truncate";
    } else if (value == "nsga2-replicate") {
        replacement = "nsga2-apportion";
    } else {
        return "";
    }
    return "= \"" + value + "\" was renamed on 2026-08-06 to \"" + replacement +
           "\"; to reproduce an archived campaign, build the commit its "
           "PROVENANCE.json names rather than editing its config. It ";
}

template <typename T>
T require_positive(T value, const char* name) {
    if (value <= T{0}) {
        throw std::runtime_error(std::string("config: ") + name +
                                 " must be positive");
    }
    return value;
}

void require_probability(double value, const char* name) {
    if (value < 0.0 || value > 1.0) {
        throw std::runtime_error(std::string("config: ") + name +
                                 " must be in [0, 1]");
    }
}

void require_nonnegative(double value, const char* name) {
    if (value < 0.0) {
        throw std::runtime_error(std::string("config: ") + name +
                                 " must be >= 0");
    }
}

/// Reads the string at `key` into `out` when it is set, rejecting any spelling
/// EnumNames does not list.
template <typename Enum>
void read_enum(const toml::table& tbl, const char* key, Enum& out,
               const char* path) {
    const auto val = tbl[key].value<std::string>();
    if (!val) {
        return;
    }
    const std::optional<Enum> parsed = enum_from_name<Enum>(*val);
    if (!parsed) {
        if constexpr (std::is_same_v<Enum, SelectionScheme>) {
            throw std::runtime_error(
                "config: genetic.selection_scheme " +
                retired_scheme_hint(*val) +
                "must be \"weighted\", \"nsga2-truncate\", or "
                "\"nsga2-apportion\"");
        }
        throw std::runtime_error(std::string("config: ") + path + " must be " +
                                 enum_alternatives<Enum>());
    }
    out = *parsed;
}

// Mirrors the keys the apply_* functions below read, so that a typo in a
// config file is reported rather than silently ignored. A key needs three
// edits, none of which the compiler ties together: the apply_* function that
// reads it, this spec (else the parser warns "unknown key" on a key it
// accepts), and schemas/config-schema.json (else editors reject it).
// scripts/check_config_schema.py enforces the last two against each other and
// against example-config.toml, as part of the lint target. Only
// test_config_io_known_keys_do_not_warn catches a key missing from this spec,
// and only for keys its TOML actually sets, so a new key belongs there too.
struct KeySpec {
    std::set<std::string> keys;
    std::map<std::string, KeySpec> tables;
};

KeySpec section(std::set<std::string> keys,
                std::map<std::string, KeySpec> tables = {}) {
    return KeySpec{std::move(keys), std::move(tables)};
}

const KeySpec& config_key_spec() {
    static const KeySpec spec = section(
        {},
        {{"genetic",
          section({"generations", "population_size", "selection_rate",
                   "elitism_rate", "crossover_rate", "mutation_rate",
                   "selection_scheme", "accumulate_repairs", "termination",
                   "max_individuals", "max_wall_s"})},
         {"fitness",
          section({"weight_syntactic", "weight_semantic", "weight_status",
                   "status_grading", "mrs_admission_order"})},
         {"mutation",
          section({"p_trigger", "p_response", "p_timing", "p_condition_type",
                   "p_scope", "p_stop", "p_monotone", "p_add_assumption",
                   "p_remove_guarantee", "p_conditional_assumption"})},
         {"tlsf",
          section({"repair_mode", "muc_max_iterations"},
                  {{"mutation",
                    section({"p_assumption", "p_temporal", "p_clone_assumption",
                             "max_assumption_width", "p_bare_assumption"})}})},
         {"model_counting", section({"default_bound", "metric"})},
         {"filters", section({"run_implication"})},
         {"runtime",
          section({"black_timeout_ms", "ltlsynt_timeout_ms",
                   "ltl2tgba_timeout_ms", "ltlfilt_timeout_ms", "parallel",
                   "max_scoring_failure_rate", "dashboard"})}});
    return spec;
}

/// Says why a key a config still sets is no longer known, for the keys removed
/// rather than never recognised. An archived campaign config is a partial
/// record whose omitted keys take the binary's current default, so a removed
/// key is the one case where the config cannot be reinterpreted at all; the
/// bare "unknown key" reads as a typo instead.
std::string retired_key_hint(const std::string& path) {
    if (path == "fitness.weight_halstead") {
        return " (removed: the Halstead objective no longer exists)";
    }
    if (path == "mutation.strengthen_assumptions") {
        return " (removed: assumptions are always mutated in the strengthening"
               " direction)";
    }
    if (path == "tlsf.mutation.p_union_assumption") {
        return " (removed: the union crossover no longer exists)";
    }
    if (path == "tlsf.mutation.p_monotone") {
        return " (removed: set [mutation] p_monotone, which both paths read)";
    }
    if (path == "tlsf.mutation.connective_implies") {
        return " (removed: the case (2d) graft always offers ->)";
    }
    if (path == "tlsf.mutation.monotone_atom_rules" ||
        path == "tlsf.mutation.monotone_extra_rules") {
        return " (removed: the monotone rewrite always offers every rule)";
    }
    if (path == "filters.run_weakening") {
        return " (removed: the final weakening screen no longer exists)";
    }
    if (path == "filters.run_well_separation") {
        return " (removed: the status score and the output gate enforce"
               " well-separation)";
    }
    if (path == "filters.run_vacuity") {
        return " (removed: vacuity now always runs per generation)";
    }
    if (path == "mutation.allow_output_assumptions") {
        return " (removed: output assumptions are always allowed)";
    }
    if (path == "tlsf.mutation.p_remove_assumption" ||
        path == "tlsf.mutation.p_burst_continue") {
        return " (removed with its operator)";
    }
    if (path == "runtime.ganak_timeout_ms") {
        return " (removed: ganak runs without a timeout)";
    }
    if (path == "runtime.max_concurrent_realizability") {
        return " (removed: no concurrency cap; bound RAM with"
               " runtime.parallel)";
    }
    return "";
}

void warn_unknown_keys(const toml::table& tbl, const KeySpec& spec,
                       const std::string& prefix) {
    for (const auto& [key, node] : tbl) {
        const std::string name(key.str());
        const std::string path = prefix + name;
        if (node.is_table()) {
            const auto sub = spec.tables.find(name);
            if (sub == spec.tables.end()) {
                std::cerr << "config: unknown section [" << path
                          << "], ignoring\n";
            } else {
                warn_unknown_keys(*node.as_table(), sub->second, path + ".");
            }
        } else if (spec.keys.find(name) == spec.keys.end()) {
            std::cerr << "config: unknown key " << path
                      << retired_key_hint(path) << ", ignoring\n";
        }
    }
}

std::size_t require_count(int64_t value, const char* name) {
    if (value < 0) {
        throw std::runtime_error(std::string("config: ") + name +
                                 " must not be negative");
    }
    return static_cast<std::size_t>(value);
}

// Split out of apply_genetic, which the cognitive-complexity limit will not
// hold all of. The three keys are one setting between them anyway: what ends
// the run.
void apply_termination(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["max_individuals"].value<int64_t>()) {
        cfg.max_individuals = require_count(*val, "genetic.max_individuals");
    }
    if (auto val = tbl["max_wall_s"].value<int64_t>()) {
        cfg.max_wall_s = require_count(*val, "genetic.max_wall_s");
    }
    read_enum(tbl, "termination", cfg.termination, "genetic.termination");
    // Rejected rather than read as unlimited: a run with no search budget is
    // what the other mode is for, and silently treating zero as unbounded
    // would turn a typo into a run that ends only on its deadline. Checked
    // against the final values, either of which may have come from the TOML
    // or from its default.
    if (cfg.termination == TerminationMode::Individuals &&
        cfg.max_individuals == 0) {
        throw std::runtime_error(
            "config: genetic.max_individuals must be at least 1 under "
            "genetic.termination = \"individuals\"");
    }
}

void apply_genetic(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["generations"].value<int64_t>()) {
        cfg.generations = static_cast<std::size_t>(
            require_positive(*val, "genetic.generations"));
    }
    if (auto val = tbl["population_size"].value<int64_t>()) {
        cfg.population_size = static_cast<std::size_t>(
            require_positive(*val, "genetic.population_size"));
    }
    if (auto val = tbl["selection_rate"].value<double>()) {
        require_probability(*val, "genetic.selection_rate");
        cfg.selection_rate = *val;
    }
    if (auto val = tbl["elitism_rate"].value<double>()) {
        require_probability(*val, "genetic.elitism_rate");
        cfg.elitism_rate = *val;
    }
    if (auto val = tbl["crossover_rate"].value<double>()) {
        require_probability(*val, "genetic.crossover_rate");
        cfg.crossover_rate = *val;
    }
    if (auto val = tbl["mutation_rate"].value<double>()) {
        require_probability(*val, "genetic.mutation_rate");
        cfg.mutation_rate = *val;
    }
    if (auto val = tbl["accumulate_repairs"].value<bool>()) {
        cfg.accumulate_repairs = *val;
    }
    apply_termination(tbl, cfg);
    read_enum(tbl, "selection_scheme", cfg.selection_scheme,
              "genetic.selection_scheme");
    // Elites are a subset of the selected parents, so elitism must be strictly
    // smaller than selection. Checked against the final values (either may come
    // from the TOML or fall back to its default).
    if (cfg.elitism_rate >= cfg.selection_rate) {
        throw std::runtime_error(
            "config: genetic.elitism_rate must be less than "
            "genetic.selection_rate");
    }
}

void apply_fitness(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["weight_syntactic"].value<double>()) {
        require_nonnegative(*val, "fitness.weight_syntactic");
        cfg.fitness_weight_syntactic = *val;
    }
    if (auto val = tbl["weight_semantic"].value<double>()) {
        require_nonnegative(*val, "fitness.weight_semantic");
        cfg.fitness_weight_semantic = *val;
    }
    if (auto val = tbl["weight_status"].value<double>()) {
        require_nonnegative(*val, "fitness.weight_status");
        cfg.fitness_weight_status = *val;
    }
    read_enum(tbl, "status_grading", cfg.status_grading,
              "fitness.status_grading");
    read_enum(tbl, "mrs_admission_order", cfg.mrs_admission_order,
              "fitness.mrs_admission_order");
}

void apply_mutation(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["p_trigger"].value<double>()) {
        require_probability(*val, "mutation.p_trigger");
        cfg.p_trigger = *val;
    }
    if (auto val = tbl["p_response"].value<double>()) {
        require_probability(*val, "mutation.p_response");
        cfg.p_response = *val;
    }
    if (auto val = tbl["p_timing"].value<double>()) {
        require_probability(*val, "mutation.p_timing");
        cfg.p_timing = *val;
    }
    if (auto val = tbl["p_condition_type"].value<double>()) {
        require_probability(*val, "mutation.p_condition_type");
        cfg.p_condition_type = *val;
    }
    if (auto val = tbl["p_scope"].value<double>()) {
        require_probability(*val, "mutation.p_scope");
        cfg.p_scope = *val;
    }
    if (auto val = tbl["p_stop"].value<double>()) {
        require_probability(*val, "mutation.p_stop");
        cfg.p_stop = *val;
    }
    if (auto val = tbl["p_monotone"].value<double>()) {
        require_probability(*val, "mutation.p_monotone");
        cfg.p_monotone = *val;
    }
    if (auto val = tbl["p_add_assumption"].value<double>()) {
        require_probability(*val, "mutation.p_add_assumption");
        cfg.p_add_assumption = *val;
    }
    if (auto val = tbl["p_remove_guarantee"].value<double>()) {
        require_probability(*val, "mutation.p_remove_guarantee");
        cfg.p_remove_guarantee = *val;
    }
    if (auto val = tbl["p_conditional_assumption"].value<double>()) {
        require_probability(*val, "mutation.p_conditional_assumption");
        cfg.p_conditional_assumption = *val;
    }
}

// The [tlsf.mutation] probabilities differ only in their key and the member
// they land on, so they are driven from a table rather than a branch each.
// Four near-identical branches read as complexity to clang-tidy, and each was
// another chance to paste the wrong member name beside a key -- a mistake
// nothing else here would catch, the types being identical.
constexpr std::array<std::pair<const char*, double Config::*>, 4>
    k_tlsf_mutation_probabilities{{
        {"p_assumption", &Config::tlsf_p_assumption},
        {"p_temporal", &Config::tlsf_p_temporal},
        {"p_clone_assumption", &Config::tlsf_p_clone_assumption},
        {"p_bare_assumption", &Config::tlsf_p_bare_assumption},
    }};

void apply_tlsf_mutation(const toml::table& mutation, Config& cfg) {
    for (const auto& [key, member] : k_tlsf_mutation_probabilities) {
        if (auto val = mutation[key].value<double>()) {
            const std::string qualified = std::string("tlsf.mutation.") + key;
            require_probability(*val, qualified.c_str());
            cfg.*member = *val;
        }
    }
    // The one key here that is a count rather than a probability.
    if (auto val = mutation["max_assumption_width"].value<std::int64_t>()) {
        if (*val < 1) {
            throw std::runtime_error(
                "tlsf.mutation.max_assumption_width must be at least 1");
        }
        cfg.tlsf_max_assumption_width = static_cast<std::size_t>(*val);
    }
}

void apply_tlsf(const toml::table& tbl, Config& cfg) {
    if (const auto* mutation = tbl["mutation"].as_table()) {
        apply_tlsf_mutation(*mutation, cfg);
    }
    read_enum(tbl, "repair_mode", cfg.repair_mode, "tlsf.repair_mode");
    if (auto val = tbl["muc_max_iterations"].value<int64_t>()) {
        cfg.muc_max_iterations = static_cast<std::size_t>(
            require_positive(*val, "tlsf.muc_max_iterations"));
    }
}

void apply_model_counting(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["default_bound"].value<int64_t>()) {
        cfg.default_model_counting_bound = static_cast<std::size_t>(
            require_positive(*val, "model_counting.default_bound"));
    }
    read_enum(tbl, "metric", cfg.similarity_metric, "model_counting.metric");
}

void apply_filters(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["run_implication"].value<bool>()) {
        cfg.run_implication_filter = *val;
    }
}

void apply_runtime(const toml::table& tbl, Config& cfg) {
    if (auto val = tbl["black_timeout_ms"].value<int64_t>()) {
        if (*val < 0) {
            throw std::runtime_error(
                "config: runtime.black_timeout_ms must be >= 0");
        }
        cfg.black_timeout = std::chrono::milliseconds{*val};
    }
    if (auto val = tbl["ltlsynt_timeout_ms"].value<int64_t>()) {
        if (*val < 0) {
            throw std::runtime_error(
                "config: runtime.ltlsynt_timeout_ms must be >= 0");
        }
        cfg.ltlsynt_timeout = std::chrono::milliseconds{*val};
    }
    if (auto val = tbl["ltl2tgba_timeout_ms"].value<int64_t>()) {
        if (*val < 0) {
            throw std::runtime_error(
                "config: runtime.ltl2tgba_timeout_ms must be >= 0");
        }
        cfg.ltl2tgba_timeout = std::chrono::milliseconds{*val};
    }
    if (auto val = tbl["ltlfilt_timeout_ms"].value<int64_t>()) {
        if (*val < 0) {
            throw std::runtime_error(
                "config: runtime.ltlfilt_timeout_ms must be >= 0");
        }
        cfg.ltlfilt_timeout = std::chrono::milliseconds{*val};
    }
    if (auto val = tbl["parallel"].value<int64_t>()) {
        if (*val <= 0) {
            throw std::runtime_error("config: runtime.parallel must be >= 1");
        }
        cfg.parallel = static_cast<std::size_t>(*val);
    }
    if (auto val = tbl["dashboard"].value<bool>()) {
        cfg.dashboard = *val;
    }
    if (auto val = tbl["max_scoring_failure_rate"].value<double>()) {
        require_probability(*val, "runtime.max_scoring_failure_rate");
        cfg.max_scoring_failure_rate = *val;
    }
}

Config apply_toml(const toml::table& tbl) {
    warn_unknown_keys(tbl, config_key_spec(), "");
    Config cfg;
    if (const auto* sec = tbl["genetic"].as_table()) {
        apply_genetic(*sec, cfg);
    }
    if (const auto* sec = tbl["fitness"].as_table()) {
        apply_fitness(*sec, cfg);
    }
    if (const auto* sec = tbl["mutation"].as_table()) {
        apply_mutation(*sec, cfg);
    }
    if (const auto* sec = tbl["model_counting"].as_table()) {
        apply_model_counting(*sec, cfg);
    }
    if (const auto* sec = tbl["filters"].as_table()) {
        apply_filters(*sec, cfg);
    }
    if (const auto* sec = tbl["runtime"].as_table()) {
        apply_runtime(*sec, cfg);
    }
    if (const auto* sec = tbl["tlsf"].as_table()) {
        apply_tlsf(*sec, cfg);
    }
    return cfg;
}

}  // namespace

Config config_from_toml(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("config: file does not exist: " +
                                 path.string());
    }
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& exc) {
        throw std::runtime_error(std::string("config: TOML parse error: ") +
                                 exc.what());
    }
    return apply_toml(tbl);
}

Config config_from_toml_string(const std::string& content) {
    toml::table tbl;
    try {
        tbl = toml::parse(content);
    } catch (const toml::parse_error& exc) {
        throw std::runtime_error(std::string("config: TOML parse error: ") +
                                 exc.what());
    }
    return apply_toml(tbl);
}

void apply_tool_timeouts(const Config& cfg) {
    global_sat_checker().set_timeout(cfg.black_timeout);
    RealizabilityChecker::set_timeout(cfg.ltlsynt_timeout);
    set_ltl2tgba_timeout(cfg.ltl2tgba_timeout);
    set_ltlfilt_timeout(cfg.ltlfilt_timeout);
}
