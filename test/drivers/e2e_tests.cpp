#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "runner/process.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

// Where the driver binaries land, from CMake. These suites are the only place
// the test binary reaches for another target's output, so the path arrives as
// a definition rather than being reconstructed from argv[0].
#ifndef PEREDUR_DRIVER_DIR
#error "PEREDUR_DRIVER_DIR must be defined by the build"
#endif

namespace {

using std::chrono::milliseconds;

// A driver that has not answered by now is wedged rather than slow: the
// specifications below decide in tens of milliseconds, and the longest run any
// suite here asks for is two generations over a population of eight.
constexpr milliseconds k_deadline{120'000};

// One request signal, one grant signal, and no way to serve a request every
// tick while alternating: the environment holds `req` high and the second
// guarantee forbids two grants in a row. Small enough that ltlsynt decides it
// in milliseconds, which is what makes it usable in eight suites.
const char* const k_unrealizable = R"(INFO {
  TITLE:       "alternating grant"
  DESCRIPTION: "unrealizable: a persistent request outruns the alternation"
  SEMANTICS:   Mealy
  TARGET:      Mealy
}

MAIN {
  INPUTS { req; }
  OUTPUTS { grant; }
  GUARANTEES {
    G(req -> X grant);
    G(grant -> X !grant);
  }
}
)";

// The same specification with the assumption that closes the gap, which is
// what `p_add_assumption` reaches for. Realizable, a genuine weakening of the
// above, and well separated, so `lint-ideals` passes it on every check.
const char* const k_realizable = R"(INFO {
  TITLE:       "alternating grant"
  DESCRIPTION: "realizable: the request cannot persist"
  SEMANTICS:   Mealy
  TARGET:      Mealy
}

MAIN {
  INPUTS { req; }
  OUTPUTS { grant; }
  ASSUMPTIONS {
    G(req -> X !req);
  }

  GUARANTEES {
    G(req -> X grant);
    G(grant -> X !grant);
  }
}
)";

// The FRETISH half, so that `peredur` is exercised on both front ends: the
// TLSF specification above never reaches src/repair/, and the two paths share
// only the CLI.
const char* const k_fretish = R"({
  "assumptions": [],
  "guarantees": [
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "takeoff_roll",
      "timing": { "type": "ForTicks", "ticks": 5 }
    },
    {
      "condition": "!takeoff_roll",
      "condition-type": "trigger",
      "response": "lift_off",
      "timing": { "type": "AfterTicks", "ticks": 1 }
    }
  ],
  "in_atoms": [],
  "out_atoms": ["takeoff_roll", "lift_off"]
}
)";

// The same specification with its second guarantee dropped, which the FRETISH
// implication check reads as a weakening: the full spec's lowering is the
// conjunction of both guarantees and implies this one, and the reverse fails
// on the guarantee that is gone. The atom lists are untouched so the two share
// an alphabet.
const char* const k_fretish_weaker = R"({
  "assumptions": [],
  "guarantees": [
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "takeoff_roll",
      "timing": { "type": "ForTicks", "ticks": 5 }
    }
  ],
  "in_atoms": [],
  "out_atoms": ["takeoff_roll", "lift_off"]
}
)";

// Two generations over eight individuals, single-threaded. The width is the
// smallest that reliably leaves the filters something to report; the thread
// count is pinned because a run these suites compare against itself must not
// depend on how many cores the machine has.
// Two generations of eight on one thread, plus a non-default value for one key
// that only the *other* path reads, so the extra key changes nothing about the
// search and the warning and the manifest's null are all it can affect.
const char* const k_config_with_fretish_key = R"([genetic]
generations = 2
population_size = 8

[mutation]
p_trigger = 0.9

[runtime]
parallel = 1
)";

const char* const k_config_with_tlsf_key = R"([genetic]
generations = 2
population_size = 8

[tlsf.mutation]
p_temporal = 0.4

[runtime]
parallel = 1
)";

std::vector<std::string> tsv_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (const char character : text) {
        if (character == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line.push_back(character);
        }
    }
    return lines;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

struct DriverRun {
    int m_exit_code = -1;
    /// stdout and stderr merged, as execute_and_capture returns them.
    std::string m_output;
};

DriverRun run_driver(const std::string& name,
                     const std::vector<std::string>& arguments) {
    std::vector<std::string> argv{std::string(PEREDUR_DRIVER_DIR) + "/" + name};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    const ProcessResult result = execute_and_capture(argv, k_deadline);
    expect(!result.m_timed_out, name + ": answered within its deadline");
    return {result.m_exit_code, result.m_output};
}

// Every driver answers --version with the commit it was built from, and
// nothing but src/version.cpp reads the generated header, so this is the only
// place a binary built against a stale one would show up.
void expect_reports_version(const std::string& name) {
    const DriverRun run = run_driver(name, {"--version"});
    expect(run.m_exit_code == 0, name + ": --version exits zero");
    expect(contains(run.m_output, "commit="),
           name + ": --version names its commit");
    expect(contains(run.m_output, "commit_short="),
           name + ": --version abbreviates its commit");
    expect(contains(run.m_output, "dirty="),
           name + ": --version reports the tree state");
}

