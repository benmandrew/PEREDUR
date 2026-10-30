#include "config/reference.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <nlohmann/json.hpp>

#include "config/enum_names.hpp"
#include "config/keys.hpp"
#include "repair/manifest.hpp"

namespace {

using ordered_json = nlohmann::ordered_json;

constexpr const char* k_schema_path = "schemas/config-schema.json";
constexpr const char* k_example_path = "example-config.toml";

constexpr const char* k_example_header =
    "#:schema ./schemas/config-schema.json\n"
    "# PEREDUR configuration file\n"
    "#\n"
    "# Every section and key is optional; absent keys use the built-in "
    "defaults. Every\n"
    "# value below IS that default, so copying this file whole changes "
    "nothing.\n"
    "# This file is generated from src/config/keys.hpp; regenerate it with\n"
    "# `cmake --build build --target config-reference` rather than editing "
    "it.\n"
    "#\n"
    "# What each key means is in schemas/config-schema.json, which an editor "
    "reads via\n"
    "# the #:schema line above. What to set and why -- the measurements "
    "behind each\n"
    "# default -- is in docs/configuration.rst. Comments here cover only what "
    "neither\n"
    "# of those records.\n";

// A key with no constant default, which the template leaves commented out.
bool has_fixed_default(const ConfigKey& entry) {
    return !(entry.member == ConfigMember{&Config::parallel});
}

ordered_json key_schema(const ConfigKey& entry, const nlohmann::json& value) {
    ordered_json node;
    std::visit(
        [&](auto member) {
            using Field =
                std::decay_t<decltype(std::declval<Config>().*member)>;
            if constexpr (std::is_same_v<Field, double>) {
                node["type"] = "number";
            } else if constexpr (std::is_same_v<Field, bool>) {
                node["type"] = "boolean";
            } else if constexpr (std::is_enum_v<Field>) {
                node["type"] = "string";
                for (const auto& [unused, name] : EnumNames<Field>::k_names) {
                    node["enum"].push_back(name);
                }
            } else {
                node["type"] = "integer";
            }
        },
        entry.member);
    switch (entry.check) {
        case KeyCheck::None:
            break;
        case KeyCheck::Probability:
            node["minimum"] = 0;
            node["maximum"] = 1;
            break;
        case KeyCheck::NonNegative:
            node["minimum"] = 0;
            break;
        case KeyCheck::Positive:
            node["minimum"] = 1;
            break;
    }
    if (has_fixed_default(entry)) {
        node["default"] = value;
    }
    node["description"] = entry.description;
    return node;
}

// JSON with one member per line, except that an object holding no object, as
// each key's schema does, stays on one line.
std::string render(const ordered_json& node, const std::string& indent) {
    if (!node.is_structured()) {
        return node.dump();
    }
    bool flat = true;
    for (const auto& child : node) {
        flat = flat && !child.is_object();
    }
    const std::string inner = indent + "  ";
    std::string text;
    for (const auto& [name, child] : node.items()) {
        if (!text.empty()) {
            text += flat ? ", " : ",\n";
        }
        text += flat ? "" : inner;
        text += node.is_object() ? ordered_json(name).dump() + ": " : "";
        text += render(child, inner);
    }
    if (node.is_array()) {
        return "[" + text + "]";
    }
    return flat ? "{ " + text + " }" : "{\n" + text + "\n" + indent + "}";
}

std::string schema_text() {
    const nlohmann::json defaults = config_json(Config{});
    ordered_json root{{"$schema", "http://json-schema.org/draft-07/schema#"},
                      {"title", "PEREDUR config"},
                      {"type", "object"},
                      {"additionalProperties", false},
                      {"properties", ordered_json::object()}};
    for (const ConfigKey& entry : k_config_keys) {
        ordered_json* table = &root;
        for (const std::string_view name : section_path(entry.section)) {
            ordered_json& section = (*table)["properties"][std::string(name)];
            if (section.is_null()) {
                section = {{"type", "object"}, {"additionalProperties", false}};
                if (name == "tlsf") {
                    section["description"] =
                        "TLSF-mode settings (a .tlsf input or --format tlsf).";
                }
                section["properties"] = ordered_json::object();
            }
            table = &section;
        }
        (*table)["properties"][entry.key] =
            key_schema(entry, defaults.at(config_pointer(entry)));
    }
    return render(root, "") + "\n";
}

std::string example_text() {
    const nlohmann::json defaults = config_json(Config{});
    std::ostringstream out;
    out << k_example_header;
    std::string_view section;
    for (const ConfigKey& entry : k_config_keys) {
        if (section != entry.section) {
            section = entry.section;
            out << "\n[" << section << "]\n";
        }
        std::istringstream note(entry.note);
        for (std::string line; std::getline(note, line);) {
            out << (line.empty() ? "#" : "# " + line) << "\n";
        }
        if (has_fixed_default(entry)) {
            out << entry.key << " = "
                << defaults.at(config_pointer(entry)).dump() << "\n";
        } else {
            out << "# " << entry.key << " = 8\n";
        }
    }
    return out.str();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

int config_reference(const std::filesystem::path& root, bool check) {
    int status = 0;
    for (const auto& [relative, text] :
         {std::pair{k_schema_path, schema_text()},
          std::pair{k_example_path, example_text()}}) {
        const std::filesystem::path path = root / relative;
        if (!check) {
            std::ofstream(path, std::ios::binary) << text;
        } else if (read_file(path) != text) {
            std::cerr << path.string()
                      << " is out of date with src/config/keys.hpp; "
                         "regenerate it with `cmake --build build --target "
                         "config-reference`\n";
            status = 1;
        }
    }
    return status;
}
