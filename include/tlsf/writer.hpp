#pragma once

/// @file writer.hpp
/// @brief Pretty-printer that renders a tlsf::Specification back to valid
///        basic-format TLSF text.

#include <string>

#include "tlsf/specification.hpp"

namespace tlsf {

/// Serialises a Specification to basic-TLSF source text: an INFO block (TITLE,
/// DESCRIPTION, SEMANTICS, TARGET) and a MAIN block (INPUTS, OUTPUTS, then each
/// non-empty section with its formulae as `;`-terminated lines). The output
/// round-trips: `parse(write(spec))` reproduces `spec`.
std::string write(const Specification& specification);

/// Lists what @p repair changed against @p original, one line per formula:
/// `SECTION - formula` for a formula of the original the repair no longer has,
/// and `SECTION + formula` for one only the repair has. Each line is indented
/// two spaces and written in the same syntax as write(). Formulae are matched
/// by equality within a section, never by position, so a repair read back from
/// a file, which has lost its deleted slots, lists the same lines. Empty when
/// the two hold the same live formulae.
std::string write_changes(const Specification& original,
                          const Specification& repair);

}  // namespace tlsf
