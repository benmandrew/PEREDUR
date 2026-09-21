#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fitness/exhaustive_count.hpp"
#include "runner/ganak.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "ganak_runner";

}  // namespace

TEST(test_ganak_runner_on_trivial_cnf) {
    const TempDir dir("ganak_runner");
    const std::filesystem::path dimacs_path =
        write_text(dir.path() / "trivial.cnf", "p cnf 1 1\n1 0\n");

    const Count count = run_ganak_on_dimacs(dimacs_path.string(), 1);
    expect(count == 1,
           "ganak-runner: expected count 1 for single-literal SAT CNF");
}

// Two guards differing only in their atom names are one count, so they must
// share a cache entry rather than buying an exec each. The renaming was where
// the whole of that collapse came from while `ltlfilt --simplify` sat in front
// of this cache normalising operand order; the canonical form carries that
// half now.
TEST(test_ganak_cache_is_rename_invariant) {
    const std::size_t misses_before = GanakStats::n_cache_misses;
    const Count first = run_ganak_on_formula("(a) & (b)");
    const Count second = run_ganak_on_formula("(y) & (z)");
    expect(first == second, "ganak-runner: a renaming changed the model count");
    expect(GanakStats::n_cache_misses == misses_before + 1,
           "ganak-runner: a renamed guard bought a second exec");
}

// The count must be over every variable the caller's formula mentions, since
// count_guard_models multiplies it by two per variable of the wider alphabet
// and computes that exponent from the HOA label rather than from whatever
// reaches ganak. `ltlfilt --simplify` returns `a` for this subject, dropping
// `b` from the DIMACS and halving the count; the canonical form keeps both.
TEST(test_ganak_counts_over_every_mentioned_variable) {
    expect(run_ganak_on_formula("(a) | ((a) & (b))") == 2,
           "ganak-runner: a subsumed term cost the count a variable");
}

// The in-process enumeration is what answers a guard now, so it is checked
// against the subprocess it stands in for rather than against a second
// implementation of its own argument. The subjects are guard-shaped --
// negation, conjunction and disjunction over a handful of atoms -- because
// that is the whole of what hoa_label_to_formula can emit.
TEST(test_exhaustive_count_agrees_with_ganak) {
    const std::vector<std::string> formulae = {
        "(a)",
        "(!a)",
        "(a) & (b)",
        "(a) | (!b)",
        "((a) & (!b)) | ((!a) & (c))",
        "(a) & (b) & (c) & (!d) & (e)",
        "((a) | (b)) & ((c) | (!d)) & ((e) | (f)) & ((!g) | (a))",
    };
    for (const std::string& formula : formulae) {
        const std::optional<Count> exact = count_models_exhaustively(formula);
        expect(exact.has_value(),
               "exhaustive-count: declined a guard-shaped formula");
        expect(exact.value_or(-1) == run_ganak_on_formula(formula),
               "exhaustive-count: disagreed with ganak on " + formula);
    }
}
