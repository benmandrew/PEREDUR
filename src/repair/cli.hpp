#pragma once

#include <optional>
#include <string>

#include "manifest.hpp"

// PEREDUR's command-line surface: the help text and the input-format decision.

void print_help(const char* prog);

// The format to read @p input_path as: @p format_arg when given, otherwise
// the file extension. Nullopt when --format names no format.
//
// Testing only for "tlsf" and letting everything else mean FRETISH would turn a
// typo or a capitalisation ("--format TLSF") into a JSON parse error on a .tlsf
// file, which reads as a broken input rather than as a bad flag.
std::optional<RepairInput> resolve_input_format(
    const std::optional<std::string>& format_arg,
    const std::string& input_path);