// The repairs themselves, which a TLSF run writes beside a `repair_N.fitness`
// sidecar of the same stem prefix: matching on the prefix alone counts each
// repair twice.
std::vector<std::filesystem::path> repair_files(
    const std::filesystem::path& dir) {
    const std::string prefix = "repair_";
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string stem = entry.path().stem().string();
        if (stem.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string index = stem.substr(prefix.size());
        const bool is_repair =
            !index.empty() &&
            std::all_of(index.begin(), index.end(), [](unsigned char digit) {
                return std::isdigit(digit);
            });
        if (is_repair) {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

// The contract every completed run has to satisfy, whatever the search found.
// Deliberately not "N repairs": which candidates survive is a property of the
// operators, pinned by the determinism suite, and asserting it here would make
// every deliberate change to the search break the driver's own test.
nlohmann::json expect_run_manifest(const std::filesystem::path& dir,
                                   const std::string& input, int seed,
                                   const std::string& label) {
    const std::filesystem::path manifest_path = dir / "run.json";
    expect(std::filesystem::exists(manifest_path), label + ": writes run.json");
    const nlohmann::json manifest =
        nlohmann::json::parse(read_text(manifest_path));
    expect(manifest.at("seed").get<int>() == seed,
           label + ": the manifest carries the seed it was given");
    expect(manifest.at("input").get<std::string>() == input,
           label + ": the manifest names its input");
    expect(manifest.contains("schema_version"),
           label + ": the manifest is versioned");
    expect(
        manifest.at("config").at("genetic").at("generations").get<int>() == 2,
        label + ": the manifest echoes the config it ran under");
    expect(manifest.at("config").at("runtime").at("parallel").get<int>() == 1,
           label + ": the manifest echoes the thread count it ran under");
    expect(
        manifest.at("n_repairs").get<std::size_t>() == repair_files(dir).size(),
        label + ": the manifest's repair count matches the files written");
    // Every memo in the run, since a hit rate is what a campaign reads and
    // three of these reached no field at all before schema 20. Checked by name
    // rather than by count: a cache dropped from the block is the failure this
    // is here for, and it is silent everywhere else.
    const nlohmann::json& caches = manifest.at("caches");
    for (const char* name : {"fitness", "satisfiability", "realizability",
                             "count_traces", "ltl2tgba", "ganak",
                             "simplify_ltl", "remove_wm", "spot_satisfiable"}) {
        expect(caches.contains(name),
               label + ": the manifest reports the " + name + " cache");
        expect(caches.at(name).contains("hits") &&
                   caches.at(name).contains("misses"),
               label + ": the " + std::string(name) +
                   " cache reports both hits and misses");
    }
    // The pairwise sweep's counters, on the same argument as the caches above:
    // they reached no field at all before schema 23, so a field silently
    // dropped from this block fails nowhere else.
    const nlohmann::json& implication = manifest.at("implication");
    for (const char* name : {"comparisons", "skipped", "duplicates", "timeouts",
                             "equivalent_collapsed"}) {
        expect(
            implication.contains(name),
            label + ": the manifest reports implication." + std::string(name));
    }
    // ltlfilt's exec count is over all three of its entry points, which is the
    // set its total_s is over; reporting one of them divided a tool's seconds
    // by a fraction of its calls.
    expect(
        manifest.at("tool_calls")
                .at("ltlfilt")
                .at("calls")
                .get<std::size_t>() ==
            caches.at("simplify_ltl").at("misses").get<std::size_t>() +
                caches.at("remove_wm").at("misses").get<std::size_t>() +
                caches.at("spot_satisfiable").at("misses").get<std::size_t>(),
        label + ": ltlfilt's call count covers all three entry points");
    return manifest;
}

TEST_IN("driver_peredur", test_peredur_repairs_tlsf) {
    const TempDir dir("e2e_peredur_tlsf");
    const std::string input =
        write_text(dir.path() / "spec.tlsf", k_unrealizable).string();
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_with_fretish_key)
            .string();
    const std::filesystem::path first = dir.path() / "first";
    const std::filesystem::path second = dir.path() / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);

    const DriverRun run =
        run_driver("peredur", {"--input", input, "--output-dir", first.string(),
                               "--config", config, "--seed", "2"});
    expect(run.m_exit_code == 0, "peredur: a TLSF run exits zero");
    expect(contains(run.m_output, "Seed: 2"),
           "peredur: the run prints the seed it was given");
    expect(contains(run.m_output, "Filter report:"),
           "peredur: the run prints the filter report");
    expect(contains(run.m_output, "Done in"),
           "peredur: the run prints its closing line");
    const nlohmann::json manifest =
        expect_run_manifest(first, input, 2, "peredur/tlsf");
    // A key only the FRETISH path reads is warned about when it is changed,
    // and recorded as null, since this run's search never read it.
    expect(contains(run.m_output,
                    "config key mutation.p_trigger is read only "
                    "on the FRETISH path"),
           "peredur: a TLSF run warns about a changed FRETISH-only key");
    expect(!contains(run.m_output, "config key mutation.p_response"),
           "peredur: a FRETISH-only key left at its default is not warned "
           "about");
    const nlohmann::json& config_block = manifest.at("config");
    expect(config_block.at("mutation").at("p_trigger").is_null() &&
               config_block.at("mutation").at("p_stop").is_null(),
           "peredur: a TLSF manifest records FRETISH-only keys as null");
    expect(config_block.at("tlsf").at("mutation").at("p_temporal").is_number(),
           "peredur: a TLSF manifest records the TLSF keys");

    // The same seed twice, which is the whole claim --seed makes. Compared over
    // the repairs rather than run.json, whose timings are wall-clock.
    const DriverRun again = run_driver(
        "peredur", {"--input", input, "--output-dir", second.string(),
                    "--config", config, "--seed", "2"});
    expect(again.m_exit_code == 0, "peredur: the repeat run exits zero");
    const std::vector<std::filesystem::path> first_repairs =
        repair_files(first);
    const std::vector<std::filesystem::path> second_repairs =
        repair_files(second);
    expect(first_repairs.size() == second_repairs.size(),
           "peredur: one seed writes the same number of repairs twice");
    for (std::size_t i = 0; i < first_repairs.size(); ++i) {
        expect(first_repairs[i].filename() == second_repairs[i].filename(),
               "peredur: the repairs are named the same way twice");
        expect(read_text(first_repairs[i]) == read_text(second_repairs[i]),
               "peredur: one seed writes byte-identical repairs twice");
    }
}

TEST_IN("driver_peredur", test_peredur_repairs_fretish) {
    const TempDir dir("e2e_peredur_fretish");
    const std::string input =
        write_text(dir.path() / "spec.json", k_fretish).string();
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_with_tlsf_key).string();
    const std::filesystem::path out = dir.path() / "out";
    std::filesystem::create_directories(out);

    const DriverRun run =
        run_driver("peredur", {"--input", input, "--output-dir", out.string(),
                               "--config", config, "--seed", "5"});
    expect(run.m_exit_code == 0, "peredur: a FRETISH run exits zero");
    expect(contains(run.m_output, "Done in"),
           "peredur: the FRETISH run prints its closing line");
    const nlohmann::json manifest =
        expect_run_manifest(out, input, 5, "peredur/fretish");
    expect(contains(run.m_output,
                    "config key tlsf.mutation.p_temporal is read "
                    "only on the TLSF path"),
           "peredur: a FRETISH run warns about a changed TLSF-only key");
    const nlohmann::json& config_block = manifest.at("config");
    expect(config_block.at("tlsf").at("mutation").at("p_temporal").is_null() &&
               config_block.at("tlsf").at("muc_max_iterations").is_null(),
           "peredur: a FRETISH manifest records TLSF-only keys as null");
    expect(config_block.at("tlsf").at("repair_mode").is_string(),
           "peredur: a FRETISH manifest still records the repair_mode it "
           "enforced");
    expect(config_block.at("mutation").at("p_trigger").is_number(),
           "peredur: a FRETISH manifest records the FRETISH keys");
    for (const auto& repair : repair_files(out)) {
        expect(repair.extension() == ".json",
               "peredur: a FRETISH run writes FRETISH repairs");
        expect(nlohmann::json::parse(read_text(repair)).contains("guarantees"),
               "peredur: each repair parses as a specification");
    }
}

TEST_IN("driver_peredur", test_peredur_rejects_bad_arguments) {
    const TempDir dir("e2e_peredur_args");
    const std::string input =
        write_text(dir.path() / "spec.tlsf", k_unrealizable).string();

    // An unknown flag is refused rather than ignored, which is what stops a
    // campaign silently running without the knob it thought it set.
    const DriverRun unknown = run_driver(
        "peredur", {"--input", input, "--output-dir", dir.string(), "--bogus"});
    expect(unknown.m_exit_code != 0, "peredur: an unknown flag is refused");
    expect(contains(unknown.m_output, "--bogus"),
           "peredur: the refusal names the flag it did not accept");

    const DriverRun no_output = run_driver("peredur", {"--input", input});
    expect(no_output.m_exit_code != 0,
           "peredur: a run without --output-dir is refused");

    const DriverRun missing =
        run_driver("peredur", {"--input", (dir.path() / "absent.tlsf").string(),
                               "--output-dir", dir.string()});
    expect(missing.m_exit_code != 0,
           "peredur: an input that is not there is refused");
}

TEST_IN("driver_realize", test_realize_decides_both_ways) {
    const TempDir dir("e2e_realize");
    const std::string unrealizable =
        write_text(dir.path() / "unrealizable.tlsf", k_unrealizable).string();
    const std::string realizable =
        write_text(dir.path() / "realizable.tlsf", k_realizable).string();

    const DriverRun one = run_driver("realize", {unrealizable});
    expect(one.m_exit_code == 0, "realize: a single input exits zero");
    expect(contains(one.m_output, "UNREALIZABLE"),
           "realize: the unrealizable specification is reported as such");

    const DriverRun other = run_driver("realize", {realizable});
    expect(contains(other.m_output, "REALIZABLE") &&
               !contains(other.m_output, "UNREALIZABLE"),
           "realize: the weakened specification is realizable");

    // Several inputs at once switch the output to one labelled line each,
    // which is the shape a script reads back.
    const DriverRun both = run_driver("realize", {unrealizable, realizable});
    expect(both.m_exit_code == 0, "realize: several inputs exit zero");
    expect(contains(both.m_output, unrealizable + ": UNREALIZABLE"),
           "realize: each line names the file it decided");
    expect(contains(both.m_output, realizable + ": REALIZABLE"),
           "realize: each line carries that file's verdict");

    const DriverRun absent =
        run_driver("realize", {(dir.path() / "absent.tlsf").string()});
    expect(absent.m_exit_code != 0, "realize: an unreadable input is refused");
}

