#include <filesystem>
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
    const TempDir dir("formula_dimacs");
    const std::filesystem::path dimacs_path =
        write_text(dir.path() / "implies.cnf", formula.to_dimacs());
    const Count count = run_ganak_on_dimacs(dimacs_path.string(), 1);
    expect(count == 3,
           "formula-dimacs: expected 3 models for formula 'P -> Q'");
}

TEST(test_formula_to_dimacs_precedence_count) {
    const Formula formula = Formula("A | B & C");
    const TempDir dir("formula_dimacs");
    const std::filesystem::path dimacs_path =
        write_text(dir.path() / "precedence.cnf", formula.to_dimacs());
    const Count count = run_ganak_on_dimacs(dimacs_path.string(), 1);
    expect(count == 5,
           "formula-dimacs: expected 5 models for formula 'A | B & C'");
}

}  // namespace
