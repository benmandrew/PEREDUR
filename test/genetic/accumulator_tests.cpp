// Tests over the cross-generation repair accumulator: that it is inert with
// the config key off, that a repair found in an early generation and lost from
// the final population still reaches the output when the key is on, that
// duplicates collapse, and that each accumulated specification reaches disk as
// it is accumulated rather than at the end of the run.
//
// The gate itself is not exercised here -- it is a `black` and an `ltlsynt`
// sweep, pinned by the `correctness` suite. What these tests stand in for is
// the driver's half: the accumulator sees whatever the gate admitted in each
// generation, and the final collection is the last generation's admissions.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "genetic/accumulator.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "serialisation.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"
#include "tlsf/writer.hpp"

namespace {

constexpr std::string_view k_test_suite = "accumulator";

const char* const k_tlsf_spec =
    "INFO { SEMANTICS: Mealy; }\n"
    "MAIN {\n"
    "  INPUTS { r0; }\n"
    "  OUTPUTS { g0; }\n"
    "  GUARANTEE {\n"
    "    G (g0 -> r0);\n"
    "    G F g0;\n"
    "  }\n"
    "}\n";

Specification make_spec(const std::string& response) {
    return Specification(
        {},
        {Requirement{Formula("a"), Formula(response), timing::immediately()}},
        {"a", "b"}, {"x", "y"});
}

bool holds(const std::vector<Specification>& specs, const Specification& spec) {
    return std::any_of(
        specs.begin(), specs.end(),
        [&spec](const Specification& candidate) { return candidate == spec; });
}

// The FRETISH writer as the driver builds it: the serialiser repair_N.json
// goes through, so a tombstoned guarantee is absent rather than flagged.
std::filesystem::path accumulated_dir(const TempDir& dir) {
    return dir.path() / "accumulated";
}

AccumulatedRepairWriter<Specification> json_writer(const TempDir& dir) {
    return {dir.string(), ".json", [](const Specification& spec) {
                const nlohmann::json jobj = spec;
                return jobj.dump(2) + "\n";
            }};
}

// The specification files alone. index.tsv sits in the same directory and is
// not one of them, so counting it here would make every count below read one
// too many; it has its own tests.
std::vector<std::filesystem::path> written_files(const TempDir& dir) {
    std::vector<std::filesystem::path> paths;
    if (!std::filesystem::exists(accumulated_dir(dir))) {
        return paths;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(accumulated_dir(dir))) {
        if (entry.path().filename() == "index.tsv") {
            continue;
        }
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::vector<std::string> index_rows(const TempDir& dir) {
    std::vector<std::string> rows;
    std::ifstream file(accumulated_dir(dir) / "index.tsv");
    for (std::string line; std::getline(file, line);) {
        rows.push_back(line);
    }
    return rows;
}

TEST(test_disabled_accumulator_keeps_nothing) {
    RepairAccumulator<Specification> accumulator(false);
    expect(!accumulator.enabled(), "accumulator: constructed disabled");
    accumulator.insert(make_spec("x"), 1);
    accumulator.insert(make_spec("y"), 2);
    expect(accumulator.specifications().empty(),
           "accumulator: a disabled instance drops every insertion");
}

// With the key off the output is what the final population's own collection
// returned, unchanged and in its own order.
TEST(test_disabled_accumulator_leaves_the_output_alone) {
    const RepairAccumulator<Specification> accumulator(false);
    std::vector<Specification> collected = {make_spec("x"), make_spec("y")};
    const std::size_t added =
        merge_accumulated(collected, accumulator.specifications());
    expect(added == 0, "accumulator: nothing merged with the key off");
    expect(collected.size() == 2 && collected[0] == make_spec("x") &&
               collected[1] == make_spec("y"),
           "accumulator: the collection is untouched with the key off");
}

// The case the accumulator exists for: gate-passing in an early generation,
// gone by the last one.
TEST(test_early_repair_survives_its_generation) {
    RepairAccumulator<Specification> accumulator(true);
    const Specification early = make_spec("x");
    const Specification late = make_spec("y");
    accumulator.insert(early, 1);
    accumulator.insert(late, 4);
    // The final population held only `late`, so `early` reaches the output
    // through the accumulator or not at all.
    std::vector<Specification> collected = {late};
    const std::size_t added =
        merge_accumulated(collected, accumulator.specifications());
    expect(added == 1, "accumulator: exactly the lost repair is merged");
    expect(holds(collected, early),
           "accumulator: a repair lost from the final population is emitted");
    expect(collected.size() == 2,
           "accumulator: a repair already collected is not emitted twice");
}

TEST(test_repeated_insertions_collapse) {
    RepairAccumulator<Specification> accumulator(true);
    // The same specification survives many generations, so the gate admits it
    // once per generation; the accumulator is a set, not a log.
    for (std::size_t gen = 0; gen < 5; ++gen) {
        accumulator.insert(make_spec("x"), gen + 1);
    }
    accumulator.insert(make_spec("y"), 5);
    expect(accumulator.specifications().size() == 2,
           "accumulator: repeated insertions of one specification collapse");
}

TEST(test_first_seen_order_is_kept) {
    RepairAccumulator<Specification> accumulator(true);
    accumulator.insert(make_spec("y"), 1);
    accumulator.insert(make_spec("x"), 2);
    accumulator.insert(make_spec("y"), 3);
    const std::vector<Specification>& kept = accumulator.specifications();
    expect(kept.size() == 2 && kept[0] == make_spec("y") &&
               kept[1] == make_spec("x"),
           "accumulator: order is first-seen, not the hash set's");
}

// A tombstoned guarantee is part of a specification's identity, so a candidate
// that deleted one must not collapse onto the candidate it was derived from.
TEST(test_a_removed_guarantee_is_a_distinct_repair) {
    Specification removed = make_spec("x");
    removed.m_guarantees[0].m_removed = true;
    RepairAccumulator<Specification> accumulator(true);
    accumulator.insert(make_spec("x"), 1);
    accumulator.insert(removed, 2);
    expect(accumulator.specifications().size() == 2,
           "accumulator: a deleted guarantee makes a distinct specification");
}

TEST(test_merge_reports_only_what_it_added) {
    std::vector<Specification> collected = {make_spec("x")};
    const std::vector<Specification> accumulated = {
        make_spec("x"), make_spec("y"), make_spec("y")};
    const std::size_t added = merge_accumulated(collected, accumulated);
    expect(added == 1,
           "accumulator: the merge counts distinct additions, not candidates");
    expect(collected.size() == 2, "accumulator: the union is deduplicated");
}

// The key off must cost nothing on disk either: no directory, so nothing for a
// campaign's output tree to grow that its configs never asked for.
TEST(test_nothing_is_written_with_the_key_off) {
    const TempDir dir("accumulator_tests");
    RepairAccumulator<Specification> accumulator(false, json_writer(dir));
    accumulator.insert(make_spec("x"), 1);
    accumulator.insert(make_spec("y"), 2);
    expect(!std::filesystem::exists(accumulated_dir(dir)),
           "accumulator: the key off creates no accumulated directory");
}

// The point of writing as we go: a run killed mid-search keeps everything it
// had already accumulated, so each specification is a closed file before the
// next one is inserted.
TEST(test_one_file_per_accumulated_specification) {
    const TempDir dir("accumulator_tests");
    RepairAccumulator<Specification> accumulator(true, json_writer(dir));
    accumulator.insert(make_spec("x"), 1);
    expect(written_files(dir).size() == 1,
           "accumulator: the first insertion is on disk before the second");
    accumulator.insert(make_spec("y"), 3);
    // Already accumulated, so not a new repair and not a new file.
    accumulator.insert(make_spec("x"), 4);
    const std::vector<std::filesystem::path> paths = written_files(dir);
    expect(paths.size() == 2,
           "accumulator: one file per distinct accumulated specification");
    expect(
        paths[0].filename().string() == "gen01_0000.json" &&
            paths[1].filename().string() == "gen03_0001.json",
        "accumulator: file names carry the generation and a unique sequence");
}

TEST(test_written_files_parse_back_to_what_was_accumulated) {
    const TempDir dir("accumulator_tests");
    RepairAccumulator<Specification> accumulator(true, json_writer(dir));
    accumulator.insert(make_spec("x"), 1);
    accumulator.insert(make_spec("y"), 2);
    const std::vector<std::filesystem::path> paths = written_files(dir);
    expect(paths.size() == 2, "accumulator: both specifications were written");
    std::vector<Specification> parsed;
    parsed.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        parsed.push_back(
            nlohmann::json::parse(read_text(path)).get<Specification>());
    }
    expect(holds(parsed, make_spec("x")) && holds(parsed, make_spec("y")),
           "accumulator: each file parses back to the specification written");
}

// The index is what makes a run's accumulation legible on a time axis: every
// "over time" metric reads it rather than the file names, which carry the
// generation but no clock.
TEST(test_the_index_carries_a_row_per_specification) {
    const TempDir dir("accumulator_tests");
    RepairAccumulator<Specification> accumulator(true, json_writer(dir));
    accumulator.insert(make_spec("x"), 1);
    accumulator.insert(make_spec("y"), 3);
    // Already accumulated, so neither a new file nor a new row.
    accumulator.insert(make_spec("x"), 4);

    const std::vector<std::string> rows = index_rows(dir);
    expect(rows.size() == 3,
           "accumulator: the index holds a header and one row per "
           "specification, found " +
               std::to_string(rows.size()));
    expect(rows[0] == "file\tgeneration\telapsed_s",
           "accumulator: the index leads with a header, once");
    expect(rows[1].rfind("gen01_0000.json\t1\t", 0) == 0,
           "accumulator: a row names its file and generation, got " + rows[1]);
    expect(rows[2].rfind("gen03_0001.json\t3\t", 0) == 0,
           "accumulator: rows follow accumulation order, got " + rows[2]);
}

// A writer given no clock still writes its files; the column reads zero rather
// than the row being absent, so a reader never has to handle a ragged index.
TEST(test_the_index_reports_zero_without_a_clock) {
    const TempDir dir("accumulator_tests");
    RepairAccumulator<Specification> accumulator(true, json_writer(dir));
    accumulator.insert(make_spec("x"), 1);
    const std::vector<std::string> rows = index_rows(dir);
    expect(rows.size() == 2 && rows[1] == "gen01_0000.json\t1\t0.000000",
           "accumulator: a clockless writer stamps zero, got " +
               (rows.size() > 1 ? rows[1] : std::string("no row")));
}

// The clock reaches the index, and rows carry it in the order they were
// written. Whether the seconds are plausible is the driver's business; what
// this pins is that the writer asks the clock at all.
TEST(test_the_index_records_the_clock_it_was_given) {
    const TempDir dir("accumulator_tests");
    double now = 0.0;
    AccumulatedRepairWriter<Specification> writer(
        dir.string(), ".json",
        [](const Specification& spec) {
            const nlohmann::json jobj = spec;
            return jobj.dump(2) + "\n";
        },
        [&now] { return now; });
    RepairAccumulator<Specification> accumulator(true, std::move(writer));
    now = 1.5;
    accumulator.insert(make_spec("x"), 1);
    now = 4.25;
    accumulator.insert(make_spec("y"), 2);

    const std::vector<std::string> rows = index_rows(dir);
    expect(rows.size() == 3, "accumulator: two rows and a header");
    expect(rows[1] == "gen01_0000.json\t1\t1.500000",
           "accumulator: the first row carries its own elapsed time, got " +
               rows[1]);
    expect(rows[2] == "gen02_0001.json\t2\t4.250000",
           "accumulator: the second row carries its own, got " + rows[2]);
}

// The other path's serialiser, on the same writer. tlsf::write is the boundary
// where a specification becomes a document, so a tombstoned guarantee is absent
// from the file and the text parses back without it.
TEST(test_the_tlsf_serialiser_round_trips) {
    const TempDir dir("accumulator_tests");
    const tlsf::Specification spec = tlsf::parse(k_tlsf_spec);
    AccumulatedRepairWriter<tlsf::Specification> writer(
        dir.string(), ".tlsf", [](const tlsf::Specification& candidate) {
            return tlsf::write(candidate);
        });
    writer.write(2, spec);
    const std::vector<std::filesystem::path> paths = written_files(dir);
    expect(
        paths.size() == 1 && paths[0].filename().string() == "gen02_0000.tlsf",
        "accumulator: the TLSF writer names its file the same way");
    expect(tlsf::parse(read_text(paths[0])) == spec,
           "accumulator: a TLSF file parses back to the specification written");
}

}  // namespace