TEST_IN("driver_ltl", test_ltl_lowers_both_formats) {
    const TempDir dir("e2e_ltl");
    const std::string tlsf =
        write_text(dir.path() / "spec.tlsf", k_unrealizable).string();
    const std::string fretish =
        write_text(dir.path() / "spec.json", k_fretish).string();

    const DriverRun lowered = run_driver("ltl", {tlsf});
    expect(lowered.m_exit_code == 0, "ltl: a TLSF input exits zero");
    expect(contains(lowered.m_output, "GUARANTEE:"),
           "ltl: the TLSF sections are named");
    expect(contains(lowered.m_output, "G((req) -> (X(grant)))"),
           "ltl: each guarantee is printed as LTL");
    expect(contains(lowered.m_output, "combined LTL:"),
           "ltl: the whole lowering is printed too");

    const DriverRun requirements = run_driver("ltl", {fretish});
    expect(requirements.m_exit_code == 0, "ltl: a FRETISH input exits zero");
    expect(contains(requirements.m_output, "[guarantee]"),
           "ltl: each FRETISH requirement is labelled");
    expect(contains(requirements.m_output, "LTL:"),
           "ltl: each FRETISH requirement carries its lowering");

    const DriverRun absent =
        run_driver("ltl", {(dir.path() / "absent.tlsf").string()});
    expect(absent.m_exit_code != 0, "ltl: an unreadable input is refused");
}

TEST_IN("driver_mucs", test_mucs_extracts_a_core) {
    const TempDir dir("e2e_mucs");
    const std::string unrealizable =
        write_text(dir.path() / "unrealizable.tlsf", k_unrealizable).string();
    const std::string realizable =
        write_text(dir.path() / "realizable.tlsf", k_realizable).string();
    const std::string fretish =
        write_text(dir.path() / "spec.json", k_fretish).string();

    const DriverRun core = run_driver("mucs", {unrealizable});
    expect(core.m_exit_code == 0, "mucs: an unrealizable input exits zero");
    expect(contains(core.m_output, "core: 2 of 2 guarantee-side formulae"),
           "mucs: both guarantees are needed for the conflict");
    expect(contains(core.m_output, "[GUARANTEE] G((req) -> (X(grant)))"),
           "mucs: the core names the formulae it holds");

    const DriverRun none = run_driver("mucs", {realizable});
    expect(none.m_exit_code == 0, "mucs: a realizable input exits zero");
    expect(contains(none.m_output, "REALIZABLE (no core)"),
           "mucs: a realizable input has no core to report");

    // FRETISH is refused rather than half-handled: the extraction works over
    // TLSF guarantee-side sections, which FRETISH JSON has no equivalent of.
    const DriverRun wrong_format = run_driver("mucs", {fretish});
    expect(wrong_format.m_exit_code != 0, "mucs: FRETISH JSON is refused");
    expect(contains(wrong_format.m_output, ".tlsf"),
           "mucs: the refusal says which format it wanted");
}

TEST_IN("driver_compare", test_compare_orders_repairs_against_ideals) {
    const TempDir dir("e2e_compare");
    const std::filesystem::path repairs = dir.path() / "repairs";
    const std::filesystem::path ideals = dir.path() / "ideals";
    write_text(repairs / "repair_0.tlsf", k_realizable);
    write_text(ideals / "add_assumption.tlsf", k_realizable);

    const DriverRun run = run_driver("compare", {"--repairs", repairs.string(),
                                                 "--ideals", ideals.string()});
    expect(run.m_exit_code == 0, "compare: a run over one pair exits zero");
    expect(contains(run.m_output, "equivalent to add_assumption.tlsf"),
           "compare: a repair identical to the ideal is equivalent to it");
    expect(contains(run.m_output, "Summary: 1 equivalent"),
           "compare: the summary counts the relation it found");

    // The unrealizable original is strictly stronger than its own weakening,
    // which is the relation the search is trying to move away from.
    const std::filesystem::path stronger = dir.path() / "stronger";
    write_text(stronger / "repair_0.tlsf", k_unrealizable);
    const DriverRun ordered = run_driver(
        "compare",
        {"--repairs", stronger.string(), "--ideals", ideals.string()});
    expect(ordered.m_exit_code == 0, "compare: the second run exits zero");
    expect(contains(ordered.m_output, "Summary: 0 equivalent"),
           "compare: the original is not equivalent to its weakening");

    const DriverRun absent =
        run_driver("compare", {"--repairs", (dir.path() / "absent").string(),
                               "--ideals", ideals.string()});
    expect(absent.m_exit_code != 0,
           "compare: a repairs directory that is not there is refused");
}

TEST_IN("driver_lint_ideals", test_lint_ideals_checks_a_subject) {
    const TempDir dir("e2e_lint_ideals");
    const std::filesystem::path good = dir.path() / "good";
    write_text(good / "spec.tlsf", k_unrealizable);
    write_text(good / "fixes" / "add_assumption.tlsf", k_realizable);

    const DriverRun passing = run_driver("lint-ideals", {good.string()});
    expect(passing.m_exit_code == 0,
           "lint-ideals: a subject whose ideal passes exits zero");
    expect(contains(passing.m_output, "add_assumption.tlsf"),
           "lint-ideals: the table names each ideal");
    expect(contains(passing.m_output, "0 ideal(s) failed at least one check"),
           "lint-ideals: nothing is reported against a good ideal");

    // The unrealizable specification as its own ideal: a weakening of itself,
    // reachable, well separated and non-trivial, and unrealizable, so exactly
    // one column fails.
    const std::filesystem::path bad = dir.path() / "bad";
    write_text(bad / "spec.tlsf", k_unrealizable);
    write_text(bad / "fixes" / "still_unrealizable.tlsf", k_unrealizable);

    const DriverRun failing = run_driver("lint-ideals", {bad.string()});
    expect(failing.m_exit_code == 1,
           "lint-ideals: a subject with a failing ideal exits one");
    expect(contains(failing.m_output, "FAIL"),
           "lint-ideals: the failing check is marked in the table");
    expect(contains(failing.m_output, "1 ideal(s) failed at least one check"),
           "lint-ideals: the footer counts the failures");

    const DriverRun absent =
        run_driver("lint-ideals", {(dir.path() / "absent").string()});
    expect(absent.m_exit_code == 2,
           "lint-ideals: a subject that is not there is a load error");
}

