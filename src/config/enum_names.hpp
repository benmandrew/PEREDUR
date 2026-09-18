#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "config.hpp"

// The one spelling of each enumerator, shared by the TOML parser (string to
// enum) and the run manifest (enum to string), so the two cannot drift and a
// manifest always round-trips into the config it describes. A specialisation
// holds `k_names`, an array of {enumerator, spelling} pairs in the order the
// parser's error message lists them.
template <typename Enum>
struct EnumNames;

template <>
struct EnumNames<SelectionScheme> {
    static constexpr std::array<std::pair<SelectionScheme, const char*>, 3>
        k_names{{{SelectionScheme::WeightedAverage, "weighted"},
                 {SelectionScheme::Nsga2Truncate, "nsga2-truncate"},
                 {SelectionScheme::Nsga2Apportion, "nsga2-apportion"}}};
};

template <>
struct EnumNames<SimilarityMetric> {
    static constexpr std::array<std::pair<SimilarityMetric, const char*>, 2>
        k_names{{{SimilarityMetric::Direct, "direct"},
                 {SimilarityMetric::Logarithmic, "logarithmic"}}};
};

template <>
struct EnumNames<RepairMode> {
    static constexpr std::array<std::pair<RepairMode, const char*>, 2> k_names{
        {{RepairMode::Monolithic, "monolithic"}, {RepairMode::Muc, "muc"}}};
};

template <>
struct EnumNames<StatusGrading> {
    static constexpr std::array<std::pair<StatusGrading, const char*>, 3>
        k_names{{{StatusGrading::Tiered, "tiered"},
                 {StatusGrading::Mrs, "mrs"},
                 {StatusGrading::Aurus, "aurus"}}};
};

template <>
struct EnumNames<MrsAdmissionOrder> {
    static constexpr std::array<std::pair<MrsAdmissionOrder, const char*>, 2>
        k_names{{{MrsAdmissionOrder::Spec, "spec"},
                 {MrsAdmissionOrder::Degree, "degree"}}};
};

template <>
struct EnumNames<TerminationMode> {
    static constexpr std::array<std::pair<TerminationMode, const char*>, 2>
        k_names{{{TerminationMode::Generations, "generations"},
                 {TerminationMode::Individuals, "individuals"}}};
};

template <typename Enum>
const char* enum_name(Enum value) {
    for (const auto& [candidate, name] : EnumNames<Enum>::k_names) {
        if (candidate == value) {
            return name;
        }
    }
    return "unknown";
}

template <typename Enum>
std::optional<Enum> enum_from_name(const std::string& name) {
    for (const auto& [value, candidate] : EnumNames<Enum>::k_names) {
        if (name == candidate) {
            return value;
        }
    }
    return std::nullopt;
}

// The spellings as the parser's errors list them: `"a" or "b"`, or
// `"a", "b" or "c"`.
template <typename Enum>
std::string enum_alternatives() {
    const auto& names = EnumNames<Enum>::k_names;
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            text += i + 1 == names.size() ? " or " : ", ";
        }
        text += std::string("\"") + names[i].second + "\"";
    }
    return text;
}
