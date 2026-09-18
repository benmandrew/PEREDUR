#include "tlsf/mucs.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "driver_support.hpp"
#include "runner/atom_names.hpp"
#include "runner/spot.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <spec.tlsf>\n"
        << "\n"
        << "Extracts a minimal unrealizable core (MUC) from an unrealizable\n"
        << "basic-TLSF specification: the smallest subset of the guarantee-\n"
        << "side sections (PRESET, ASSERT, GUARANTEE) that stays unrealizable\n"
        << "against the full, unchanged environment side. Prints REALIZABLE\n"
        << "(no core) when the input is already realizable.\n"
        << "Exits 0 on success, 1 on error.\n"
        << "\n"
        << "  --version  Print the git commit this binary was built from.\n";
}

}  // namespace

int main(int argc, const char* const argv[]) {
    if (argc == 0 || argv == nullptr || argv[0] == nullptr) {
        std::cerr << "fatal: missing argv[0]\n";
        return 1;
    }
    if (handle_info_flags(argc, argv, print_usage)) {
        return 0;
    }
    if (argc != 2 || argv[1] == nullptr) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string path = argv[1];
    if (std::filesystem::path(path).extension() != ".tlsf") {
        std::cerr << path
                  << ": expected a .tlsf file (FRETISH JSON is not "
                     "supported by mucs)\n";
        return 1;
    }

    const std::optional<std::string> contents = read_file_contents(path);
    if (!contents.has_value()) {
        std::cerr << path << ": cannot read file\n";
        return 1;
    }

    tlsf::Specification spec;
    try {
        spec = tlsf::parse(*contents);
    } catch (const std::exception& exc) {
        std::cerr << path << ": " << exc.what() << "\n";
        return 1;
    }

    // Before the first query: ltlsynt reads an unmatched --ins as an output,
    // so an unsafe name here would answer about a different specification.
    if (const std::optional<std::string> unsafe =
            runner::first_unsafe_atom_name(spec.m_inputs, spec.m_outputs)) {
        std::cerr << path << ": " << *unsafe << "\n";
        return 1;
    }

    RealizabilityChecker& checker = global_real_checker();
    const std::optional<bool> realizable = checker.check_realizability_ltl(
        spec.to_ltl(), spec.m_inputs, spec.m_outputs);
    // Undecided is an error, not a licence to extract: extract_muc asserts an
    // unrealizable input, and every oracle call inside it would face the same
    // budget that just ran out.
    if (!realizable.has_value()) {
        std::cerr << path << ": ltlsynt timed out, realizability undecided\n";
        return 1;
    }
    if (*realizable) {
        std::cout << "REALIZABLE (no core)\n";
        return 0;
    }

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec);
    const std::size_t n_guarantee_side =
        spec.m_preset.size() + spec.m_assert.size() + spec.m_guarantee.size();
    std::cout << "core: " << muc.formulae.size() << " of " << n_guarantee_side
              << " guarantee-side formulae\n";
    for (const tlsf::CoreFormula& entry : muc.formulae) {
        std::cout << "[" << tlsf::section_name(entry.section_id) << "] "
                  << entry.formula.to_string() << "\n";
    }
    return 0;
}