// signal_tracer reads its frames from stdin, which execute_and_capture leaves
// as the test process's own — under ctest that is whatever invoked it, so the
// driver's input would be neither empty nor under this suite's control. The
// piped spawn is how a caller states it: closing the write end immediately is
// a trace of no frames, which is the deterministic half of a crash report. The
// frames are the part that depends on where the crash happened.
std::string run_signal_tracer(const std::vector<std::string>& arguments) {
    std::vector<std::string> argv{std::string(PEREDUR_DRIVER_DIR) +
                                  "/signal_tracer"};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    const PipedChild child =
        spawn_piped_child(argv, ParentDeathPolicy::KillWithParentThread,
                          ExecutableLookup::AbsolutePath);
    close(child.m_write_fd);
    const std::pair<std::string, bool> read =
        read_until_eof(child.m_read_fd, k_deadline);
    expect(!read.second, "signal_tracer: answered within its deadline");
    close(child.m_read_fd);
    reap_with_grace(child.m_pid, milliseconds{1'000}, "signal_tracer",
                    child.m_rss_floor_kb);
    return read.first;
}

TEST_IN("driver_signal_tracer", test_signal_tracer_writes_a_report) {
    const TempDir dir("e2e_signal_tracer");
    const std::filesystem::path report = dir.path() / "crash.txt";

    const std::string streamed =
        run_signal_tracer({report.string(), "6", "4242", "seed=1 spec=probe"});
    expect(streamed.empty(),
           "signal_tracer: a named report file takes the whole report");
    const std::string written = read_text(report);
    expect(contains(written, "=== CRASH REPORT ==="),
           "signal_tracer: the report is written to the named file");
    expect(contains(written, "Signal: SIGABRT (6)"),
           "signal_tracer: the signal number is translated to its name");
    expect(contains(written, "PID:    4242"),
           "signal_tracer: the crashed process is identified");
    expect(contains(written, "seed=1 spec=probe"),
           "signal_tracer: the crash metadata is carried through");
    expect(contains(written, "Stack trace:"),
           "signal_tracer: the trace section is written even when empty");

    // A second crash appends rather than replacing: a run that dies twice must
    // not lose the first report.
    run_signal_tracer({report.string(), "11", "4243", ""});
    const std::string appended = read_text(report);
    expect(contains(appended, "PID:    4242") &&
               contains(appended, "PID:    4243"),
           "signal_tracer: a second report is appended to the first");
    expect(contains(appended, "Signal: SIGSEGV (11)"),
           "signal_tracer: the second signal is named too");
}

TEST_IN("driver_maximal", test_maximal_reports_both_formats) {
    const TempDir dir("e2e_maximal");

    // The unrealizable specification implies its own weakening and not the
    // reverse, so the pair has one maximal member; the third file is a copy of
    // the second and collapses into it before any solver call.
    const std::filesystem::path tlsf = dir.path() / "tlsf";
    write_text(tlsf / "original.tlsf", k_unrealizable);
    write_text(tlsf / "weaker.tlsf", k_realizable);
    write_text(tlsf / "weaker_again.tlsf", k_realizable);

    const DriverRun tlsf_run = run_driver("maximal", {tlsf.string()});
    expect(tlsf_run.m_exit_code == 0, "maximal: a TLSF directory exits zero");
    expect(contains(tlsf_run.m_output, "files      3") &&
               contains(tlsf_run.m_output, "distinct   2"),
           "maximal: structural duplicates collapse before the sweep");
    expect(contains(tlsf_run.m_output, "maximal    1") &&
               contains(tlsf_run.m_output, "classes    1"),
           "maximal: the weakening is dominated by the specification it came "
           "from");
    expect(contains(tlsf_run.m_output, "class 0  ") &&
               contains(tlsf_run.m_output, "original.tlsf"),
           "maximal: the survivor is named by its file");

    // The FRETISH half, over a directory shaped like a run's output: the
    // repairs carry a fitness block and the manifest sits beside them. Reading
    // the manifest as a specification would land in the report as an unparsed
    // file, so its absence from the counts is the assertion.
    const std::filesystem::path fretish = dir.path() / "fretish";
    write_text(fretish / "repair_0.json", k_fretish);
    write_text(fretish / "repair_1.json", k_fretish_weaker);
    write_text(fretish / "run.json", "{\"schema_version\": 25}\n");

    const DriverRun fretish_run = run_driver("maximal", {fretish.string()});
    expect(fretish_run.m_exit_code == 0,
           "maximal: a FRETISH directory exits zero");
    expect(contains(fretish_run.m_output, "files      2") &&
               contains(fretish_run.m_output, "distinct   2"),
           "maximal: the run manifest is not read as a repair");
    expect(!contains(fretish_run.m_output, "unparsed"),
           "maximal: nothing in a run output directory fails to parse");
    expect(contains(fretish_run.m_output, "maximal    1") &&
               contains(fretish_run.m_output, "repair_0.json"),
           "maximal: the full specification dominates the one that dropped a "
           "guarantee");

    // Nothing of either extension, so the format falls through to FRETISH and
    // the refusal names what it looked for.
    const std::filesystem::path empty = dir.path() / "empty";
    std::filesystem::create_directories(empty);
    const DriverRun nothing = run_driver("maximal", {empty.string()});
    expect(nothing.m_exit_code != 0,
           "maximal: a directory holding no specifications is refused");
    expect(contains(nothing.m_output, ".json"),
           "maximal: the refusal says which extension it wanted");
}

// A FRETISH specification that is unrealizable over an input: the environment
// can raise `req`, which demands a `grant` the second guarantee forbids. The
// FRETISH k_fretish above is realizable, so the FRETISH halves of realize,
// compare and lint-ideals need this one to see both verdicts.
const char* const k_fretish_unrealizable = R"({
  "assumptions": [],
  "guarantees": [
    {
      "condition": "req",
      "condition-type": "trigger",
      "response": "grant",
      "timing": { "type": "Immediately" }
    },
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "!grant",
      "timing": { "type": "Always" }
    }
  ],
  "in_atoms": ["req"],
  "out_atoms": ["grant"]
}
)";

// The assumption that closes the gap: an environment that never requests.
// Realizable, and a weakening of the specification above.
const char* const k_fretish_assumed = R"({
  "assumptions": [
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "!req",
      "timing": { "type": "Always" }
    }
  ],
  "guarantees": [
    {
      "condition": "req",
      "condition-type": "trigger",
      "response": "grant",
      "timing": { "type": "Immediately" }
    },
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "!grant",
      "timing": { "type": "Always" }
    }
  ],
  "in_atoms": ["req"],
  "out_atoms": ["grant"]
}
)";

// k_fretish_assumed without its second guarantee, so strictly weaker than it.
const char* const k_fretish_assumed_weaker = R"({
  "assumptions": [
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "!req",
      "timing": { "type": "Always" }
    }
  ],
  "guarantees": [
    {
      "condition": "req",
      "condition-type": "trigger",
      "response": "grant",
      "timing": { "type": "Immediately" }
    }
  ],
  "in_atoms": ["req"],
  "out_atoms": ["grant"]
}
)";

// k_fretish_assumed with a third guarantee, which no operator can produce from
// the two-guarantee original.
const char* const k_fretish_extra_guarantee = R"({
  "assumptions": [
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "!req",
      "timing": { "type": "Always" }
    }
  ],
  "guarantees": [
    {
      "condition": "req",
      "condition-type": "trigger",
      "response": "grant",
      "timing": { "type": "Immediately" }
    },
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "!grant",
      "timing": { "type": "Always" }
    },
    {
      "condition": "req",
      "condition-type": "trigger",
      "response": "grant",
      "timing": { "type": "NextTimepoint" }
    }
  ],
  "in_atoms": ["req"],
  "out_atoms": ["grant"]
}
)";

// k_unrealizable with its input renamed to `Req`. ltlsynt's --ins never
// matches an uppercase name, so every driver that decides realizability has
// to refuse it at load.
const char* const k_uppercase_input = R"(INFO {
  TITLE:       "alternating grant"
  DESCRIPTION: "an input name ltlsynt cannot match"
  SEMANTICS:   Mealy
  TARGET:      Mealy
}

MAIN {
  INPUTS { Req; }
  OUTPUTS { grant; }
  GUARANTEES {
    G(Req -> X grant);
    G(grant -> X !grant);
  }
}
)";

// k_unrealizable's first guarantee as a disjunction: a different spelling of
// the same specification, which only a solver call can tell is equivalent.
const char* const k_unrealizable_respelled = R"(INFO {
  TITLE:       "alternating grant"
  DESCRIPTION: "unrealizable, spelled differently"
  SEMANTICS:   Mealy
  TARGET:      Mealy
}

MAIN {
  INPUTS { req; }
  OUTPUTS { grant; }
  GUARANTEES {
    G(!req || X grant);
    G(grant -> X !grant);
  }
}
)";

// k_unrealizable over another input name, so it shares no alphabet with it.
const char* const k_unrealizable_renamed = R"(INFO {
  TITLE:       "alternating grant"
  DESCRIPTION: "unrealizable, over another input"
  SEMANTICS:   Mealy
  TARGET:      Mealy
}

MAIN {
  INPUTS { ask; }
  OUTPUTS { grant; }
  GUARANTEES {
    G(ask -> X grant);
    G(grant -> X !grant);
  }
}
)";

const char* const k_config_small = R"([genetic]
generations = 2
population_size = 8

[runtime]
parallel = 1
)";

// With the accumulator off the final filters run once over the last
// population, rather than streaming while the search does, which is the only
// way a run reaches the batch maximality filter.
const char* const k_config_batch_final_filters = R"([genetic]
generations = 2
population_size = 8
accumulate_repairs = false

[runtime]
parallel = 1
)";

const char* const k_config_muc = R"([genetic]
generations = 2
population_size = 8

[runtime]
parallel = 1

[tlsf]
repair_mode = "muc"
)";

// Long enough that the run is still searching whenever the signal lands.
const char* const k_config_endless = R"([genetic]
generations = 1000000
population_size = 8

[runtime]
parallel = 1
)";

TEST_IN("driver_peredur", test_peredur_prints_help) {
    for (const char* flag : {"--help", "-h"}) {
        const DriverRun run = run_driver("peredur", {flag});
        expect(run.m_exit_code == 0,
               std::string("peredur: ") + flag + " exits zero");
        expect(contains(run.m_output, "Usage:") &&
                   contains(run.m_output, "--output-dir <dir>"),
               std::string("peredur: ") + flag + " prints the usage");
    }
}

