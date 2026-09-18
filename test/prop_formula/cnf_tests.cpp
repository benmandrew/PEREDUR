#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include "prop_formula.hpp"
#include "runner/ganak.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "prop_formula_cnf";

TEST(test_formula_to_dimacs_implies_count) {
    const Formula formula = Formula("P -> Q");
    std::string dimacs_path = "/tmp/peredur-formula-implies-XXXXXX";
    const int file_descriptor = mkstemp(dimacs_path.data());
    expect(file_descriptor >= 0,
           "formula-dimacs: failed to create DIMACS file for implies");
    close(file_descriptor);
    {
        std::ofstream dimacs_file(dimacs_path);
        expect(dimacs_file.good(),
               "formula-dimacs: failed to create DIMACS file for implies");
        dimacs_file << formula.to_dimacs();
    }
    const Count count = run_ganak_on_dimacs(dimacs_path, 1);
    expect(count == 3,
           "formula-dimacs: expected 3 models for formula 'P -> Q'");
    std::remove(dimacs_path.c_str());
}

TEST(test_formula_to_dimacs_precedence_count) {
    const Formula formula = Formula("A | B & C");
    std::string dimacs_path = "/tmp/peredur-formula-precedence-XXXXXX";
    const int file_descriptor = mkstemp(dimacs_path.data());
    expect(file_descriptor >= 0,
           "formula-dimacs: failed to create DIMACS file for precedence");
    close(file_descriptor);
    {
        std::ofstream dimacs_file(dimacs_path);
        expect(dimacs_file.good(),
               "formula-dimacs: failed to create DIMACS file for precedence");
        dimacs_file << formula.to_dimacs();
    }
    const Count count = run_ganak_on_dimacs(dimacs_path, 1);
    expect(count == 5,
           "formula-dimacs: expected 5 models for formula 'A | B & C'");
    std::remove(dimacs_path.c_str());
}

}  // namespace
