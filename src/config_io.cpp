#include "config_io.hpp"

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
        case KeyCheck::WidthAtLeastOne:
            return value < T{1} ? " must be at least 1" : nullptr;
    }
    return nullptr;
}

template <typename T>
void require(KeyCheck check, T value, const std::string& path) {
    if (const char* problem = complaint(check, value)) {
        const char* prefix =
            check == KeyCheck::WidthAtLeastOne ? "" : "config: ";
        throw std::runtime_error(prefix + path + problem);
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
        if constexpr (std::is_same_v<Enum, SelectionScheme>) {
            throw std::runtime_error(
                "config: genetic.selection_scheme " +
                retired_scheme_hint(*val) +
                "must be \"weighted\", \"nsga2-truncate\", or "
                "\"nsga2-apportion\"");
        }
        throw std::runtime_error("config: " + path + " must be " +
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
