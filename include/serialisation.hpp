#pragma once

/// @file serialisation.hpp
/// @brief JSON serialisation and deserialisation for Timing, Requirement,
///        Specification, and related types via nlohmann/json.

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "requirement.hpp"

// --- Timing (functions in timing:: so ADL finds them for
// std::variant<timing::*>) ---

namespace timing {

inline void to_json(nlohmann::json& jobj, const Timing& tim) {
    std::visit(
        [&jobj](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, Immediately>) {
                jobj = {{"type", "Immediately"}};
            } else if constexpr (std::is_same_v<T, NextTimepoint>) {
                jobj = {{"type", "NextTimepoint"}};
            } else if constexpr (std::is_same_v<T, WithinTicks>) {
                jobj = {{"type", "WithinTicks"}, {"ticks", val.m_ticks}};
            } else if constexpr (std::is_same_v<T, ForTicks>) {
                jobj = {{"type", "ForTicks"}, {"ticks", val.m_ticks}};
            } else if constexpr (std::is_same_v<T, AfterTicks>) {
                jobj = {{"type", "AfterTicks"}, {"ticks", val.m_ticks}};
            } else if constexpr (std::is_same_v<T, Eventually>) {
                jobj = {{"type", "Eventually"}};
            } else if constexpr (std::is_same_v<T, Always>) {
                jobj = {{"type", "Always"}};
            } else if constexpr (std::is_same_v<T, Until>) {
                jobj = {{"type", "Until"}, {"stop", val.m_stop.to_string()}};
            } else if constexpr (std::is_same_v<T, Before>) {
                jobj = {{"type", "Before"}, {"stop", val.m_stop.to_string()}};
            } else {
                static_assert(k_unhandled_timing<T>);
            }
        },
        tim);
}

inline void from_json(const nlohmann::json& jobj, Timing& tim) {
    const std::string type = jobj.at("type").get<std::string>();
    if (type == "Immediately") {
        tim = immediately();
    } else if (type == "NextTimepoint") {
        tim = next_timepoint();
    } else if (type == "WithinTicks") {
        tim = within_ticks(jobj.at("ticks").get<std::size_t>());
    } else if (type == "ForTicks") {
        tim = for_ticks(jobj.at("ticks").get<std::size_t>());
    } else if (type == "AfterTicks") {
        tim = after_ticks(jobj.at("ticks").get<std::size_t>());
    } else if (type == "Eventually") {
        tim = eventually();
    } else if (type == "Always") {
        tim = always();
    } else if (type == "Until") {
        tim = until(Formula(jobj.at("stop").get<std::string>()));
    } else if (type == "Before") {
        tim = before(Formula(jobj.at("stop").get<std::string>()));
    } else {
        throw std::invalid_argument("unknown timing type: " + type);
    }
}

}  // namespace timing

// --- Scope ---

inline const char* scope_kind_name(ScopeKind kind) {
    switch (kind) {
        case ScopeKind::Global:
            return "Global";
        case ScopeKind::In:
            return "In";
        case ScopeKind::NotIn:
            return "NotIn";
        case ScopeKind::Before:
            return "Before";
        case ScopeKind::After:
            return "After";
        case ScopeKind::OnlyIn:
            return "OnlyIn";
        case ScopeKind::OnlyBefore:
            return "OnlyBefore";
        case ScopeKind::OnlyAfter:
            return "OnlyAfter";
    }
    return "Global";
}

/// Every ScopeKind, in declaration order. The JSON reader and the schema check
/// both walk this, so a new kind reaches both by being added here.
inline const std::array<ScopeKind, 8>& scope_kinds() {
    static const std::array<ScopeKind, 8> kinds = {
        ScopeKind::Global,     ScopeKind::In,       ScopeKind::NotIn,
        ScopeKind::Before,     ScopeKind::After,    ScopeKind::OnlyIn,
        ScopeKind::OnlyBefore, ScopeKind::OnlyAfter};
    return kinds;
}

inline void to_json(nlohmann::json& jobj, const Scope& scope) {
    jobj = {{"type", scope_kind_name(scope.m_kind)}};
    if (!scope.is_global()) {
        jobj["mode"] = scope.m_mode;
    }
}

