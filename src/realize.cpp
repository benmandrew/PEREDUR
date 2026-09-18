#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "driver_support.hpp"
#include "requirement.hpp"
#include "runner/atom_names.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "tlsf/specification.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <spec.json|spec.tlsf> [...]\n"
        << "\n"
        << "Checks whether specification(s) are realisable. FRETISH JSON\n"
        << "and basic-TLSF (.tlsf) inputs are both accepted; the format\n"
        << "is chosen from the file extension.\n"
        << "Single file: prints REALIZABLE or UNREALIZABLE.\n"
        << "Multiple files: prints \"<path>: REALIZABLE\" or \"<path>: "
           "UNREALIZABLE\" per line.\n"
        << "Exits with status 0 on success, or status 1 on error.\n"
        << "\n"
        << "  --version  Print the git commit this binary was built from.\n";
}

std::optional<bool> check_tlsf_realizable(const std::string& path,
                                          RealizabilityChecker& checker) {
    const std::optional<tlsf::Specification> spec = load_tlsf_or_report(path);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    try {
        // ltlsynt reads an unmatched --ins as an output, so an unsafe name
        // turns this tool's verdict into one about a different specification.
        if (const std::optional<std::string> unsafe =
                runner::first_unsafe_atom_name(spec->m_inputs,
                                               spec->m_outputs)) {
            std::cerr << path << ": " << *unsafe << "\n";
            return std::nullopt;
        }
        const std::optional<bool> realizable = checker.check_realizability_ltl(
            spec->to_ltl(), spec->m_inputs, spec->m_outputs);
        if (!realizable.has_value()) {
            std::cerr << path
                      << ": ltlsynt timed out, realizability undecided\n";
        }
        return realizable;
    } catch (const std::exception& exc) {
        std::cerr << path << ": " << exc.what() << "\n";
        return std::nullopt;
    }
}

// Resolves a single path's realizability, dispatching on the .tlsf extension.
// Returns nullopt (after printing the error) if the file cannot be loaded, or
// if ltlsynt hit its timeout: this tool reports what was decided, so undecided
// is an error rather than a verdict in either direction.
std::optional<bool> realize_one(const std::string& path,
                                RealizabilityChecker& checker, bool multi) {
    if (std::filesystem::path(path).extension() == ".tlsf") {
        return check_tlsf_realizable(path, checker);
    }
    Specification spec;
    try {
        spec = load_specification(path);
    } catch (const std::exception& exc) {
        std::cerr << (multi ? path + ": " : "") << exc.what() << "\n";
        return std::nullopt;
    }
    if (const std::optional<std::string> unsafe =
            runner::first_unsafe_atom_name(environment_signals(spec),
                                           spec.m_out_atoms)) {
        std::cerr << (multi ? path + ": " : "") << *unsafe << "\n";
        return std::nullopt;
    }
    const std::optional<bool> realizable = checker.check_realizability(spec);
    if (!realizable.has_value()) {
        std::cerr << (multi ? path + ": " : "")
                  << "ltlsynt timed out, realizability undecided\n";
    }
    return realizable;
}

}  // namespace

int main(int argc, const char* const argv[]) {
    if (!has_program_name(argc, argv)) {
        return 1;
    }
    if (handle_info_flags(argc, argv, print_usage)) {
        return 0;
    }

    const std::vector<std::string> paths = collect_argument_paths(argc, argv);
    if (paths.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    const bool multi = paths.size() > 1;
    RealizabilityChecker& checker = global_real_checker();

    for (const std::string& path : paths) {
        const std::optional<bool> realizable =
            realize_one(path, checker, multi);
        if (!realizable.has_value()) {
            return 1;
        }
        const char* result = *realizable ? "REALIZABLE" : "UNREALIZABLE";
        if (multi) {
            std::cout << path << ": " << result << "\n";
        } else {
            std::cout << result << "\n";
        }
    }
    return 0;
}
