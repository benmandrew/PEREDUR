#pragma once

/// @file tool_paths.hpp
/// @brief Resolves the paths of the external tools and data files a run needs,
///        from the environment where it supplies one and from the compiled-in
///        default otherwise.
///
/// Private to `src/`: the published resolvers are the per-tool ones in
/// `include/runner/`, which this backs.

#include <string>

/// A resolved tool path together with where it came from, which only the
/// formaliser needs: it is the one resolver that reports a missing file, and
/// the report is far more use when it names the variable that pointed at it.
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
