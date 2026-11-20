#include "cli.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

void print_help(const char* prog) {
    std::cout
        << "Usage: " << prog
        << " --input <spec> --output-dir <dir> [--seed <n>]\n"
        << "\n"
        << "Repair an unrealisable reactive specification using a genetic\n"
        << "algorithm. The input is FRETISH requirements as JSON, a TLSF\n"
        << "specification or a PSP specification; the algorithm evolves a\n"
        << "population of\n"
        << "candidate repairs, scoring each by syntactic similarity, semantic\n"
        << "similarity, and LTL realisability.\n"
        << "\n"
        << "Options:\n"
        << "  --input <spec>       Path to the input specification "
           "(required).\n"
        << "                       FRETISH JSON, accepting either a plain\n"
        << "                       Specification or a ScoredSpecification "
           "with\n"
        << "                       an optional \"fitness\" field; a .tlsf\n"
        << "                       file; or a .psp file.\n"
        << "  --output-dir <dir>   Directory to write maximal realizable\n"
        << "                       repairs to as repair_0.json, "
           "repair_1.json,\n"
        << "                       ... (repair_0.tlsf or repair_0.psp ... for\n"
        << "                       TLSF or PSP input;\n"
        << "                       required, and must already exist).\n"
        << "  --config <file>      Path to a TOML configuration file.\n"
        << "                       Absent keys use built-in defaults.\n"
        << "  --format <fmt>       Input format: fretish, tlsf or psp. If\n"
        << "                       omitted, inferred from the --input\n"
        << "                       extension (.tlsf as TLSF, .psp as PSP,\n"
        << "                       any other extension as FRETISH).\n"
        << "  --seed <n>           RNG seed for reproducible runs. If omitted\n"
        << "                       a random seed is chosen and printed.\n"
        << "  --diagnostics        Print the filter report and the\n"
        << "                       engine-internal counters at exit: "
           "per-tool\n"
        << "                       calls and cache totals, the "
           "constant-folded\n"
        << "                       count and the fitness cache hit rate. "
           "The\n"
        << "                       counters are written to\n"
        << "                       <output-dir>/run.json on every run "
           "whether\n"
        << "                       or not this is given.\n"
        << "  --repair-summary     List what each repair changed, after the\n"
        << "                       count: one block per repair file, with "
           "a\n"
        << "                       '-' line for each formula or "
           "requirement\n"
        << "                       of the input the repair dropped and a "
           "'+'\n"
        << "                       line for each one it added.\n"
        << "  --cpu-report         Print a CPU-attribution report at exit:\n"
        << "                       this process's own code against the\n"
        << "                       external CLI tools, and per tool.\n"
        << "  --version            Print the git commit this binary was "
           "built\n"
        << "                       from as commit=, commit_short= and dirty=\n"
        << "                       lines, and exit.\n"
        << "  -h, --help           Show this help message and exit.\n"
        << "\n"
        << "Input format (examples/takeoff/spec.json):\n"
        << "  {\n"
        << "    \"assumptions\": [],\n"
        << "    \"guarantees\":  [ { \"trigger\": \"<formula>\",\n"
        << "                        \"response\": \"<formula>\",\n"
        << "                        \"timing\":   { \"type\": \"Immediately\" "
           "} } ],\n"
        << "    \"in_atoms\":  [\"a\", \"b\"],\n"
        << "    \"out_atoms\": [\"x\", \"y\"]\n"
        << "  }\n"
        << "\n"
        << "Timing types: Immediately, NextTimepoint, Eventually, Always,\n"
        << "              WithinTicks {\"ticks\": n}, ForTicks {\"ticks\": "
           "n},\n"
        << "              AfterTicks  {\"ticks\": n},\n"
        << "              Until {\"stop\": \"<formula>\"}, Before {\"stop\": "
           "\"<formula>\"},\n"
        << "              Never (loaded as Always over the negated response)\n";
}

std::optional<RepairInput> resolve_input_format(
    const std::optional<std::string>& format_arg,
    const std::string& input_path) {
    if (!format_arg.has_value()) {
        const std::filesystem::path extension =
            std::filesystem::path(input_path).extension();
        if (extension == ".tlsf") {
            return RepairInput::Tlsf;
        }
        if (extension == ".psp") {
            return RepairInput::Psp;
        }
        return RepairInput::Fretish;
    }
    if (*format_arg == "tlsf") {
        return RepairInput::Tlsf;
    }
    if (*format_arg == "psp") {
        return RepairInput::Psp;
    }
    if (*format_arg == "fretish") {
        return RepairInput::Fretish;
    }
    return std::nullopt;
}