TEST_IN("driver_peredur", test_peredur_rejects_bad_values) {
    const TempDir dir("e2e_peredur_values");
    const std::string input =
        write_text(dir.path() / "spec.json", k_fretish_unrealizable).string();
    const std::string out = dir.string();

    for (const char* seed : {"abc", "-1"}) {
        const DriverRun run = run_driver(
            "peredur", {"--input", input, "--output-dir", out, "--seed", seed});
        expect(run.m_exit_code == 1,
               std::string("peredur: --seed ") + seed + " is refused");
        expect(contains(run.m_output, "Invalid --seed value"),
               std::string("peredur: the refusal of --seed ") + seed +
                   " says why");
    }

    const DriverRun format = run_driver(
        "peredur", {"--input", input, "--output-dir", out, "--format", "yaml"});
    expect(format.m_exit_code == 1, "peredur: an unknown --format is refused");
    expect(contains(format.m_output, "Unknown --format value: 'yaml'"),
           "peredur: the refusal names the format it did not accept");

    const DriverRun no_dir = run_driver(
        "peredur",
        {"--input", input, "--output-dir", (dir.path() / "absent").string()});
    expect(no_dir.m_exit_code == 1,
           "peredur: an output directory that is not there is refused");
    expect(contains(no_dir.m_output, "Output directory does not exist"),
           "peredur: the refusal says the output directory is missing");

    const std::string broken =
        write_text(dir.path() / "broken.toml", "[genetic\n").string();
    const DriverRun parse = run_driver(
        "peredur", {"--input", input, "--output-dir", out, "--config", broken});
    expect(parse.m_exit_code == 1, "peredur: a malformed config is refused");
    expect(contains(parse.m_output, "TOML parse error"),
           "peredur: the refusal says the config did not parse");

    const DriverRun absent = run_driver(
        "peredur", {"--input", input, "--output-dir", out, "--config",
                    (dir.path() / "absent.toml").string()});
    expect(absent.m_exit_code == 1,
           "peredur: a config file that is not there is refused");
    expect(contains(absent.m_output, "file does not exist"),
           "peredur: the refusal says the config file is missing");
}

TEST_IN("driver_peredur", test_peredur_rejects_unusable_inputs) {
    const TempDir dir("e2e_peredur_inputs");
    const std::string out = dir.string();

    // repair_mode is read only on the TLSF path, and a FRETISH run that
    // ignored it would record a mode in its manifest that it never ran.
    const std::string fretish =
        write_text(dir.path() / "spec.json", k_fretish_unrealizable).string();
    const std::string muc =
        write_text(dir.path() / "muc.toml", k_config_muc).string();
    const DriverRun mode = run_driver(
        "peredur", {"--input", fretish, "--output-dir", out, "--config", muc});
    expect(mode.m_exit_code == 1,
           "peredur: a FRETISH run under repair_mode = muc is refused");
    expect(contains(mode.m_output, "repair_mode is TLSF-only"),
           "peredur: the refusal names the TLSF-only key");

    const std::string malformed =
        write_text(dir.path() / "malformed.json", "{\"guarantees\": [")
            .string();
    const DriverRun parse =
        run_driver("peredur", {"--input", malformed, "--output-dir", out});
    expect(parse.m_exit_code == 1,
           "peredur: a FRETISH input that does not parse is refused");
    expect(contains(parse.m_output, "JSON parse error"),
           "peredur: the refusal says the input did not parse");

    // Refused on both front ends, before any realizability verdict is made.
    const std::string upper_tlsf =
        write_text(dir.path() / "upper.tlsf", k_uppercase_input).string();
    const DriverRun tlsf =
        run_driver("peredur", {"--input", upper_tlsf, "--output-dir", out});
    expect(tlsf.m_exit_code == 1,
           "peredur: a TLSF input with an uppercase input name is refused");
    expect(contains(tlsf.m_output, "input 'Req' contains an uppercase letter"),
           "peredur: the TLSF refusal names the unsafe atom");

    nlohmann::json upper = nlohmann::json::parse(k_fretish_unrealizable);
    upper["in_atoms"] = {"Req"};
    upper["guarantees"][0]["condition"] = "Req";
    const std::string upper_fretish =
        write_text(dir.path() / "upper.json", upper.dump()).string();
    const DriverRun json =
        run_driver("peredur", {"--input", upper_fretish, "--output-dir", out});
    expect(json.m_exit_code == 1,
           "peredur: a FRETISH input with an uppercase input name is refused");
    expect(contains(json.m_output, "contains an uppercase letter"),
           "peredur: the FRETISH refusal names the unsafe atom");
}

TEST_IN("driver_peredur", test_peredur_format_overrides_the_extension) {
    const TempDir dir("e2e_peredur_format");
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_small).string();

    // TLSF under an extension that would otherwise select FRETISH.
    const std::string text =
        write_text(dir.path() / "spec.txt", k_unrealizable).string();
    const std::filesystem::path out = dir.path() / "out";
    std::filesystem::create_directories(out);
    const DriverRun tlsf = run_driver(
        "peredur", {"--input", text, "--output-dir", out.string(), "--config",
                    config, "--seed", "1", "--format", "tlsf"});
    expect(tlsf.m_exit_code == 0,
           "peredur: --format tlsf reads a .txt input as TLSF");
    expect_run_manifest(out, text, 1, "peredur/format");
    for (const auto& repair : repair_files(out)) {
        expect(repair.extension() == ".tlsf",
               "peredur: --format tlsf writes TLSF repairs");
    }

    // And the reverse: a .tlsf file read as FRETISH JSON does not parse.
    const std::string tlsf_file =
        write_text(dir.path() / "spec.tlsf", k_unrealizable).string();
    const DriverRun fretish =
        run_driver("peredur", {"--input", tlsf_file, "--output-dir",
                               out.string(), "--format", "fretish"});
    expect(fretish.m_exit_code == 1,
           "peredur: --format fretish reads a .tlsf input as JSON");
    expect(contains(fretish.m_output, "JSON parse error"),
           "peredur: TLSF read as FRETISH fails to parse");
}

// The seed is drawn when --seed is absent, and the run has to print the one it
// drew and record the same one, or the run cannot be reproduced.
std::size_t printed_seed(const std::string& output) {
    const std::string marker = "Seed: ";
    const std::size_t at = output.find(marker);
    expect(at != std::string::npos, "peredur: the run prints its seed");
    if (at == std::string::npos) {
        return 0;
    }
    return std::stoull(output.substr(at + marker.size()));
}

TEST_IN("driver_peredur", test_peredur_reports_on_request) {
    const TempDir dir("e2e_peredur_reports");
    const std::string input =
        write_text(dir.path() / "spec.json", k_fretish_unrealizable).string();
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_small).string();
    const std::filesystem::path out = dir.path() / "out";
    std::filesystem::create_directories(out);

    const DriverRun run = run_driver(
        "peredur", {"--input", input, "--output-dir", out.string(), "--config",
                    config, "--dashboard", "--diagnostics", "--cpu-report"});
    expect(run.m_exit_code == 0, "peredur: a run with every report exits zero");
    const std::size_t seed = printed_seed(run.m_output);
    const nlohmann::json manifest =
        nlohmann::json::parse(read_text(out / "run.json"));
    expect(manifest.at("seed").get<std::size_t>() == seed,
           "peredur: a drawn seed is the one the manifest records");

    expect(contains(run.m_output, "Tool timing report:") &&
               contains(run.m_output, "Cache report:"),
           "peredur: --diagnostics prints the tool and cache reports");
    expect(contains(run.m_output, "CPU attribution (wall "),
           "peredur: --cpu-report prints the CPU attribution");

    expect(contains(run.m_output, "Progress: "),
           "peredur: --dashboard prints where the progress goes");
    expect(std::filesystem::exists(out / "index.html"),
           "peredur: --dashboard writes the page beside the progress file");
    const std::string progress = read_text(out / "progress.jsonl");
    std::size_t n_events = 0;
    for (const std::string& line : tsv_lines(progress)) {
        expect(nlohmann::json::accept(line),
               "peredur: each progress line is one JSON object");
        ++n_events;
    }
    expect(n_events >= 2, "peredur: the progress file records the run");
}

