// Lints hand-written ideal repairs against the invariants an ideal has to
// satisfy to be a meaningful target for `implies_ideal`.
//
// The metric counts a run as a success when it produces a repair equivalent to
// or stronger than some ideal. That makes an ideal useless in three distinct
// ways, none of which shows up as an error anywhere else:
//
//   - It is not a weakening of its own spec. The framework emits only
//     weakenings, so `orig -> R` holds for every repair R. If `R -> I` held
//     then `orig -> I` would follow, so an ideal the original does not imply
//     can never be implied by any repair. implies_ideal is 0 by construction.
//   - It is outside the image of the genetic operators. evolve.cpp seeds the
//     whole population with copies of the original, and src/tlsf/mutation.cpp
//     moves guarantee-section conjunct counts one way only: tlsf_remove_
//     guarantee deletes one (down to a floor of one), nothing adds. INITIALLY
//     and REQUIRE are never appended to; only ASSUME grows, via
//     tlsf_add_assumption. An ideal that adds a guarantee or a REQUIRE is
//     unreachable no matter how long the search runs.
//   - It is degenerate: unrealisable, ill-separated, or carrying a
//     trivially-true guarantee. The first can never be produced; the other two
//     are screened out of the search's own output, so an ideal failing them
//     asks the search for something it is built to discard.
//
// A fourth outcome is redundancy rather than uselessness. Where one ideal is
// strictly stronger than a sibling, the weaker one always governs -- implying
// the stronger implies the weaker too -- so the stronger cannot change the
// score. That is reported separately, since it costs nothing but measures
// nothing either.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "driver_support.hpp"
#include "filter/implication_check.hpp"
#include "filter/vacuity.hpp"
#include "filter/well_separation.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"
#include "version.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <subject-dir> [<subject-dir> ...]\n"
        << "\n"
        << "Lints the ideal repairs of one or more subjects. A subject\n"
        << "directory holds a spec.tlsf or spec.json alongside a fixes/\n"
        << "directory of ideals, which is the layout under examples/.\n"
        << "\n"
        << "Each ideal is checked for five properties:\n"
        << "  weakening   the spec implies the ideal, so a repair can reach "
           "it\n"
        << "  realisable  ltlsynt can synthesise it\n"
        << "  separated   the system cannot force its own assumptions to "
           "fail\n"
        << "  nontrivial  no guarantee-side conjunct is valid\n"
        << "  reachable   it lies in the image of the genetic operators\n"
        << "\n"
        << "Ideals a sibling makes redundant are reported after the table.\n"
        << "Exits 0 when every ideal passes, 1 when any check fails, and 2\n"
        << "on a usage or load error.\n"
        << "\n"
        << "  --version  Print the git commit this binary was built from.\n";
}

// One ideal's verdict. Each check is tri-state: an undecided solver query is
// neither a pass nor a failure, and collapsing it either way would repeat the
// mistake this tool exists to catch.
struct Verdict {
    std::string name;
    std::optional<bool> weakening;
    std::optional<bool> realisable;
    std::optional<bool> separated;
    std::optional<bool> nontrivial;
    bool reachable = true;
    std::string unreachable_reason;
};

// Prints a check as pass, fail, or "?" for undecided. Undecided never counts
// as a failure for the exit status, but it is never silently a pass either.
const char* mark(std::optional<bool> value) {
    if (!value.has_value()) {
        return "?";
    }
    return *value ? "ok" : "FAIL";
}

bool failed(std::optional<bool> value) { return value.has_value() && !*value; }

