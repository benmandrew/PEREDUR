#pragma once

/// @file psp/specification.hpp
/// @brief The PSP front end's specification, and its `.psp` JSON format.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "hash_combine.hpp"
#include "psp/requirement.hpp"

namespace psp {

/// A PSP specification, with the same top-level shape as the FRETISH one:
/// assumptions, guarantees and the two atom lists (PLAN.md D6).
struct Specification {
    std::vector<Requirement> m_assumptions;
    std::vector<Requirement> m_guarantees;
    std::vector<std::string> m_in_atoms;
    std::vector<std::string> m_out_atoms;

    /// Drops a requirement repeated within one list, keeping the first, as
    /// the FRETISH constructor does.
    explicit Specification(std::vector<Requirement> assumptions = {},
                           std::vector<Requirement> guarantees = {},
                           std::vector<std::string> in_atoms = {},
                           std::vector<std::string> out_atoms = {});

    friend bool operator<(const Specification& lhs, const Specification& rhs);
    friend bool operator==(const Specification& lhs, const Specification& rhs);

    /// One structured-English line per live requirement, assumptions first,
    /// each as Requirement::to_string gives it.
    [[nodiscard]] std::string to_string() const;

    /// The whole specification as one LTL formula in SPOT syntax,
    /// `(a1 & ... & an) -> (g1 & ... & gm)` over the live requirements, or the
    /// guarantee conjunction alone where no assumption is live. An empty
    /// conjunction is `true`, as on the FRETISH path.
    [[nodiscard]] std::string to_ltl() const;
};

/// Applies add_atom_prefix to every requirement and both atom lists.
Specification add_atom_prefix(const Specification& spec);

/// The inverse of the Specification add_atom_prefix.
Specification strip_atom_prefix(const Specification& spec);

/// Largest count a bounded pattern may carry in a `.psp` file. The lowering
/// expands a count into an `X` chain, quadratically for bounded invariance
/// under a boundary, so a count above this is refused at load, as
/// `tlsf::k_max_bound_expansion` refuses a long TLSF bound.
inline constexpr std::size_t k_max_count = 64;

/// The first violation of the `.psp` format
/// (`schemas/psp-specification.schema.json`) in @p text, or the empty string
/// for a valid document. The atom names are screened as `realize` screens
/// them (`runner::first_unsafe_atom_name`).
std::string first_format_error(const std::string& text);

/// Parses @p text in the `.psp` format and prefixes every atom with
/// `k_atom_prefix`, as the FRETISH loader does. Throws std::invalid_argument
/// naming the first violation, an unsafe atom name included.
Specification parse_specification(const std::string& text);

/// Reads and parses the `.psp` file at @p path. Throws std::runtime_error
/// naming the path when it cannot be read or is not valid.
Specification load_specification(const std::string& path);

/// Writes @p spec in the `.psp` format, with the atom prefix stripped and
/// removed requirements left out, as the FRETISH writer does. The result
/// parses back to the same specification.
std::string write_specification(const Specification& spec);

}  // namespace psp

/// \cond
namespace std {  // NOLINT(build/namespaces)

template <>
struct hash<psp::Specification> {
    std::size_t operator()(const psp::Specification& spec) const noexcept {
        std::size_t seed = 0;
        for (const psp::Requirement& req : spec.m_assumptions) {
            seed = hash_combine(seed, std::hash<psp::Requirement>{}(req));
        }
        // A separator, so moving a requirement across the two lists changes
        // the hash.
        seed = hash_combine(seed, spec.m_assumptions.size());
        for (const psp::Requirement& req : spec.m_guarantees) {
            seed = hash_combine(seed, std::hash<psp::Requirement>{}(req));
        }
        for (const std::string& atom : spec.m_in_atoms) {
            seed = hash_combine(seed, std::hash<std::string>{}(atom));
        }
        for (const std::string& atom : spec.m_out_atoms) {
            seed = hash_combine(seed, std::hash<std::string>{}(atom));
        }
        return seed;
    }
};

}  // namespace std
/// \endcond