TEST_IN("driver_peredur", test_peredur_runs_tlsf_muc_mode) {
    const TempDir dir("e2e_peredur_muc");
    const std::string input =
        write_text(dir.path() / "spec.tlsf", k_unrealizable).string();
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_muc).string();
    const std::filesystem::path out = dir.path() / "out";
    std::filesystem::create_directories(out);

    const DriverRun run = run_driver(
        "peredur", {"--input", input, "--output-dir", out.string(), "--config",
                    config, "--seed", "4", "--diagnostics", "--cpu-report"});
    expect(run.m_exit_code == 0, "peredur: a muc-mode run exits zero");
    expect(contains(run.m_output, "muc iteration 1/"),
           "peredur: muc mode reports its first core iteration");
    expect(contains(run.m_output, "Tool timing report:") &&
               contains(run.m_output, "CPU attribution (wall "),
           "peredur: a TLSF run prints the reports it was asked for");
    const nlohmann::json manifest =
        expect_run_manifest(out, input, 4, "peredur/muc");
    expect(manifest.at("config").at("tlsf").at("repair_mode") == "muc",
           "peredur: the manifest records the mode the run used");
}

TEST_IN("driver_peredur", test_peredur_filters_without_the_accumulator) {
    const TempDir dir("e2e_peredur_batch");
    const std::string input =
        write_text(dir.path() / "spec.json", k_fretish_unrealizable).string();
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_batch_final_filters)
            .string();
    const std::filesystem::path out = dir.path() / "out";
    std::filesystem::create_directories(out);

    const DriverRun run =
        run_driver("peredur", {"--input", input, "--output-dir", out.string(),
                               "--config", config, "--seed", "4"});
    expect(run.m_exit_code == 0,
           "peredur: a run with the accumulator off exits zero");
    expect(contains(run.m_output, "final/implication"),
           "peredur: the batch final filters report their own rows");
    expect_run_manifest(out, input, 4, "peredur/batch");
}

// The crash log lands in a `crashes/` directory under the working directory,
// named after the crashed process, which is this test's own working directory.
std::vector<std::filesystem::path> crash_logs_of(pid_t pid) {
    const std::filesystem::path crashes =
        std::filesystem::current_path() / "crashes";
    const std::string prefix = "crash_" + std::to_string(pid) + "_";
    std::vector<std::filesystem::path> found;
    if (!std::filesystem::is_directory(crashes)) {
        return found;
    }
    for (const auto& entry : std::filesystem::directory_iterator(crashes)) {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) {
            found.push_back(entry.path());
        }
    }
    return found;
}

