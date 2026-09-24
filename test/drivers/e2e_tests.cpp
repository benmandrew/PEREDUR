#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
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

}  // namespace