inline void from_json(const nlohmann::json& jobj, Scope& scope) {
    const std::string type = jobj.at("type").get<std::string>();
    for (const ScopeKind kind : scope_kinds()) {
        if (type != scope_kind_name(kind)) {
            continue;
        }
        scope.m_kind = kind;
        scope.m_mode = kind == ScopeKind::Global
                           ? std::string()
                           : jobj.at("mode").get<std::string>();
        return;
    }
    throw std::invalid_argument("unknown scope type: " + type);
}

// --- Requirement ---

inline void to_json(nlohmann::json& jobj, const Requirement& req) {
    // Atom names carry the internal k_atom_prefix in memory; strip it so the
    // serialised form shows the user's original names.
    const Requirement stripped = strip_atom_prefix(req);
    jobj = {{"condition", stripped.m_condition.to_string()},
            {"condition-type",
             stripped.m_condition_type == ConditionType::Trigger ? "trigger"
                                                                 : "continual"},
            {"response", stripped.m_response.to_string()},
            {"timing", stripped.m_timing}};
    // Emitted only when locked; absence round-trips to the default
    // (weakenable).
    if (!stripped.m_weakenable) {
        jobj["weakenable"] = false;
    }
    // Emitted only when scoped, so every specification written before scopes
    // existed round-trips byte for byte; absence means Global, which is also
    // what an omitted FRET scope means.
    if (!stripped.m_scope.is_global()) {
        jobj["scope"] = stripped.m_scope;
    }
}

// --- Specification ---

inline void to_json(nlohmann::json& jobj, const Specification& spc) {
    // Removed requirements are dropped rather than marked. JSON is the boundary
    // where a specification stops being search state and becomes a document,
    // and a deleted requirement's meaning there is absence. Emitting a flagged
    // entry would leave every reader that does not know the flag reading a
    // requirement the repair removed.
    const auto live = [](const std::vector<Requirement>& reqs) {
        std::vector<Requirement> kept;
        kept.reserve(reqs.size());
        std::copy_if(reqs.begin(), reqs.end(), std::back_inserter(kept),
                     [](const Requirement& req) { return !req.m_removed; });
        return kept;
    };
    // Requirements are stripped by to_json(Requirement) as they serialise; the
    // atom vectors have no such hook, so strip them here (once, on this node).
    const Specification stripped = strip_atom_prefix(spc);
    jobj = {{"assumptions", live(spc.m_assumptions)},
            {"guarantees", live(spc.m_guarantees)},
            {"in_atoms", stripped.m_in_atoms},
            {"out_atoms", stripped.m_out_atoms}};
    // Same rule as a requirement's scope: absent unless used, so an unscoped
    // specification is written exactly as it was before modes existed.
    if (!stripped.m_modes.empty()) {
        jobj["modes"] = stripped.m_modes;
    }
}

namespace serialisation {

/// Constructs a Requirement from its JSON representation. Cannot be expressed
/// as a from_json ADL overload because Requirement is not
/// default-constructible.
inline Requirement requirement_from_json(const nlohmann::json& jobj) {
    const auto ctype_str = jobj.at("condition-type").get<std::string>();
    const ConditionType ctype = ctype_str == "trigger"
                                    ? ConditionType::Trigger
                                    : ConditionType::Continual;
    Formula response(jobj.at("response").get<std::string>());
    const nlohmann::json& timing_json = jobj.at("timing");
    // FRET's `never r` is `always !r`. It loads as that respelling rather than
    // as a kind of its own, which would split the cache keys of one obligation
    // and double the mutation grammar for it, so a repair prints `always !r`.
    Timing timing = timing::always();
    if (timing_json.at("type").get<std::string>() == "Never") {
        response = Formula::make_unary(Formula::Kind::Not, response);
    } else {
        timing = timing_json.get<Timing>();
    }
    return Requirement(
        Formula(jobj.at("condition").get<std::string>()), std::move(response),
        std::move(timing), ctype, jobj.value("weakenable", true), false,
        jobj.contains("scope") ? jobj.at("scope").get<Scope>() : Scope{});
}

}  // namespace serialisation

inline void from_json(const nlohmann::json& jobj, Specification& spc) {
    auto parse_reqs = [](const nlohmann::json& arr) {
        std::vector<Requirement> reqs;
        reqs.reserve(arr.size());
        for (const auto& req_json : arr) {
            reqs.push_back(serialisation::requirement_from_json(req_json));
        }
        return reqs;
    };
    spc = Specification(parse_reqs(jobj.at("assumptions")),
                        parse_reqs(jobj.at("guarantees")),
                        jobj.at("in_atoms").get<std::vector<std::string>>(),
                        jobj.at("out_atoms").get<std::vector<std::string>>(),
                        jobj.value("modes", std::vector<std::string>{}));
}

