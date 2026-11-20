// The `.psp` format (PLAN.md D6): JSON of the FRETISH top-level shape, whose
// requirements are a pattern, a scope and the fields P, S, Q and R, or a fixed
// raw-LTL formula. schemas/psp-specification.schema.json describes the same
// format for editors; the checks here are the ones the loader enforces.

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "requirement.hpp"
#include "runner/atom_names.hpp"

namespace psp {

namespace {

using nlohmann::json;

constexpr std::array<PspScopeKind, 5> k_scopes = {
    PspScopeKind::Globally, PspScopeKind::Before, PspScopeKind::After,
    PspScopeKind::Between, PspScopeKind::AfterUntil};

// One of each alternative, at count 1 where it carries one, so that a name
// maps back to its alternative through pattern_name.
const std::array<Pattern, 9>& pattern_prototypes() {
    static const std::array<Pattern, 9> prototypes = {
        pattern::Absence{},
        pattern::Universality{},
        pattern::Existence{},
        pattern::Precedence{},
        pattern::Response{},
        pattern::BoundedResponse{1},
        pattern::BoundedInvariance{1},
        pattern::BoundedExistence{1},
        pattern::BoundedRecurrence{1}};
    return prototypes;
}

std::optional<Pattern> pattern_named(const std::string& name,
                                     std::size_t count) {
    for (const Pattern& prototype : pattern_prototypes()) {
        if (name != pattern_name(prototype)) {
            continue;
        }
        return std::visit(
            [count](auto pat) -> Pattern {
                if constexpr (k_bounded_pattern<decltype(pat)>) {
                    pat.m_count = count;
                }
                return pat;
            },
            prototype);
    }
    return std::nullopt;
}

std::optional<PspScopeKind> scope_named(const std::string& name) {
    for (const PspScopeKind scope : k_scopes) {
        if (name == scope_name(scope)) {
            return scope;
        }
    }
    return std::nullopt;
}

// The error for one formula field, or nothing when it reads.
std::optional<std::string> formula_error(const json& value,
                                         const std::string& path,
                                         bool propositional) {
    if (!value.is_string()) {
        return path + ": must be a string";
    }
    const std::optional<Formula> formula =
        Formula::try_parse(value.get<std::string>());
    if (!formula.has_value()) {
        return path + ": not a formula: '" + value.get<std::string>() + "'";
    }
    if (propositional && !formula->is_propositional()) {
        return path + ": must be propositional";
    }
    return std::nullopt;
}

std::optional<std::string> fixed_error(const json& req,
                                       const std::string& path) {
    if (req.size() != 1) {
        for (const auto& [key, value] : req.items()) {
            if (key != "fixed") {
                std::string err = path;
                err.append(".").append(key).append(
                    ": a fixed requirement takes no other field");
                return err;
            }
        }
    }
    return formula_error(req.at("fixed"), path + ".fixed", false);
}

// The error for the count of @p pattern, or nothing.
std::optional<std::string> count_error(const json& req, const Pattern& pattern,
                                       const std::string& path) {
    if (pattern_count(pattern).has_value()) {
        if (!req.contains("count") || !req.at("count").is_number_unsigned() ||
            req.at("count").get<std::size_t>() == 0 ||
            req.at("count").get<std::size_t>() > k_max_count) {
            return path + ".count: must be an integer from 1 to " +
                   std::to_string(k_max_count);
        }
    } else if (req.contains("count")) {
        return path + ".count: " + req.at("pattern").get<std::string>() +
               " takes no count";
    }
    return std::nullopt;
}

// The scope of @p req, globally when absent, or nothing when it names none.
std::optional<PspScopeKind> scope_of(const json& req) {
    if (!req.contains("scope")) {
        return PspScopeKind::Globally;
    }
    if (!req.at("scope").is_string()) {
        return std::nullopt;
    }
    return scope_named(req.at("scope").get<std::string>());
}

// The error for P, S, Q and R: each present exactly where read.
std::optional<std::string> fields_error(const json& req, const Pattern& pattern,
                                        PspScopeKind scope,
                                        const std::string& path) {
    const std::array<std::pair<const char*, bool>, 4> fields = {{
        {"p", pattern_uses_p(pattern)},
        {"s", pattern_uses_s(pattern)},
        {"q", scope_uses_q(scope)},
        {"r", scope_uses_r(scope)},
    }};
    for (const auto& [field, used] : fields) {
        const std::string field_path = path + "." + field;
        if (!used) {
            // Dropping it would let the file say one thing and mean another.
            if (req.contains(field)) {
                return field_path + ": not read by this pattern and scope";
            }
            continue;
        }
        if (!req.contains(field)) {
            return field_path + ": missing required field";
        }
        if (auto err = formula_error(req.at(field), field_path, true)) {
            return err;
        }
    }
    return std::nullopt;
}

std::optional<std::string> requirement_error(const json& req,
                                             const std::string& path) {
    if (!req.is_object()) {
        return path + ": expected object";
    }
    if (req.contains("fixed")) {
        return fixed_error(req, path);
    }
    static const std::unordered_set<std::string> known = {
        "pattern", "count", "scope", "p", "s", "q", "r", "weakenable"};
    for (const auto& [key, value] : req.items()) {
        if (known.count(key) == 0) {
            std::string err = path;
            err.append(".").append(key).append(": unknown field");
            return err;
        }
    }
    if (!req.contains("pattern") || !req.at("pattern").is_string()) {
        return path + ".pattern: must be a string";
    }
    const std::optional<Pattern> pattern =
        pattern_named(req.at("pattern").get<std::string>(), 1);
    if (!pattern.has_value()) {
        return path + ".pattern: unknown value '" +
               req.at("pattern").get<std::string>() + "'";
    }
    if (auto err = count_error(req, *pattern, path)) {
        return err;
    }
    const std::optional<PspScopeKind> scope = scope_of(req);
    if (!scope.has_value()) {
        return path +
               ".scope: must be one of globally, before, after, "
               "between, afteruntil";
    }
    if (auto err = fields_error(req, *pattern, *scope, path)) {
        return err;
    }
    if (req.contains("weakenable") && !req.at("weakenable").is_boolean()) {
        return path + ".weakenable: must be a boolean";
    }
    return std::nullopt;
}
std::optional<std::string> string_array_error(const json& jobj,
                                              const char* field) {
    if (!jobj.contains(field) || !jobj.at(field).is_array()) {
        return std::string(field) + ": must be an array";
    }
    for (std::size_t i = 0; i < jobj.at(field).size(); ++i) {
        const json& name = jobj.at(field).at(i);
        if (!name.is_string() || name.get<std::string>().empty()) {
            return std::string(field) + "[" + std::to_string(i) +
                   "]: must be a non-empty string";
        }
    }
    return std::nullopt;
}

std::optional<std::string> document_error(const json& jobj) {
    if (!jobj.is_object()) {
        return std::string("root: expected object");
    }
    static const std::unordered_set<std::string> known = {
        "$schema", "assumptions", "guarantees", "in_atoms", "out_atoms"};
    for (const auto& [key, value] : jobj.items()) {
        if (known.count(key) == 0) {
            return key + ": unknown field";
        }
    }
    for (const char* field : {"in_atoms", "out_atoms"}) {
        if (auto err = string_array_error(jobj, field)) {
            return err;
        }
    }
    for (const char* field : {"assumptions", "guarantees"}) {
        if (!jobj.contains(field) || !jobj.at(field).is_array()) {
            return std::string(field) + ": must be an array";
        }
        for (std::size_t i = 0; i < jobj.at(field).size(); ++i) {
            if (auto err = requirement_error(
                    jobj.at(field).at(i),
                    std::string(field) + "[" + std::to_string(i) + "]")) {
                return err;
            }
        }
    }
    return std::nullopt;
}

std::optional<Formula> optional_formula(const json& req, const char* field) {
    if (!req.contains(field)) {
        return std::nullopt;
    }
    return Formula(req.at(field).get<std::string>());
}

// Only called on a requirement document_error passed.
Requirement requirement_from_json(const json& req) {
    if (req.contains("fixed")) {
        return Requirement::fixed(Formula(req.at("fixed").get<std::string>()));
    }
    const std::size_t count = req.value("count", std::size_t{1});
    std::optional<Pattern> pattern =
        pattern_named(req.at("pattern").get<std::string>(), count);
    std::optional<PspScopeKind> scope =
        scope_named(req.value("scope", std::string("globally")));
    if (!pattern.has_value() || !scope.has_value()) {
        throw std::logic_error(
            "psp: requirement_from_json on an invalid "
            "requirement");
    }
    return {*std::move(pattern),          *scope,
            optional_formula(req, "p"),   optional_formula(req, "s"),
            optional_formula(req, "q"),   optional_formula(req, "r"),
            req.value("weakenable", true)};
}

json requirement_to_json(const Requirement& req) {
    if (req.m_fixed) {
        return {{"fixed", req.m_fixed->to_string()}};
    }
    json out = {{"pattern", pattern_name(req.m_pattern)},
                {"scope", scope_name(req.m_scope)}};
    if (const std::optional<std::size_t> count = pattern_count(req.m_pattern)) {
        out["count"] = *count;
    }
    const std::array<std::pair<const char*, const std::optional<Formula>*>, 4>
        fields = {{{"p", &req.m_p},
                   {"s", &req.m_s},
                   {"q", &req.m_q},
                   {"r", &req.m_r}}};
    for (const auto& [name, field] : fields) {
        if (field->has_value()) {
            out[name] = (*field)->to_string();
        }
    }
    // Emitted only when locked, as on the FRETISH side.
    if (!req.m_weakenable) {
        out["weakenable"] = false;
    }
    return out;
}

// Every atom a requirement names, the constants excepted.
void collect_requirement_atoms(const Requirement& req,
                               std::unordered_set<std::string>& out) {
    for (const std::optional<Formula>* field :
         {&req.m_p, &req.m_s, &req.m_q, &req.m_r, &req.m_fixed}) {
        if (field->has_value()) {
            collect_atoms(**field, out);
        }
    }
}

// An undeclared atom would reach ltlsynt on neither side, and so silently as
// an output; the FRETISH loader rejects an undeclared mode for the same
// reason.
std::optional<std::string> undeclared_atom(const Specification& spec) {
    std::unordered_set<std::string> used;
    for (const std::vector<Requirement>* reqs :
         {&spec.m_assumptions, &spec.m_guarantees}) {
        for (const Requirement& req : *reqs) {
            collect_requirement_atoms(req, used);
        }
    }
    std::vector<std::string> sorted(used.begin(), used.end());
    std::sort(sorted.begin(), sorted.end());
    const auto declared = [&spec](const std::string& name) {
        return std::find(spec.m_in_atoms.begin(), spec.m_in_atoms.end(),
                         name) != spec.m_in_atoms.end() ||
               std::find(spec.m_out_atoms.begin(), spec.m_out_atoms.end(),
                         name) != spec.m_out_atoms.end();
    };
    for (const std::string& name : sorted) {
        if (!is_constant_atom(name) && !declared(name)) {
            return "atom '" + name +
                   "' is declared in neither in_atoms nor out_atoms";
        }
    }
    return std::nullopt;
}

// The unprefixed specification, or the first reason the text is not one.
std::pair<std::optional<Specification>, std::string> read(
    const std::string& text) {
    json jobj;
    try {
        jobj = json::parse(text);
    } catch (const json::parse_error& exc) {
        return {std::nullopt, std::string("JSON parse error: ") + exc.what()};
    }
    if (const std::optional<std::string> err = document_error(jobj)) {
        return {std::nullopt, *err};
    }
    const auto parse_reqs = [](const json& arr) {
        std::vector<Requirement> reqs;
        reqs.reserve(arr.size());
        for (const json& req : arr) {
            reqs.push_back(requirement_from_json(req));
        }
        return reqs;
    };
    Specification spec(parse_reqs(jobj.at("assumptions")),
                       parse_reqs(jobj.at("guarantees")),
                       jobj.at("in_atoms").get<std::vector<std::string>>(),
                       jobj.at("out_atoms").get<std::vector<std::string>>());
    if (const std::optional<std::string> err = undeclared_atom(spec)) {
        return {std::nullopt, *err};
    }
    // Screened after the prefix, as `realize` screens a FRETISH
    // specification, so the rule left to apply is the uppercase-input one.
    const Specification prefixed = add_atom_prefix(spec);
    if (const std::optional<std::string> unsafe =
            runner::first_unsafe_atom_name(prefixed.m_in_atoms,
                                           prefixed.m_out_atoms)) {
        return {std::nullopt, *unsafe};
    }
    return {std::move(spec), std::string()};
}

}  // namespace

std::string first_format_error(const std::string& text) {
    return read(text).second;
}

Specification parse_specification(const std::string& text) {
    auto [spec, err] = read(text);
    if (!spec.has_value()) {
        throw std::invalid_argument(err);
    }
    return add_atom_prefix(*spec);
}

Specification load_specification(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open input file: " + path);
    }
    std::ostringstream text;
    text << file.rdbuf();
    try {
        return parse_specification(text.str());
    } catch (const std::invalid_argument& exc) {
        throw std::runtime_error("invalid specification in " + path + ": " +
                                 exc.what());
    }
}

std::string write_specification(const Specification& spec) {
    const Specification stripped = strip_atom_prefix(spec);
    const auto live = [](const std::vector<Requirement>& reqs) {
        json out = json::array();
        for (const Requirement& req : reqs) {
            if (!req.m_removed) {
                out.push_back(requirement_to_json(req));
            }
        }
        return out;
    };
    const json out = {{"assumptions", live(stripped.m_assumptions)},
                      {"guarantees", live(stripped.m_guarantees)},
                      {"in_atoms", stripped.m_in_atoms},
                      {"out_atoms", stripped.m_out_atoms}};
    return out.dump(2) + "\n";
}

}  // namespace psp
