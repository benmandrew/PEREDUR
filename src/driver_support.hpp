#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "runner/black.hpp"
#include "tlsf/specification.hpp"

// Argument handling shared by the CLI drivers (peredur, compare, realize, ltl
// and mucs). Each driver still owns its own usage text and its own flags; only
// the pieces every one of them needs identically live here.

// False, after saying so on stderr, when the driver was started without an
// argv[0]: every usage message names the program by it.
bool has_program_name(int argc, const char* const* argv);

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

// The contents of @p path, for the drivers that report an unreadable file by
// throwing into a handler they already have. The std::runtime_error carries
// @p failure, followed by ": " and the path when @p name_path is set.
std::string read_file_or_throw(const std::string& path, const char* failure,
                               bool name_path = true);

// Reads and parses the basic-TLSF file at @p path, or prints
// "<path>: <reason>" to stderr and returns nullopt when it cannot be read or
// does not parse.
std::optional<tlsf::Specification> load_tlsf_or_report(const std::string& path);

// Applies the solver budgets of the offline drivers (compare, maximal and
// lint-ideals) and returns the satisfiability checker they share. Each of them
// queries specifications a search or a person has already stretched, and runs
// rarely, so every budget is generous next to a run's.
//
// @p whole_spec_queries drops the ltlfilt --simplify pass and gives SPOT
// @p black_timeout, for drivers that ask whole-specification implications.
SatisfiabilityChecker& configure_offline_checkers(
    std::chrono::milliseconds black_timeout, bool whole_spec_queries);

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