namespace serialisation {

/// Per-component breakdown entry stored alongside a scored specification.
struct ComponentScore {
    std::string name;
    double score = 0.0;
    double weight = 0.0;
};

/// Fitness breakdown attached to a serialised result.
struct FitnessRecord {
    double total = 0.0;
    std::vector<ComponentScore> components;
};

/// A specification paired with an optional fitness record for result storage.
struct ScoredSpecification {
    Specification spec;
    std::optional<FitnessRecord> fitness;
};

inline void to_json(nlohmann::json& jobj, const ComponentScore& cmp) {
    jobj = {{"name", cmp.name}, {"score", cmp.score}, {"weight", cmp.weight}};
}

inline void from_json(const nlohmann::json& jobj, ComponentScore& cmp) {
    cmp.name = jobj.at("name").get<std::string>();
    cmp.score = jobj.at("score").get<double>();
    cmp.weight = jobj.at("weight").get<double>();
}

inline void to_json(nlohmann::json& jobj, const FitnessRecord& frec) {
    jobj = {{"total", frec.total}, {"components", frec.components}};
}

inline void from_json(const nlohmann::json& jobj, FitnessRecord& frec) {
    frec.total = jobj.at("total").get<double>();
    frec.components = jobj.at("components").get<std::vector<ComponentScore>>();
}

inline void to_json(nlohmann::json& jobj, const ScoredSpecification& ssc) {
    ::to_json(jobj, ssc.spec);
    if (ssc.fitness.has_value()) {
        jobj["fitness"] = *ssc.fitness;
    }
}

inline void from_json(const nlohmann::json& jobj, ScoredSpecification& ssc) {
    ::from_json(jobj, ssc.spec);
    if (jobj.contains("fitness")) {
        ssc.fitness = jobj.at("fitness").get<FitnessRecord>();
    }
}

}  // namespace serialisation

