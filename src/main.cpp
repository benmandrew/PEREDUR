#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "config.hpp"
#include "config_io.hpp"
#include "crash/crash_handler.hpp"
#include "driver_support.hpp"
#include "repair/cli.hpp"
#include "repair/driver.hpp"
#include "thread_pool.hpp"

int main(int argc, const char* const argv[]) {
    if (!has_program_name(argc, argv)) {
        return 1;
    }
    if (handle_info_flags(argc, argv, print_help)) {
        return 0;
    }
    init_cpptrace(argv[0]);
    // Before any of the lookups below, so an argument this binary does not have
    // stops the run rather than being read past.
    const std::optional<std::string> unknown = find_unknown_arg(
        argc, argv,
        {"--input", "--output-dir", "--config", "--format", "--seed"},
        {"--dashboard", "--cpu-report", "--diagnostics", "--version", "-h",
         "--help"});
    if (unknown.has_value()) {
        std::cerr << "Unknown argument: '" << *unknown << "'\n"
                  << "Try '" << argv[0] << " --help' for the accepted flags. "
                  << "Search parameters such as generations and population size"
                  << " are config keys, set with --config <file>.\n";
        return 1;
    }
    Config cfg;
    const std::optional<std::string> config_path =
        parse_string_arg(argc, argv, "--config");
    if (config_path.has_value()) {
        try {
            cfg = config_from_toml(*config_path);
        } catch (const std::exception& exc) {
            std::cerr << exc.what() << "\n";
            return 1;
        }
    }
    // Folded into cfg here so everything downstream, both drivers included,
    // reads one switch. The flag can only enable: a config that already asked
    // for the dashboard is not turned off by its absence.
    cfg.dashboard = cfg.dashboard || has_flag(argc, argv, "--dashboard");
    cfg.report_cpu_timing = has_flag(argc, argv, "--cpu-report");
    cfg.report_diagnostics = has_flag(argc, argv, "--diagnostics");
    apply_tool_timeouts(cfg);
    set_thread_pool_size(cfg.parallel);

    const std::optional<std::string> input_path =
        parse_string_arg(argc, argv, "--input");
    const std::optional<std::string> output_dir =
        parse_string_arg(argc, argv, "--output-dir");
    if (!input_path.has_value() || !output_dir.has_value()) {
        std::cerr << "Usage: " << argv[0]
                  << " --input <spec.json> --output-dir <dir> [--seed <n>]\n"
                  << "Try '" << argv[0] << " --help' for more information.\n";
        return 1;
    }
    if (!std::filesystem::is_directory(*output_dir)) {
        std::cerr << "Output directory does not exist: " << *output_dir << "\n";
        return 1;
    }
    const std::optional<std::string> format_arg =
        parse_string_arg(argc, argv, "--format");
    const std::optional<bool> is_tlsf =
        resolve_is_tlsf(format_arg, *input_path);
    if (!is_tlsf.has_value()) {
        // resolve_is_tlsf only declines when --format was given, so the
        // fallback here is unreachable; it is what lets the access be checked.
        std::cerr << "Unknown --format value: '" << format_arg.value_or("")
                  << "' (expected 'fretish' or 'tlsf')\n";
        return 1;
    }
    const std::optional<std::string> seed_arg =
        parse_string_arg(argc, argv, "--seed");
    std::optional<std::size_t> seed;
    if (seed_arg.has_value()) {
        seed = parse_seed(*seed_arg);
        if (!seed.has_value()) {
            std::cerr << "Invalid --seed value: '" << *seed_arg
                      << "' (expected a non-negative integer)\n";
            return 1;
        }
    }
    return *is_tlsf ? run_tlsf_repair(cfg, *input_path, *output_dir, seed)
                    : run_fretish_repair(cfg, *input_path, *output_dir, seed);
}