// The genetic operators can shrink the guarantee side (PRESET, ASSERT,
// GUARANTEE) by deleting a conjunct, but never grow it, and never append to
// INITIALLY or REQUIRE; ASSUME is the one section tlsf_add_assumption can grow.
// Since the seed population is copies of the original, an ideal whose section
// counts fall outside those bounds cannot be produced by any sequence of
// mutations and crossovers.
//
// Deletion arrived with p_remove_guarantee. The bound is on what the operators
// can express rather than on what one configuration turns on, so this reports
// an ideal as reachable whichever probability the run that scores it uses,
// including zero.
void check_tlsf_reachable(const tlsf::Specification& spec,
                          const tlsf::Specification& ideal, Verdict& verdict) {
    const std::size_t spec_guarantees =
        spec.m_preset.size() + spec.m_assert.size() + spec.m_guarantee.size();
    const std::size_t ideal_guarantees = ideal.m_preset.size() +
                                         ideal.m_assert.size() +
                                         ideal.m_guarantee.size();
    const std::size_t spec_fixed =
        spec.m_initially.size() + spec.m_require.size();
    const std::size_t ideal_fixed =
        ideal.m_initially.size() + ideal.m_require.size();
    if (ideal_guarantees > spec_guarantees) {
        verdict.reachable = false;
        verdict.unreachable_reason = "guarantee conjuncts " +
                                     std::to_string(spec_guarantees) + " -> " +
                                     std::to_string(ideal_guarantees) +
                                     ", but no operator adds to a "
                                     "guarantee section";
        return;
    }
    // Removal stops at one: a specification with nothing left to guarantee is
    // realizable by doing nothing, so the operator refuses the last conjunct.
    if (ideal_guarantees == 0 && spec_guarantees > 0) {
        verdict.reachable = false;
        verdict.unreachable_reason =
            "every guarantee conjunct deleted, but removal keeps the last one";
        return;
    }
    if (ideal_fixed != spec_fixed) {
        verdict.reachable = false;
        verdict.unreachable_reason = "INITIALLY/REQUIRE conjuncts " +
                                     std::to_string(spec_fixed) + " -> " +
                                     std::to_string(ideal_fixed) +
                                     ", but only ASSUME can gain a conjunct";
        return;
    }
    if (ideal.m_assume.size() < spec.m_assume.size()) {
        verdict.reachable = false;
        verdict.unreachable_reason =
            "ASSUME conjuncts " + std::to_string(spec.m_assume.size()) +
            " -> " + std::to_string(ideal.m_assume.size()) +
            ", but no operator removes one";
    }
}

// The FRETISH counterpart. mutate_specification's requirement list moves the
// same way — removal can shrink the guarantee list, nothing grows it — so a
// larger guarantee count is the analogous obstruction. The model carries only
// condition, response and timing (requirement.hpp:82), so scope and component
// changes are already outside what a loaded ideal can express and need no
// check here.
void check_fretish_reachable(const Specification& spec,
                             const Specification& ideal, Verdict& verdict) {
    if (ideal.m_guarantees.size() > spec.m_guarantees.size()) {
        verdict.reachable = false;
        verdict.unreachable_reason =
            "guarantee requirements " +
            std::to_string(spec.m_guarantees.size()) + " -> " +
            std::to_string(ideal.m_guarantees.size()) +
            ", but no operator adds to the guarantee list";
        return;
    }
    if (ideal.m_guarantees.empty() && !spec.m_guarantees.empty()) {
        verdict.reachable = false;
        verdict.unreachable_reason =
            "every guarantee requirement deleted, but removal keeps the last "
            "one";
        return;
    }
    if (ideal.m_assumptions.size() < spec.m_assumptions.size()) {
        verdict.reachable = false;
        verdict.unreachable_reason =
            "assumption requirements " +
            std::to_string(spec.m_assumptions.size()) + " -> " +
            std::to_string(ideal.m_assumptions.size()) +
            ", but no operator removes one";
    }
}

// A subject's display name. Shell globs like `examples/*/` expand with a
// trailing separator, and filename() on such a path is empty, so normalise
// before taking it or every heading prints blank.
std::string subject_name(const std::filesystem::path& dir) {
    const std::filesystem::path normalised = dir.lexically_normal();
    std::string name = normalised.filename().string();
    if (name.empty()) {
        name = normalised.parent_path().filename().string();
    }
    return name.empty() ? dir.string() : name;
}

std::string read_or_throw(const std::string& path) {
    const std::optional<std::string> contents = read_file_contents(path);
    if (!contents.has_value()) {
        throw std::runtime_error("cannot read file: " + path);
    }
    return *contents;
}

