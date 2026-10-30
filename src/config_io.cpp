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
#include <string_view>
#include <type_traits>
#include <variant>

#define TOML_EXCEPTIONS 1
#include <toml++/toml.hpp>

#include "config/enum_names.hpp"
#include "config/keys.hpp"
#include "runner/black.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/spot.hpp"

namespace {

/// A value a kept key no longer accepts, renamed or removed with the arm it
/// selected. Matched here rather than listed in EnumNames, which both a reader
/// and scripts/check_config_schema.py take to be the values the parser accepts:
/// these are rejected, and the schema's enum must not list them.
struct RetiredValue {
    std::string_view path;
    std::string_view value;
    std::string_view fate;
};

constexpr std::array<RetiredValue, 4> k_retired_values{{
    {"genetic.selection_scheme", "nsga2",
     "was renamed to \"nsga2-truncate\" on 2026-08-06, and that was removed "
     "on 2026-10-30"},
    {"genetic.selection_scheme", "nsga2-replicate",
     "was renamed to \"nsga2-apportion\" on 2026-08-06"},
    {"genetic.selection_scheme", "nsga2-truncate", "was removed on 2026-10-30"},
    {"fitness.status_grading", "tiered", "was removed on 2026-10-30"},
}};

/// Says what became of a value listed in k_retired_values, as a clause to fold
/// into the error, or "" for any other value.
///
/// Rejected rather than aliased or mapped onto a survivor: an archived config
/// that pins one fails against a current binary, deliberately. Reproduce it at
/// the commit its PROVENANCE.json names, which is what the vendored
/// per-campaign scripts/ exists for. Archived *results* are the separate half
/// and still join, via canonical_scheme() in the harness scripts.
std::string retired_value_hint(const std::string& path,
                               const std::string& value) {
    for (const RetiredValue& retired : k_retired_values) {
        if (retired.path == path && retired.value == value) {
            return " = \"" + value + "\" " + std::string(retired.fate) +
                   "; to reproduce an archived campaign, build the commit its "
                   "PROVENANCE.json names. It";
        }
    }
    return "";
}

// The complaint for a value that fails its key's check, or nullptr.
template <typename T>
const char* complaint(KeyCheck check, T value) {
    switch (check) {
        case KeyCheck::None:
            return nullptr;
        case KeyCheck::Probability:
            return value < T{0} || value > T{1} ? " must be in [0, 1]"
                                                : nullptr;
        case KeyCheck::NonNegative:
            return value < T{0} ? " must be >= 0" : nullptr;
        case KeyCheck::NotNegative:
            return value < T{0} ? " must not be negative" : nullptr;
        case KeyCheck::Positive:
            return value <= T{0} ? " must be positive" : nullptr;
        case KeyCheck::AtLeastOne:
            return value < T{1} ? " must be >= 1" : nullptr;
    }
    return nullptr;
}

template <typename T>
void require(KeyCheck check, T value, const std::string& path) {
    if (const char* problem = complaint(check, value)) {
        throw std::runtime_error("config: " + path + problem);
    }
}

/// Reads the string in `node` into `out` when it is set, rejecting any
/// spelling EnumNames does not list.
template <typename Enum>
void read_enum(toml::node_view<const toml::node> node, const std::string& path,
               Enum& out) {
    const auto val = node.value<std::string>();
    if (!val) {
        return;
    }
    const std::optional<Enum> parsed = enum_from_name<Enum>(*val);
    if (!parsed) {
        throw std::runtime_error("config: " + path +
                                 retired_value_hint(path, *val) + " must be " +
                                 enum_alternatives<Enum>());
    }
    out = *parsed;
}

// Reads one key from its section, if set with the type its member takes. A
// value of another type is ignored rather than rejected, as toml++'s value<T>
// leaves it.
void read_key(const toml::table& section, const ConfigKey& entry, Config& cfg) {
    using std::chrono::milliseconds;
    const auto node = section[entry.key];
    const std::string path = std::string(entry.section) + "." + entry.key;
    std::visit(
        [&](auto member) {
            auto& field = cfg.*member;
            using Field = std::remove_reference_t<decltype(field)>;
            if constexpr (std::is_same_v<Field, double>) {
                if (auto val = node.value<double>()) {
                    require(entry.check, *val, path);
                    field = *val;
                }
            } else if constexpr (std::is_same_v<Field, bool>) {
                if (auto val = node.value<bool>()) {
                    field = *val;
                }
            } else if constexpr (std::is_same_v<Field, std::size_t>) {
                if (auto val = node.value<std::int64_t>()) {
                    require(entry.check, *val, path);
                    field = static_cast<std::size_t>(*val);
                }
            } else if constexpr (std::is_same_v<Field, milliseconds>) {
                if (auto val = node.value<std::int64_t>()) {
                    require(entry.check, *val, path);
                    field = milliseconds{*val};
                }
            } else {
                read_enum(node, path, field);
            }
        },
        entry.member);
}

// The keys k_config_keys declares, as a tree of sections, so that a typo in a
// config file is reported rather than silently ignored. Only
// test_config_io_known_keys_do_not_warn exercises it, and only for keys its
// TOML actually sets, so a new key belongs there too.
struct KeySpec {
    std::set<std::string> keys;
    std::map<std::string, KeySpec> tables;
};

const KeySpec& config_key_spec() {
    static const KeySpec spec = [] {
        KeySpec root;
        for (const ConfigKey& entry : k_config_keys) {
            KeySpec* node = &root;
            for (const std::string_view name : section_path(entry.section)) {
                node = &node->tables[std::string(name)];
            }
            node->keys.insert(entry.key);
        }
        return root;
    }();
    return spec;
}

const toml::table* find_section(const toml::table& root, const char* dotted) {
    const toml::table* table = &root;
    for (const std::string_view name : section_path(dotted)) {
        if (table == nullptr) {
            return nullptr;
        }
        table = (*table)[name].as_table();
    }
    return table;
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
    if (path == "tlsf.mutation.max_assumption_width") {
        return " (removed: an appended assumption's body is one literal)";
    }
    if (path == "tlsf.mutation.p_bare_assumption") {
        return " (removed: an appended fairness assumption is always G F)";
    }
    if (path == "model_counting.metric") {
        return " (removed: trace counts always combine on the logarithmic"
               " metric)";
    }
    if (path == "genetic.accumulate_repairs") {
        return " (removed: every run accumulates gate-passing repairs)";
    }
    if (path == "fitness.keyword_similarity" ||
        path == "fitness.semantic_trace_weight") {
        return " (removed: the keyword orders always join the semantic"
               " objective, at weight 1/2)";
    }
    if (path == "tlsf.repair_mode" || path == "tlsf.muc_max_iterations") {
        return " (removed: TLSF repair always evolves the whole specification;"
               " the mucs tool still extracts cores)";
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

// Rejected rather than read as unlimited: a run with no search budget is what
// the other mode is for, and silently treating zero as unbounded would turn a
// typo into a run that ends only on its deadline. Checked against the final
// values, either of which may have come from the TOML or from its default.
void require_termination_budget(const Config& cfg) {
    if (cfg.termination == TerminationMode::Individuals &&
        cfg.max_individuals == 0) {
        throw std::runtime_error(
            "config: genetic.max_individuals must be at least 1 under "
            "genetic.termination = \"individuals\"");
    }
}

// Elites are a subset of the selected parents, so elitism must be strictly
// smaller than selection. Checked against the final values (either may come
// from the TOML or fall back to its default).
void require_elitism_below_selection(const Config& cfg) {
    if (cfg.elitism_rate >= cfg.selection_rate) {
        throw std::runtime_error(
            "config: genetic.elitism_rate must be less than "
            "genetic.selection_rate");
    }
}

Config apply_toml(const toml::table& tbl) {
    warn_unknown_keys(tbl, config_key_spec(), "");
    Config cfg;
    for (const ConfigKey& entry : k_config_keys) {
        if (const toml::table* section = find_section(tbl, entry.section)) {
            read_key(*section, entry, cfg);
        }
        // The cross-field checks run at fixed points in the read order, so
        // that of several errors in one config the same one is reported.
        if (entry.member == ConfigMember{&Config::termination}) {
            require_termination_budget(cfg);
        } else if (entry.member == ConfigMember{&Config::selection_scheme}) {
            require_elitism_below_selection(cfg);
        }
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