TEST_IN("driver_peredur", test_peredur_writes_a_crash_report) {
    const TempDir dir("e2e_peredur_crash");
    const std::string input =
        write_text(dir.path() / "spec.json", k_fretish_unrealizable).string();
    const std::string config =
        write_text(dir.path() / "config.toml", k_config_endless).string();
    const std::filesystem::path out = dir.path() / "out";
    std::filesystem::create_directories(out);

    const PipedChild child =
        spawn_piped_child({std::string(PEREDUR_DRIVER_DIR) + "/peredur",
                           "--input", input, "--output-dir", out.string(),
                           "--config", config, "--seed", "7", "--dashboard"},
                          ParentDeathPolicy::KillWithParentThread,
                          ExecutableLookup::AbsolutePath);
    close(child.m_write_fd);

    // The progress file is opened after the handler is installed and the seed
    // registered, so its existence is the earliest moment a signal is a crash
    // rather than a kill. A signal sent before then takes the default action
    // and leaves no report.
    const std::filesystem::path progress = out / "progress.jsonl";
    const auto give_up = std::chrono::steady_clock::now() + k_deadline;
    while (!std::filesystem::exists(progress) &&
           std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(milliseconds{20});
    }
    expect(std::filesystem::exists(progress),
           "peredur: the endless run reached its search");
    kill(child.m_pid, SIGSEGV);

    const std::pair<std::string, bool> read =
        read_until_eof(child.m_read_fd, k_deadline);
    expect(!read.second, "peredur: the crashed run closed its output");
    close(child.m_read_fd);
    reap_with_grace(child.m_pid, milliseconds{1'000}, "peredur",
                    child.m_rss_floor_kb);

    const std::vector<std::filesystem::path> logs = crash_logs_of(child.m_pid);
    expect(logs.size() == 1, "peredur: a crash writes one report");
    for (const auto& log : logs) {
        const std::string report = read_text(log);
        // Removed before anything is asserted: left behind by a failure, it
        // would read as a real crash of this build.
        std::filesystem::remove(log);
        expect(contains(report, "=== CRASH REPORT ==="),
               "peredur: the report carries its heading");
        expect(contains(report, "Signal: SIGSEGV (11)"),
               "peredur: the report names the signal");
        expect(contains(report, "PID:    " + std::to_string(child.m_pid)),
               "peredur: the report names the crashed process");
        expect(contains(report, "Input:            " + input),
               "peredur: the report names the input");
        expect(contains(report, "  Seed:           7"),
               "peredur: the report carries the seed that reproduces it");
        expect(contains(report, "Stack trace:"),
               "peredur: the report carries a stack trace");
    }
}

TEST_IN("driver_realize", test_realize_decides_fretish) {
    const TempDir dir("e2e_realize_fretish");
    const std::string unrealizable =
        write_text(dir.path() / "unrealizable.json", k_fretish_unrealizable)
            .string();
    const std::string realizable =
        write_text(dir.path() / "realizable.json", k_fretish_assumed).string();

    const DriverRun one = run_driver("realize", {unrealizable});
    expect(one.m_exit_code == 0 && contains(one.m_output, "UNREALIZABLE"),
           "realize: the unrealizable FRETISH specification is reported");

    const DriverRun both = run_driver("realize", {unrealizable, realizable});
    expect(both.m_exit_code == 0, "realize: several FRETISH inputs exit zero");
    expect(contains(both.m_output, unrealizable + ": UNREALIZABLE") &&
               contains(both.m_output, realizable + ": REALIZABLE"),
           "realize: each FRETISH line carries its file's verdict");

    // A file that does not parse fails the whole run, and among several the
    // message names the one that failed.
    const std::string malformed =
        write_text(dir.path() / "malformed.json", "{\"guarantees\": [")
            .string();
    const DriverRun broken = run_driver("realize", {malformed, realizable});
    expect(broken.m_exit_code == 1,
           "realize: an input that does not parse fails the run");
    expect(contains(broken.m_output, malformed + ": JSON parse error"),
           "realize: the failure names the file that did not parse");
}

TEST_IN("driver_realize", test_realize_refuses_unsafe_atoms) {
    const TempDir dir("e2e_realize_atoms");
    const std::string upper =
        write_text(dir.path() / "upper.tlsf", k_uppercase_input).string();
    const DriverRun run = run_driver("realize", {upper});
    expect(run.m_exit_code == 1,
           "realize: an uppercase input name is refused rather than decided");
    expect(contains(run.m_output, "input 'Req' contains an uppercase letter"),
           "realize: the refusal names the unsafe atom");
}

TEST_IN("driver_realize", test_realize_prints_usage) {
    const DriverRun bare = run_driver("realize", {});
    expect(bare.m_exit_code == 1, "realize: no input is a usage error");
    expect(contains(bare.m_output, "Usage:"),
           "realize: a usage error prints the usage");
    const DriverRun help = run_driver("realize", {"--help"});
    expect(help.m_exit_code == 0 && contains(help.m_output, "Usage:"),
           "realize: --help prints the usage and exits zero");
}

TEST_IN("driver_ltl", test_ltl_labels_several_inputs) {
    const TempDir dir("e2e_ltl_several");
    const std::string tlsf =
        write_text(dir.path() / "spec.tlsf", k_unrealizable).string();
    const std::string fretish =
        write_text(dir.path() / "assumed.json", k_fretish_assumed).string();

    const DriverRun run = run_driver("ltl", {tlsf, fretish});
    expect(run.m_exit_code == 0, "ltl: several inputs exit zero");
    expect(contains(run.m_output, tlsf + ":\n") &&
               contains(run.m_output, fretish + ":\n"),
           "ltl: each input's block is headed by its path");
    expect(contains(run.m_output, "[assumption]"),
           "ltl: FRETISH assumptions are labelled as such");
    expect(contains(run.m_output, "G(!(req))"),
           "ltl: each FRETISH assumption carries its lowering");

    const DriverRun bare = run_driver("ltl", {});
    expect(bare.m_exit_code == 1 && contains(bare.m_output, "Usage:"),
           "ltl: no input is a usage error");
}

TEST_IN("driver_mucs", test_mucs_refuses_unusable_inputs) {
    const TempDir dir("e2e_mucs_inputs");
    const std::string upper =
        write_text(dir.path() / "upper.tlsf", k_uppercase_input).string();
    const DriverRun run = run_driver("mucs", {upper});
    expect(run.m_exit_code == 1, "mucs: an uppercase input name is refused");
    expect(contains(run.m_output, "contains an uppercase letter"),
           "mucs: the refusal names the unsafe atom");

    const DriverRun bare = run_driver("mucs", {});
    expect(bare.m_exit_code == 1 && contains(bare.m_output, "Usage:"),
           "mucs: no input is a usage error");
}

TEST_IN("driver_compare", test_compare_orders_fretish_repairs) {
    const TempDir dir("e2e_compare_fretish");
    const std::filesystem::path ideals = dir.path() / "ideals";
    write_text(ideals / "assumed.json", k_fretish_assumed);

    // A run's output directory, manifest included: run.json is not a repair.
    const std::filesystem::path same = dir.path() / "same";
    write_text(same / "repair_0.json", k_fretish_assumed);
    write_text(same / "run.json", "{\"schema_version\": 25}\n");
    const DriverRun equivalent = run_driver(
        "compare", {"--repairs", same.string(), "--ideals", ideals.string()});
    expect(equivalent.m_exit_code == 0,
           "compare: a FRETISH run over one pair exits zero");
    expect(contains(equivalent.m_output, "equivalent to assumed.json"),
           "compare: a FRETISH repair identical to the ideal is equivalent");
    expect(contains(equivalent.m_output, "Summary: 1 equivalent"),
           "compare: the run manifest is not compared as a repair");

    // The unrealizable original implies the ideal and not the reverse; the
    // ideal with a guarantee dropped is implied by it and not the reverse.
    const std::filesystem::path ordered = dir.path() / "ordered";
    write_text(ordered / "repair_0.json", k_fretish_unrealizable);
    write_text(ordered / "repair_1.json", k_fretish_assumed_weaker);
    const DriverRun both = run_driver("compare", {"--repairs", ordered.string(),
                                                  "--ideals", ideals.string()});
    expect(both.m_exit_code == 0, "compare: the ordered run exits zero");
    expect(contains(both.m_output, "strictly stronger than assumed.json"),
           "compare: the original is strictly stronger than its weakening");
    expect(contains(both.m_output, "strictly weaker than assumed.json"),
           "compare: a dropped guarantee is strictly weaker than the ideal");
    expect(contains(both.m_output,
                    "Summary: 0 equivalent, 1 strictly stronger, 1 strictly "
                    "weaker"),
           "compare: the summary counts each relation once");
}

TEST_IN("driver_compare", test_compare_rejects_bad_arguments) {
    const TempDir dir("e2e_compare_args");
    const std::filesystem::path ideals = dir.path() / "ideals";
    write_text(ideals / "assumed.json", k_fretish_assumed);
    const std::filesystem::path empty = dir.path() / "empty";
    std::filesystem::create_directories(empty);

    const DriverRun bare = run_driver("compare", {});
    expect(bare.m_exit_code == 1 && contains(bare.m_output, "Usage:"),
           "compare: no directories is a usage error");

    const DriverRun unknown =
        run_driver("compare", {"--repairs", empty.string(), "--bogus"});
    expect(unknown.m_exit_code == 1 &&
               contains(unknown.m_output, "Unknown argument: --bogus"),
           "compare: an unknown flag is refused by name");

    const DriverRun nothing = run_driver(
        "compare", {"--repairs", empty.string(), "--ideals", ideals.string()});
    expect(nothing.m_exit_code == 1,
           "compare: a repairs directory with nothing in it is refused");
    expect(contains(nothing.m_output, "No .json files found in"),
           "compare: the refusal says which extension it wanted");
}

TEST_IN("driver_maximal", test_maximal_reports_unparsed_and_equivalent) {
    const TempDir dir("e2e_maximal_mixed");
    const std::filesystem::path specs = dir.path() / "specs";
    write_text(specs / "original.tlsf", k_unrealizable);
    write_text(specs / "respelled.tlsf", k_unrealizable_respelled);
    write_text(specs / "garbage.tlsf", "garbage\n");

    // Two spellings of one specification are distinct files and one class,
    // and a file that does not parse is counted rather than fatal.
    const DriverRun run = run_driver(
        "maximal", {specs.string(), "--jobs", "2", "--timeout", "5"});
    expect(run.m_exit_code == 0,
           "maximal: a directory with an unparsed file still exits zero");
    expect(contains(run.m_output, "files      2") &&
               contains(run.m_output, "distinct   2"),
           "maximal: the two spellings are distinct before the solver");
    expect(contains(run.m_output, "maximal    1") &&
               contains(run.m_output, "classes    1"),
           "maximal: equivalent spellings leave one maximal member");
    expect(contains(run.m_output, "unparsed   1"),
           "maximal: the file that did not parse is counted");
    expect(contains(run.m_output, "garbage.tlsf: TLSF parse error"),
           "maximal: the file that did not parse is named");

    write_text(specs / "renamed.tlsf", k_unrealizable_renamed);
    const DriverRun mixed = run_driver("maximal", {specs.string()});
    expect(mixed.m_exit_code == 0, "maximal: mixed alphabets still exit zero");
    expect(contains(mixed.m_output, "the input set mixes signal alphabets"),
           "maximal: mixed alphabets are warned about");
}

TEST_IN("driver_maximal", test_maximal_rejects_bad_arguments) {
    const TempDir dir("e2e_maximal_args");
    const std::filesystem::path specs = dir.path() / "specs";
    write_text(specs / "original.tlsf", k_unrealizable);

    const DriverRun bare = run_driver("maximal", {});
    expect(bare.m_exit_code == 1 && contains(bare.m_output, "Usage:"),
           "maximal: no inputs is a usage error");

    const DriverRun zero =
        run_driver("maximal", {specs.string(), "--jobs", "0"});
    expect(zero.m_exit_code == 1 &&
               contains(zero.m_output, "--jobs expects a positive integer"),
           "maximal: --jobs 0 is refused");

    const DriverRun no_value =
        run_driver("maximal", {specs.string(), "--timeout"});
    expect(no_value.m_exit_code == 1 &&
               contains(no_value.m_output, "--timeout expects a value"),
           "maximal: --timeout without a value is refused");

    const DriverRun unknown =
        run_driver("maximal", {specs.string(), "--bogus"});
    expect(unknown.m_exit_code == 1 &&
               contains(unknown.m_output, "unknown argument: --bogus"),
           "maximal: an unknown flag is refused by name");
}

TEST_IN("driver_lint_ideals", test_lint_ideals_checks_a_fretish_subject) {
    const TempDir dir("e2e_lint_ideals_fretish");
    const std::filesystem::path subject = dir.path() / "subject";
    write_text(subject / "spec.json", k_fretish_unrealizable);
    write_text(subject / "fixes" / "assumed.json", k_fretish_assumed);
    write_text(subject / "fixes" / "dropped.json", k_fretish_assumed_weaker);
    write_text(subject / "fixes" / "extra.json", k_fretish_extra_guarantee);

    const DriverRun run = run_driver("lint-ideals", {subject.string()});
    expect(
        run.m_exit_code == 1,
        "lint-ideals: a FRETISH subject with an unreachable ideal exits one");
    expect(contains(run.m_output, "subject (3 ideals)"),
           "lint-ideals: the heading names the subject and counts its ideals");
    expect(contains(run.m_output, "no operator adds to the guarantee list"),
           "lint-ideals: an ideal with an extra guarantee is unreachable");
    expect(contains(run.m_output, "1 ideal(s) failed at least one check"),
           "lint-ideals: only the unreachable ideal fails");
    expect(contains(run.m_output,
                    "assumed.json is strictly stronger than dropped.json"),
           "lint-ideals: a sibling's weakening is reported as redundant");
}

TEST_IN("driver_lint_ideals", test_lint_ideals_rejects_bad_subjects) {
    const TempDir dir("e2e_lint_ideals_subjects");

    const DriverRun bare = run_driver("lint-ideals", {});
    expect(bare.m_exit_code == 2 && contains(bare.m_output, "Usage:"),
           "lint-ideals: no subject is a usage error");

    const std::filesystem::path no_fixes = dir.path() / "no_fixes";
    write_text(no_fixes / "spec.json", k_fretish_unrealizable);
    const DriverRun none = run_driver("lint-ideals", {no_fixes.string()});
    expect(
        none.m_exit_code == 0 && contains(none.m_output, "no_fixes: no ideals"),
        "lint-ideals: a subject without ideals passes and says so");

    const std::filesystem::path no_spec = dir.path() / "no_spec";
    std::filesystem::create_directories(no_spec);
    const DriverRun missing = run_driver("lint-ideals", {no_spec.string()});
    expect(missing.m_exit_code == 2 &&
               contains(missing.m_output, "no spec.tlsf or spec.json"),
           "lint-ideals: a directory without a specification is a load error");

    const std::filesystem::path malformed = dir.path() / "malformed";
    write_text(malformed / "spec.json", "{\"guarantees\": [");
    const DriverRun broken = run_driver("lint-ideals", {malformed.string()});
    expect(broken.m_exit_code == 2 &&
               contains(broken.m_output, "JSON parse error"),
           "lint-ideals: a specification that does not parse is a load error");
}

// Registered after each driver's own tests, so each suite checks --version
// last.
TEST_IN("driver_peredur", test_peredur_reports_its_version) {
    expect_reports_version("peredur");
}

TEST_IN("driver_realize", test_realize_reports_its_version) {
    expect_reports_version("realize");
}

TEST_IN("driver_ltl", test_ltl_reports_its_version) {
    expect_reports_version("ltl");
}

TEST_IN("driver_mucs", test_mucs_reports_its_version) {
    expect_reports_version("mucs");
}

TEST_IN("driver_compare", test_compare_reports_its_version) {
    expect_reports_version("compare");
}

TEST_IN("driver_maximal", test_maximal_reports_its_version) {
    expect_reports_version("maximal");
}

TEST_IN("driver_lint_ideals", test_lint_ideals_reports_its_version) {
    expect_reports_version("lint-ideals");
}

// k_fretish with its first guarantee's window shortened, so exactly one slot
// pair differs and its trace and keyword terms both fall below 1.
const char* const k_fretish_retimed = R"({
  "assumptions": [],
  "guarantees": [
    {
      "condition": "true",
      "condition-type": "trigger",
      "response": "takeoff_roll",
      "timing": { "type": "ForTicks", "ticks": 3 }
    },
    {
      "condition": "!takeoff_roll",
      "condition-type": "trigger",
      "response": "lift_off",
      "timing": { "type": "AfterTicks", "ticks": 1 }
    }
  ],
  "in_atoms": [],
  "out_atoms": ["takeoff_roll", "lift_off"]
}
)";