// Ideal paths for a subject, sorted so the report order is stable across runs.
std::vector<std::filesystem::path> ideal_paths(
    const std::filesystem::path& fixes, const std::string& extension) {
    std::vector<std::filesystem::path> paths;
    if (!std::filesystem::is_directory(fixes)) {
        return paths;
    }
    for (const auto& entry : std::filesystem::directory_iterator(fixes)) {
        if (entry.path().extension() == extension) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

// Reports every ideal a sibling makes redundant. `implies_ideal` succeeds when
// some repair implies some ideal, so where A implies B the weaker B always
// governs and A cannot change the score. Equivalent pairs are duplication and
// are called out separately, since one of the two is simply dead weight.
template <typename SpecT, typename ImpliesFn>
void report_redundancy(const std::vector<std::string>& names,
                       const std::vector<SpecT>& specs, ImpliesFn implies) {
    for (std::size_t i = 0; i < specs.size(); ++i) {
        for (std::size_t j = 0; j < specs.size(); ++j) {
            if (i == j) {
                continue;
            }
            const std::optional<bool> forward = implies(specs[i], specs[j]);
            const std::optional<bool> backward = implies(specs[j], specs[i]);
            if (!forward.value_or(false)) {
                continue;
            }
            if (backward.value_or(false)) {
                // Equivalent. Report once, from the lexicographically first.
                if (i < j) {
                    std::cout << "  " << names[i] << " is equivalent to "
                              << names[j] << "; one of the two is redundant\n";
                }
                continue;
            }
            std::cout << "  " << names[i] << " is strictly stronger than "
                      << names[j] << "; it cannot affect implies_ideal\n";
        }
    }
}

void print_row(const Verdict& verdict) {
    std::cout << "  " << std::left << std::setw(44) << verdict.name
              << std::setw(11) << mark(verdict.weakening) << std::setw(12)
              << mark(verdict.realisable) << std::setw(11)
              << mark(verdict.separated) << std::setw(11)
              << mark(verdict.nontrivial) << std::setw(11)
              << (verdict.reachable ? "ok" : "FAIL") << "\n";
    if (!verdict.reachable) {
        std::cout << "      unreachable: " << verdict.unreachable_reason
                  << "\n";
    }
}

void print_header() {
    std::cout << "  " << std::left << std::setw(44) << "IDEAL" << std::setw(11)
              << "weakening" << std::setw(12) << "realisable" << std::setw(11)
              << "separated" << std::setw(11) << "nontrivial" << std::setw(11)
              << "reachable" << "\n";
}

// Runs every check over one TLSF subject. Returns the number of failures.
std::size_t lint_tlsf(const std::filesystem::path& dir,
                      const std::filesystem::path& spec_path,
                      SatisfiabilityChecker& sat, RealizabilityChecker& real) {
    const tlsf::Specification spec =
        tlsf::parse(read_or_throw(spec_path.string()));
    const std::vector<std::filesystem::path> paths =
        ideal_paths(dir / "fixes", ".tlsf");
    if (paths.empty()) {
        std::cout << subject_name(dir) << ": no ideals\n";
        return 0;
    }
    std::cout << subject_name(dir) << " (" << paths.size() << " ideals)\n";
    print_header();

    std::vector<tlsf::Specification> ideals;
    std::vector<std::string> names;
    std::size_t failures = 0;
    for (const auto& path : paths) {
        const tlsf::Specification ideal =
            tlsf::parse(read_or_throw(path.string()));
        Verdict verdict;
        verdict.name = path.filename().string();
        verdict.weakening = tlsf_spec_implies(spec, ideal, sat);
        verdict.realisable = real.check_realizability_ltl(
            ideal.to_ltl(), ideal.m_inputs, ideal.m_outputs);
        const bool ill_separated =
            specification_is_not_well_separated(ideal, real);
        verdict.separated = !ill_separated;
        verdict.nontrivial = !tlsf_has_valid_guarantee(ideal, sat);
        check_tlsf_reachable(spec, ideal, verdict);
        print_row(verdict);
        failures += static_cast<std::size_t>(
            failed(verdict.weakening) || failed(verdict.realisable) ||
            failed(verdict.separated) || failed(verdict.nontrivial) ||
            !verdict.reachable);
        ideals.push_back(ideal);
        names.push_back(verdict.name);
    }
    report_redundancy(names, ideals,
                      [&sat](const tlsf::Specification& from,
                             const tlsf::Specification& dest) {
                          return tlsf_spec_implies(from, dest, sat);
                      });
    return failures;
}

// The FRETISH path. Same checks, different loaders and library entry points.
std::size_t lint_fretish(const std::filesystem::path& dir,
                         const std::filesystem::path& spec_path,
                         SatisfiabilityChecker& sat,
                         RealizabilityChecker& real) {
    const Specification spec = load_specification(spec_path.string());
    const std::vector<std::filesystem::path> paths =
        ideal_paths(dir / "fixes", ".json");
    if (paths.empty()) {
        std::cout << subject_name(dir) << ": no ideals\n";
        return 0;
    }
    std::cout << subject_name(dir) << " (" << paths.size() << " ideals)\n";
    print_header();

    std::vector<Specification> ideals;
    std::vector<std::string> names;
    std::size_t failures = 0;
    for (const auto& path : paths) {
        const Specification ideal = load_specification(path.string());
        Verdict verdict;
        verdict.name = path.filename().string();
        verdict.weakening = spec_implies(spec, ideal, sat);
        verdict.realisable = real.check_realizability(ideal);
        verdict.separated = !specification_is_not_well_separated(ideal, real);
        verdict.nontrivial = !specification_has_valid_guarantee(ideal, sat);
        check_fretish_reachable(spec, ideal, verdict);
        print_row(verdict);
        failures += static_cast<std::size_t>(
            failed(verdict.weakening) || failed(verdict.realisable) ||
            failed(verdict.separated) || failed(verdict.nontrivial) ||
            !verdict.reachable);
        ideals.push_back(ideal);
        names.push_back(verdict.name);
    }
    report_redundancy(
        names, ideals,
        [&sat](const Specification& from, const Specification& dest) {
            return spec_implies(from, dest, sat);
        });
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> dirs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--version") {
            version::print(std::cout);
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        dirs.push_back(arg);
    }
    if (dirs.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    // Generous next to a run's budgets, for the same reason compare's are: the
    // specs here have already been stretched by hand, and a lint runs rarely.
    // ltlfilt is raised with the rest -- check_satisfiability answers from
    // SPOT's constant folding before black is spawned, so a fold lost to a
    // short budget costs a verdict rather than a simplification.
    Config cfg;
    cfg.black_timeout = std::chrono::milliseconds{20'000};
    cfg.ltlsynt_timeout = std::chrono::milliseconds{60'000};
    cfg.ltl2tgba_timeout = std::chrono::milliseconds{60'000};
    cfg.ltlfilt_timeout = std::chrono::milliseconds{300'000};
    apply_tool_timeouts(cfg);
    SatisfiabilityChecker& sat = global_sat_checker();
    RealizabilityChecker real;

    std::size_t failures = 0;
    for (const std::string& name : dirs) {
        const std::filesystem::path dir(name);
        try {
            const std::filesystem::path tlsf_spec = dir / "spec.tlsf";
            const std::filesystem::path json_spec = dir / "spec.json";
            if (std::filesystem::exists(tlsf_spec)) {
                failures += lint_tlsf(dir, tlsf_spec, sat, real);
            } else if (std::filesystem::exists(json_spec)) {
                failures += lint_fretish(dir, json_spec, sat, real);
            } else {
                std::cerr << name << ": no spec.tlsf or spec.json\n";
                return 2;
            }
        } catch (const std::exception& exc) {
            std::cerr << name << ": " << exc.what() << "\n";
            return 2;
        }
    }
    std::cout << "\n" << failures << " ideal(s) failed at least one check\n";
    return failures == 0 ? 0 : 1;
}
