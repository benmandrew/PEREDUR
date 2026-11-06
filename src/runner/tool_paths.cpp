#include "tool_paths.hpp"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "runner/process.hpp"

namespace {

// Each probe answers in about 10 ms. The budget only has to stop a wedged
// binary from holding up the manifest at the end of a run.
constexpr std::chrono::milliseconds k_version_timeout{5000};

}  // namespace

ToolPath tool_path_from_env(const char* env_var, const char* compiled_default) {
    const char* value = std::getenv(env_var);
    // An empty override reads as "not set" rather than as an empty path: the
    // shells and container runtimes that carry these variables around export
    // an unset one as empty, so treating it as an answer would break exactly
    // the deployments the override exists for.
    if (value != nullptr && *value != '\0') {
        return ToolPath{std::string(value), true};
    }
    return ToolPath{std::string(compiled_default), false};
}

ToolVersion probe_tool_version(const std::vector<std::string>& command,
                               std::string_view marker) {
    ProcessResult result;
    try {
        result = execute_and_capture(command, k_version_timeout);
    } catch (const std::exception& error) {
        return {std::nullopt, std::string("could not run: ") + error.what()};
    }
    if (result.m_timed_out) {
        return {std::nullopt, "timed out after " +
                                  std::to_string(k_version_timeout.count()) +
                                  " ms"};
    }
    if (result.m_exit_code != 0) {
        return {std::nullopt,
                "exit status " + std::to_string(result.m_exit_code)};
    }
    const std::string& output = result.m_output;
    const std::size_t start = output.find(marker);
    if (start == std::string::npos) {
        return {std::nullopt, "no line holding \"" + std::string(marker) +
                                  "\" in its output"};
    }
    std::size_t end = output.find('\n', start);
    if (end == std::string::npos) {
        end = output.size();
    }
    while (end > start && (output[end - 1] == '\r' || output[end - 1] == ' ' ||
                           output[end - 1] == '\t')) {
        --end;
    }
    return {output.substr(start, end - start), ""};
}