TEST_IN("driver_keyword_terms", test_keyword_terms_prints_the_pair_terms) {
    const TempDir dir("e2e_keyword_terms");
    const std::filesystem::path original =
        write_text(dir.path() / "spec.json", k_fretish);
    const std::filesystem::path retimed =
        write_text(dir.path() / "b_retimed.json", k_fretish_retimed);
    const std::filesystem::path same =
        write_text(dir.path() / "a_same.json", k_fretish);
    const std::filesystem::path pairs = dir.path() / "pairs.tsv";
    const std::filesystem::path specs = dir.path() / "specs.tsv";

    const std::vector<std::string> argv{
        std::string(PEREDUR_DRIVER_DIR) + "/keyword-terms",
        "--original",
        original.string(),
        "--pairs",
        pairs.string(),
        "--specs",
        specs.string(),
        "--bound",
        "5",
        "--jobs",
        "2"};
    const ProcessResult result = execute_and_capture_with_input(
        argv, retimed.string() + "\n" + same.string() + "\n", k_deadline);
    expect(!result.m_timed_out && result.m_exit_code == 0,
           "keyword-terms: scoring two candidates exits zero");

    const std::vector<std::string> pair_lines = tsv_lines(read_text(pairs));
    expect(pair_lines.size() == 2 &&
               pair_lines[0] ==
                   "candidate\tside\tslot\tremoved\ttrace\ttiming_order\t"
                   "scope_order\tcondition_type_order\tkeyword",
           "keyword-terms: one header and one row for the one changed slot");
    expect(pair_lines.size() == 2 &&
               pair_lines[1].rfind(retimed.string() + "\tG\t0\t0\t", 0) == 0,
           "keyword-terms: the row names the candidate, side and slot");

    const std::vector<std::string> spec_lines = tsv_lines(read_text(specs));
    expect(spec_lines.size() == 3 &&
               spec_lines[0] ==
                   "candidate\tn_pairs\tsyntactic_order\tsyntactic_token\t"
                   "restored",
           "keyword-terms: one header and one row per candidate");
    expect(spec_lines.size() == 3 &&
               spec_lines[1] == same.string() + "\t0\t1\t1\t0" &&
               spec_lines[2].rfind(retimed.string() + "\t1\t", 0) == 0,
           "keyword-terms: rows sort by candidate, and an unchanged one "
           "scores 1 on both syntactic measures");

    const DriverRun missing = run_driver(
        "keyword-terms",
        {"--original", original.string(), "--pairs", pairs.string()});
    expect(missing.m_exit_code == 1,
           "keyword-terms: a missing output path is refused");
    const DriverRun bad_metric =
        run_driver("keyword-terms",
                   {"--original", original.string(), "--pairs", pairs.string(),
                    "--specs", specs.string(), "--metric", "cubic"});
    expect(bad_metric.m_exit_code == 1,
           "keyword-terms: an unknown metric is refused");
}

// k_fretish with its first guarantee removed, as the archive writes it: the
// tombstone is dropped rather than kept, so the survivor sits at index 0.
const char* const k_fretish_first_removed = R"({
  "assumptions": [],
  "guarantees": [
    {
      "condition": "!takeoff_roll",
      "condition-type": "trigger",
      "response": "lift_off",
      "timing": { "type": "AfterTicks", "ticks": 1 }
    }
  ],
  "in_atoms": [],
  "out_atoms": ["takeoff_roll", "lift_off"]
}
)";

TEST_IN("driver_keyword_terms",
        test_keyword_terms_restores_a_dropped_tombstone) {
    // Paired by index, the survivor would be compared with guarantee 0.
    // Aligned, it is unchanged in slot 1, so the only row is slot 0 as removed.
    const TempDir dir("e2e_keyword_terms_realign");
    const std::filesystem::path original =
        write_text(dir.path() / "spec.json", k_fretish);
    const std::filesystem::path candidate =
        write_text(dir.path() / "first_removed.json", k_fretish_first_removed);
    const std::filesystem::path pairs = dir.path() / "pairs.tsv";
    const std::filesystem::path specs = dir.path() / "specs.tsv";
    const std::vector<std::string> argv{
        std::string(PEREDUR_DRIVER_DIR) + "/keyword-terms",
        "--original",
        original.string(),
        "--pairs",
        pairs.string(),
        "--specs",
        specs.string(),
        "--bound",
        "5"};
    const ProcessResult result = execute_and_capture_with_input(
        argv, candidate.string() + "\n", k_deadline);
    expect(!result.m_timed_out && result.m_exit_code == 0,
           "keyword-terms: a candidate missing a requirement exits zero");
    const std::vector<std::string> pair_lines = tsv_lines(read_text(pairs));
    expect(pair_lines.size() == 2 &&
               pair_lines[1] == candidate.string() + "\tG\t0\t1\t0\t\t\t\t0",
           "keyword-terms: the removed slot is restored where it was");
    const std::vector<std::string> spec_lines = tsv_lines(read_text(specs));
    expect(spec_lines.size() == 2 &&
               spec_lines[1].substr(spec_lines[1].rfind('\t') + 1) == "1",
           "keyword-terms: one tombstone is reported as restored");
}

TEST_IN("driver_keyword_terms", test_keyword_terms_reports_its_version) {
    expect_reports_version("keyword-terms");
}

}  // namespace
