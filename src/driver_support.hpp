#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Argument handling shared by the CLI drivers (peredur, compare, realize, ltl
// and mucs). Each driver still owns its own usage text and its own flags; only
// the pieces every one of them needs identically live here.

bool has_flag(int argc, const char* const* argv, const char* flag);

std::optional<std::string> parse_string_arg(int argc, const char* const* argv,
                                            const char* flag);

// Answers --version and -h/--help, reporting usage through @p print_usage so
// each driver keeps its own text and output stream. True when one of them
// fired and the caller should exit successfully.
//
// Both flags report on the binary rather than on a run, so they are answered
// before anything else: interrogating a binary must not require valid
// arguments, and --version in particular is what a harness calls to find out
// what it is about to run.
bool handle_info_flags(int argc, const char* const* argv,
                       void (*print_usage)(const char*));

std::vector<std::string> collect_argument_paths(int argc,
                                                const char* const* argv);

// Nullopt when the file cannot be opened. Each driver words that failure its
// own way -- one throws, the others print and return -- so it is reported here
// as absence rather than decided.
std::optional<std::string> read_file_contents(const std::string& path);

// Nullopt when @p text is not a complete run of decimal digits naming a value
// that fits. Only a whole-string match is accepted, so "12abc" and "-1" are
// rejected rather than silently read as 12 and as a wrapped 2^64-1: a seed is
// what makes a run reproducible, and a typo that still starts a run pins the
// result to a value nobody chose.
std::optional<std::size_t> parse_seed(const std::string& text);

// The first argument that is neither a flag the driver knows nor the value
// belonging to one, or nullopt when every argument is accounted for.
// @p value_flags take the following argument as their value; @p bare_flags
// stand alone.
//
// Callers used to look up only the flags they recognised and ignore the rest,
// so a plausible-looking flag the binary does not have ran a whole search
// against the defaults without a word. The ones that catch people name real
// config keys, generations and population size among them, which is exactly
// why they look like they should work. Silently running something other than
// what was asked for is worse than refusing to start.
std::optional<std::string> find_unknown_arg(
    int argc, const char* const* argv,
    const std::vector<std::string>& value_flags,
    const std::vector<std::string>& bare_flags);
