#pragma once

#include <filesystem>

// Writes schemas/config-schema.json and example-config.toml under `root` from
// k_config_keys, or with `check` compares them instead and reports each that
// differs. Returns the process exit status.
int config_reference(const std::filesystem::path& root, bool check);
