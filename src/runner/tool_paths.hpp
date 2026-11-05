#pragma once

/// @file tool_paths.hpp
/// @brief Resolves the paths of the external tools and data files a run needs,
///        from the environment where it supplies one and from the compiled-in
///        default otherwise.
///
/// Private to `src/`: the published resolvers are the per-tool ones in
/// `include/runner/`, which this backs.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// A resolved tool path together with where it came from. The formaliser names
/// the variable in its missing-file report, and the run manifest records both
/// halves, so an archived run says which binary it ran and why.
struct ToolPath {
    std::string m_path;
    bool m_from_env = false;
};

/// Returns @p env_var's value when it is set and non-empty, and
/// @p compiled_default otherwise.
///
/// Callers must hold the result in a function-local `static`, so that the
/// environment is read once per path for the life of the process. getenv() is
/// not thread-safe against a concurrent setenv() and every resolver here is
/// called from the scoring pool's threads at once; reading once also stops a
/// run from changing tool half-way through it.
ToolPath tool_path_from_env(const char* env_var, const char* compiled_default);

/// The Spot bin directory, ganak and black as this process resolved them, each
/// the same function-local `static` its public resolver in `include/runner/`
/// returns the path of.
const ToolPath& spot_bin_dir_source();
const ToolPath& ganak_path_source();
const ToolPath& black_path_source();

/// What one version probe of a tool found: the version line, or why there is
/// none.
struct ToolVersion {
    /// The first output line holding the probe's marker, trimmed to start at
    /// the marker. Empty when the tool failed to run or printed no such line.
    std::optional<std::string> m_version;
    /// Why `m_version` is empty, and empty itself otherwise.
    std::string m_error;
};

/// Runs @p command once, with a short timeout, and returns the first line of
/// its output that holds @p marker, from the marker on. Never throws a
/// std::exception: a tool that is missing, fails, times out or prints no such
/// line yields an empty version and the reason, since a run must not end
/// because its provenance could not be read.
ToolVersion probe_tool_version(const std::vector<std::string>& command,
                               std::string_view marker);

/// The version of each tool above, probed once per process on the first call
/// and cached in a function-local `static`. Only the run manifest asks, after
/// the search has finished, so a run that writes none never pays for it.
const ToolVersion& spot_version();
const ToolVersion& ganak_version();
const ToolVersion& black_version();