namespace detail {

inline std::optional<std::string> validate_timing_json(
    const nlohmann::json& timing, const std::string& path) {
    if (!timing.is_object()) {
        return path + ": must be an object";
    }
    if (!timing.contains("type") || !timing.at("type").is_string()) {
        return path + ".type: must be a string";
    }
    const std::string ttype = timing.at("type").get<std::string>();
    if (ttype != "Immediately" && ttype != "NextTimepoint" &&
        ttype != "WithinTicks" && ttype != "ForTicks" &&
        ttype != "AfterTicks" && ttype != "Eventually" && ttype != "Always" &&
        ttype != "Until" && ttype != "Before" && ttype != "Never") {
        return path + ".type: unknown value '" + ttype + "'";
    }
    if (ttype == "Until" || ttype == "Before") {
        if (!timing.contains("stop") || !timing.at("stop").is_string()) {
            return path + ".stop: must be a string";
        }
        return std::nullopt;
    }
    if (ttype != "WithinTicks" && ttype != "ForTicks" &&
        ttype != "AfterTicks") {
        return std::nullopt;
    }
    if (!timing.contains("ticks") || !timing.at("ticks").is_number_unsigned()) {
        return path + ".ticks: must be a non-negative integer";
    }
    return std::nullopt;
}

inline std::optional<std::string> validate_scope_json(
    const nlohmann::json& scope, const std::string& path) {
    if (!scope.is_object()) {
        return path + ": must be an object";
    }
    if (!scope.contains("type") || !scope.at("type").is_string()) {
        return path + ".type: must be a string";
    }
    const std::string stype = scope.at("type").get<std::string>();
    const auto& kinds = scope_kinds();
    const bool known = std::any_of(
        kinds.begin(), kinds.end(),
        [&stype](ScopeKind kind) { return stype == scope_kind_name(kind); });
    if (!known) {
        return path + ".type: unknown value '" + stype + "'";
    }
    // Every kind but Global names a mode, and Global names none. A mode on a
    // Global scope would be dropped on load, so it is rejected rather than
    // ignored: silently discarding it would let a specification say one thing
    // and mean another.
    if (stype == "Global") {
        if (scope.contains("mode")) {
            return path + ".mode: Global scope takes no mode";
        }
        return std::nullopt;
    }
    if (!scope.contains("mode") || !scope.at("mode").is_string() ||
        scope.at("mode").get<std::string>().empty()) {
        return path + ".mode: must be a non-empty string";
    }
    return std::nullopt;
}

inline std::optional<std::string> validate_requirement_json(
    const nlohmann::json& req, const std::string& path) {
    if (!req.is_object()) {
        return path + ": expected object";
    }
    for (const char* field : {"condition", "response"}) {
        if (!req.contains(field)) {
            return path + "." + field + ": missing required field";
        }
        if (!req.at(field).is_string()) {
            return path + "." + field + ": must be a string";
        }
    }
    if (!req.contains("condition-type")) {
        return path + ".condition-type: missing required field";
    }
    if (!req.at("condition-type").is_string()) {
        return path + ".condition-type: must be a string";
    }
    {
        const std::string ctype = req.at("condition-type").get<std::string>();
        if (ctype != "trigger" && ctype != "continual") {
            return path + ".condition-type: unknown value '" + ctype + "'";
        }
    }
    if (!req.contains("timing")) {
        return path + ".timing: missing required field";
    }
    if (auto err = validate_timing_json(req.at("timing"), path + ".timing")) {
        return err;
    }
    if (req.contains("weakenable") && !req.at("weakenable").is_boolean()) {
        return path + ".weakenable: must be a boolean";
    }
    if (req.contains("scope")) {
        if (auto err = validate_scope_json(req.at("scope"), path + ".scope")) {
            return err;
        }
    }
    return std::nullopt;
}

/// Checks the declared modes against the scopes that use them, and against the
/// two atom lists. Modes are their own namespace, so a name in both is
/// ambiguous rather than merely redundant: the lowering would emit one atom
/// while ltlsynt was told about two different signals.
///
/// This is the one cross-field check in the file. Nothing validates that a
/// condition or response mentions only declared atoms, but a scope's mode has
/// to be declared, since it is what decides the mode reaches ltlsynt's input
/// side at all — an undeclared one falls through to the output side, where the
/// synthesised system can discharge the scope by never entering the mode.
inline std::optional<std::string> validate_modes_json(
    const nlohmann::json& jobj) {
    std::vector<std::string> declared;
    if (jobj.contains("modes")) {
        declared = jobj.at("modes").get<std::vector<std::string>>();
    }
    const auto is_declared = [&declared](const std::string& name) {
        return std::find(declared.begin(), declared.end(), name) !=
               declared.end();
    };
    for (const char* field : {"in_atoms", "out_atoms"}) {
        for (const auto& atom : jobj.at(field)) {
            if (atom.is_string() && is_declared(atom.get<std::string>())) {
                return std::string("modes: '") + atom.get<std::string>() +
                       "' is also declared in " + field;
            }
        }
    }
    for (const char* field : {"assumptions", "guarantees"}) {
        const nlohmann::json& reqs = jobj.at(field);
        for (std::size_t i = 0; i < reqs.size(); ++i) {
            const nlohmann::json& req = reqs.at(i);
            if (!req.contains("scope") || !req.at("scope").contains("mode")) {
                continue;
            }
            const auto mode = req.at("scope").at("mode").get<std::string>();
            if (!is_declared(mode)) {
                return std::string(field) + "[" + std::to_string(i) +
                       "].scope.mode: '" + mode + "' is not declared in modes";
            }
        }
    }
    return std::nullopt;
}

inline std::optional<std::string> validate_string_array_json(
    const nlohmann::json& jobj, const char* field) {
    if (!jobj.contains(field)) {
        return std::string(field) + ": missing required field";
    }
    if (!jobj.at(field).is_array()) {
        return std::string(field) + ": must be an array";
    }
    for (std::size_t i = 0; i < jobj.at(field).size(); ++i) {
        if (!jobj.at(field).at(i).is_string()) {
            return std::string(field) + "[" + std::to_string(i) +
                   "]: must be a string";
        }
    }
    return std::nullopt;
}

inline std::optional<std::string> validate_requirement_array_json(
    const nlohmann::json& jobj, const char* field) {
    if (!jobj.contains(field)) {
        return std::string(field) + ": missing required field";
    }
    if (!jobj.at(field).is_array()) {
        return std::string(field) + ": must be an array";
    }
    for (std::size_t i = 0; i < jobj.at(field).size(); ++i) {
        const std::string epath =
            std::string(field) + "[" + std::to_string(i) + "]";
        if (auto err = validate_requirement_json(jobj.at(field).at(i), epath)) {
            return err;
        }
    }
    return std::nullopt;
}

inline std::optional<std::string> validate_fitness_json(
    const nlohmann::json& fitness) {
    if (!fitness.is_object()) {
        return "fitness: must be an object";
    }
    if (!fitness.contains("total") || !fitness.at("total").is_number()) {
        return "fitness.total: must be a number";
    }
    if (!fitness.contains("components") ||
        !fitness.at("components").is_array()) {
        return "fitness.components: must be an array";
    }
    for (std::size_t i = 0; i < fitness.at("components").size(); ++i) {
        const nlohmann::json& comp = fitness.at("components").at(i);
        const std::string cpath =
            "fitness.components[" + std::to_string(i) + "]";
        if (!comp.is_object()) {
            return cpath + ": expected object";
        }
        if (!comp.contains("name") || !comp.at("name").is_string()) {
            return cpath + ".name: must be a string";
        }
        if (!comp.contains("score") || !comp.at("score").is_number()) {
            return cpath + ".score: must be a number";
        }
        if (!comp.contains("weight") || !comp.at("weight").is_number()) {
            return cpath + ".weight: must be a number";
        }
    }
    return std::nullopt;
}

}  // namespace detail

