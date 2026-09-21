#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "driver_support.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "serialisation.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <spec.json|spec.tlsf> [...]\n"
              << "\n"
              << "Prints the LTL formulae for each requirement in a "
                 "specification.\n"
              << "FRETISH JSON prints each requirement's LTL; a basic-TLSF\n"
              << "(.tlsf) input prints each non-empty section's formulae and\n"
              << "the combined lowering.\n"
              << "\n"
              << "  --version  Print the git commit this binary was built "
                 "from.\n";
}

void print_tlsf_ltl(const std::string& path, bool show_path) {
    const tlsf::Specification spec =
        tlsf::parse(read_file_or_throw(path, "cannot read file", false));
    if (show_path) {
        std::cout << path << ":\n";
    }
    // Deleted conjuncts are not printed, so what this shows is what `to_ltl`
    // below lowers.
    const auto print_section = [](const char* name,
                                  const tlsf::Section& section) {
        const std::vector<Formula> formulae = tlsf::live_formulae(section);
        if (formulae.empty()) {
            return;
        }
        std::cout << "  " << name << ":\n";
        for (const Formula& formula : formulae) {
            std::cout << "    " << formula.to_string() << "\n";
        }
    };
    print_section("INITIALLY", spec.m_initially);
    print_section("PRESET", spec.m_preset);
    print_section("REQUIRE", spec.m_require);
    print_section("ASSUME", spec.m_assume);
    print_section("ASSERT", spec.m_assert);
    print_section("GUARANTEE", spec.m_guarantee);
    std::cout << "  combined LTL: " << spec.to_ltl() << "\n";
    if (show_path) {
        std::cout << "\n";
    }
}

void print_spec_ltl(const std::string& path, const Specification& prefixed_spec,
                    bool show_path) {
    // Internal atom names carry k_atom_prefix; strip once so both the FRETish
    // line (to_string) and the LTL line (m_ltl) show the original names.
    const Specification spec = strip_atom_prefix(prefixed_spec);
    if (show_path) {
        std::cout << path << ":\n";
    }
    for (const Requirement& req : spec.m_assumptions) {
        std::cout << "  [assumption] " << req.to_string() << "\n";
        std::cout << "    LTL: " << req.m_ltl << "\n";
    }
    for (const Requirement& req : spec.m_guarantees) {
        std::cout << "  [guarantee] " << req.to_string() << "\n";
        std::cout << "    LTL: " << req.m_ltl << "\n";
    }
    if (show_path) {
        std::cout << "\n";
    }
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

    for (const std::string& path : paths) {
        try {
            if (std::filesystem::path(path).extension() == ".tlsf") {
                print_tlsf_ltl(path, multi);
            } else {
                print_spec_ltl(path, load_specification(path), multi);
            }
        } catch (const std::exception& exc) {
            std::cerr << path << ": " << exc.what() << "\n";
            return 1;
        }
    }
    return 0;
}
