#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "driver_support.hpp"
#include "filter/implication_check.hpp"
#include "filter/vacuity.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

// Everything one front end contributes to the offline drivers (compare,
// lint-ideals and maximal), so each driver is written once over `Spec`. The
// pair of specialisations is the whole of the format split; adding a third
// format means adding one of these.
template <typename Spec>
struct SpecFormat;

template <>
struct SpecFormat<Specification> {
    static constexpr const char* k_extension = ".json";
    static constexpr const char* k_alphabet = "atom alphabets";

    static Specification parse(const std::string& text) {
        return parse_specification_json(text);
    }

    // load_specification words its own failures, each naming the path, so
    // @p unreadable is the TLSF side's alone.
    static Specification load(const std::string& path,
                              const char* /*unreadable*/) {
        return load_specification(path);
    }

    static bool same_alphabet(const Specification& lhs,
                              const Specification& rhs) {
        return lhs.m_in_atoms == rhs.m_in_atoms &&
               lhs.m_out_atoms == rhs.m_out_atoms && lhs.m_modes == rhs.m_modes;
    }

    static std::optional<bool> implies(const Specification& from,
                                       const Specification& dest,
                                       SatisfiabilityChecker& checker) {
        return spec_implies(from, dest, checker);
    }

    static std::optional<bool> realisable(const Specification& spec,
                                          RealizabilityChecker& real) {
        return real.check_realizability(spec);
    }

    static bool has_valid_guarantee(const Specification& spec,
                                    SatisfiabilityChecker& checker) {
        return specification_has_valid_guarantee(spec, checker);
    }
};

template <>
struct SpecFormat<tlsf::Specification> {
    static constexpr const char* k_extension = ".tlsf";
    static constexpr const char* k_alphabet = "signal alphabets";

    static tlsf::Specification parse(const std::string& text) {
        return tlsf::parse(text);
    }

    // Throws "<unreadable>: <path>" when @p path cannot be read.
    static tlsf::Specification load(const std::string& path,
                                    const char* unreadable) {
        return tlsf::parse(read_file_or_throw(path, unreadable));
    }

    static bool same_alphabet(const tlsf::Specification& lhs,
                              const tlsf::Specification& rhs) {
        return lhs.m_inputs == rhs.m_inputs && lhs.m_outputs == rhs.m_outputs;
    }

    static std::optional<bool> implies(const tlsf::Specification& from,
                                       const tlsf::Specification& dest,
                                       SatisfiabilityChecker& checker) {
        return tlsf_spec_implies(from, dest, checker);
    }

    static std::optional<bool> realisable(const tlsf::Specification& spec,
                                          RealizabilityChecker& real) {
        return real.check_realizability_ltl(spec.to_ltl(), spec.m_inputs,
                                            spec.m_outputs);
    }

    static bool has_valid_guarantee(const tlsf::Specification& spec,
                                    SatisfiabilityChecker& checker) {
        return tlsf_has_valid_guarantee(spec, checker);
    }
};

// Route by input format. A .tlsf extension on any path, or any .tlsf file in
// any directory path, selects TLSF; otherwise FRETISH JSON. Mixing the formats
// is not supported, and the FRETISH side then finds no .json files and says so.
inline bool selects_tlsf(const std::vector<std::string>& paths) {
    return std::any_of(paths.begin(), paths.end(), [](const std::string& path) {
        if (std::filesystem::path(path).extension() == ".tlsf") {
            return true;
        }
        std::error_code err_code;
        const std::filesystem::directory_iterator iter(path, err_code);
        return std::any_of(std::filesystem::begin(iter),
                           std::filesystem::end(iter), [](const auto& entry) {
                               return entry.path().extension() == ".tlsf";
                           });
    });
}