/// Validates that @p jobj conforms to the Specification JSON schema.
///
/// Checks required fields (assumptions, guarantees, in_atoms, out_atoms) and
/// their types, validates each requirement's condition/condition-type/response/
/// timing, and validates the optional fitness block if present.
///
/// @return A human-readable error description, or std::nullopt if the object
///         is valid.
inline std::optional<std::string> validate_specification_json(
    const nlohmann::json& jobj) {
    if (!jobj.is_object()) {
        return std::string("root: expected object");
    }
    for (const char* field : {"in_atoms", "out_atoms"}) {
        if (auto err = detail::validate_string_array_json(jobj, field)) {
            return err;
        }
    }
    for (const char* field : {"assumptions", "guarantees"}) {
        if (auto err = detail::validate_requirement_array_json(jobj, field)) {
            return err;
        }
    }
    if (jobj.contains("modes")) {
        if (auto err = detail::validate_string_array_json(jobj, "modes")) {
            return err;
        }
    }
    if (auto err = detail::validate_modes_json(jobj)) {
        return err;
    }
    if (jobj.contains("fitness")) {
        return detail::validate_fitness_json(jobj.at("fitness"));
    }
    return std::nullopt;
}

/// Reads a JSON file at @p path and deserialises it as a ScoredSpecification.
/// Throws std::runtime_error on I/O failure, malformed JSON, or schema
/// violations.
inline serialisation::ScoredSpecification load_scored_specification(
    const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open input file: " + path);
    }
    nlohmann::json json_in;
    try {
        file >> json_in;
    } catch (const nlohmann::json::parse_error& exc) {
        throw std::runtime_error("JSON parse error in " + path + ": " +
                                 exc.what());
    }
    if (const auto err = validate_specification_json(json_in)) {
        throw std::runtime_error("invalid specification in " + path + ": " +
                                 *err);
    }
    try {
        auto scored = json_in.get<serialisation::ScoredSpecification>();
        scored.spec = add_atom_prefix(scored.spec);
        return scored;
    } catch (const nlohmann::json::exception& exc) {
        throw std::runtime_error("invalid specification in " + path + ": " +
                                 exc.what());
    }
}

/// Reads a JSON file at @p path and deserialises it as a Specification,
/// discarding any fitness record. Throws as load_scored_specification does.
inline Specification load_specification(const std::string& path) {
    return load_scored_specification(path).spec;
}

/// Parses @p text as a Specification, for a caller that has read the file
/// itself. Throws nlohmann::json::parse_error on malformed JSON,
/// std::invalid_argument naming the first schema violation, and
/// nlohmann::json::exception if deserialisation fails after validation.
inline Specification parse_specification_json(const std::string& text) {
    const nlohmann::json jobj = nlohmann::json::parse(text);
    if (const std::optional<std::string> err =
            validate_specification_json(jobj)) {
        throw std::invalid_argument(*err);
    }
    return add_atom_prefix(jobj.get<Specification>());
}
