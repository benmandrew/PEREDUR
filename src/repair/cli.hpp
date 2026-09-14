#pragma once

#include <optional>
#include <string>

// PEREDUR's command-line surface: the help text and the input-format decision.

void print_help(const char* prog);

// Whether to read @p input_path as TLSF: @p format_arg when given, otherwise
// the file extension. Nullopt when --format names neither format.
//
// Testing only for "tlsf" and letting everything else mean FRETISH would turn a
// typo or a capitalisation ("--format TLSF") into a JSON parse error on a .tlsf
// file, which reads as a broken input rather than as a bad flag.
std::optional<bool> resolve_is_tlsf(
    const std::optional<std::string>& format_arg,
    const std::string& input_path);
