// Tests over the progress log the live dashboard reads: that every record is
// one parseable JSON object, that the optional fields are omitted rather than
// defaulted when a driver does not measure them, and that the per-objective
// means are labelled by name.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "dashboard.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "dashboard";

// A directory unique to this suite, removed on scope exit so a failing test
// cannot leave the next run reading a stale log.
class TempDir {
   public:
    TempDir()
        : m_path(std::filesystem::temp_directory_path() /
                 "peredur_dashboard_tests") {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~TempDir() { std::filesystem::remove_all(m_path); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] std::string string() const { return m_path.string(); }

   private:
    std::filesystem::path m_path;
};

std::vector<nlohmann::json> read_records(const std::string& path) {
    std::vector<nlohmann::json> records;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        records.push_back(nlohmann::json::parse(line));
    }
    return records;
}

StageObservation observation(const std::string& name, std::size_t n_in,
                             std::size_t n_out, std::size_t distinct = 0) {
    StageObservation obs;
    obs.name = name;
    obs.n_in = n_in;
    obs.n_out = n_out;
    obs.distinct = distinct;
    obs.elapsed_s = 0.5;
    return obs;
}

TEST(test_records_are_one_json_object_per_line) {
    const TempDir dir;
    {
        DashboardWriter writer(dir.string(), true);
        expect(writer.enabled(),
               "dashboard: the writer should open its log in a directory that "
               "exists");
        writer.run_start("spec.json", 2, 10, 7, {"syntactic", "semantic"},
                         {"breed", "dedup", "score"});
        writer.stage(1, 0, observation("breed", 10, 8, 5));
        writer.generation(1, 1.5, 0.9, 0.5, {{"syntactic", 0.8}}, 3, 10);
        writer.run_end(2, 4, 2, 3.0);
    }
    const std::vector<nlohmann::json> records =
        read_records(dir.string() + "/progress.jsonl");
    expect(records.size() == 4,
           "dashboard: one line should be written per record");
    expect(records[0]["type"] == "run_start" && records[1]["type"] == "stage" &&
               records[2]["type"] == "generation" &&
               records[3]["type"] == "run_end",
           "dashboard: records should carry their type, in write order");
    expect(records[0]["objectives"].size() == 2,
           "dashboard: run_start should list the objective names so the page "
           "need not know them");
    expect(records[0]["stages"].size() == 3,
           "dashboard: run_start should declare every stage the run can "
           "execute, so the page can lay out the full set before the first "
           "generation reports");
    expect(records[1]["name"] == "breed" && records[1]["n_in"] == 10 &&
               records[1]["n_out"] == 8,
           "dashboard: a stage record should carry its name and both "
           "population sizes");
    expect(records[1]["distinct"] == 5,
           "dashboard: a stage record should carry how many of its survivors "
           "are distinct, which is what the population sizes cannot show");
}

TEST(test_unmeasured_fields_are_omitted_not_defaulted) {
    const TempDir dir;
    {
        DashboardWriter writer(dir.string(), true);
        // The TLSF driver counts realizable survivors only at the end of a run,
        // so a zero here would be a measurement the run never made.
        writer.generation(1, 1.0, 0.5, 0.4, {}, std::nullopt, 10);
        writer.generation(2, 1.0, 0.5, 0.4, {}, 0, 10);
        writer.stage(1, 0, observation("breed", 10, 8));
        writer.stage(1, 0, observation("breed", 10, 8), 3);
    }
    const std::vector<nlohmann::json> records =
        read_records(dir.string() + "/progress.jsonl");
    expect(!records[0].contains("n_realizable"),
           "dashboard: an unmeasured realizable count should be absent, so the "
           "page can tell 'not measured' from 'none found'");
    expect(
        records[1].contains("n_realizable") && records[1]["n_realizable"] == 0,
        "dashboard: a measured count of zero should still be reported");
    expect(!records[2].contains("muc_iter"),
           "dashboard: a non-MUC run should not carry a MUC iteration");
    expect(records[3]["muc_iter"] == 3,
           "dashboard: a MUC-guided run should record which core a generation "
           "belonged to");
}

TEST(test_writer_survives_an_unwritable_directory) {
    DashboardWriter writer("/nonexistent-directory-for-peredur-tests", true);
    expect(!writer.enabled(),
           "dashboard: a writer that cannot open its log should report itself "
           "disabled");
    // Every call must still be safe: losing the progress log must never take
    // the repair run with it.
    writer.run_start("spec.json", 1, 1, 1, {}, {});
    writer.stage(1, 0, observation("breed", 1, 1));
    writer.generation(1, 1.0, 0.5, 0.5, {}, 1, 1);
    writer.run_end(1, 0, 0, 1.0);
    expect(writer.write_page().empty(),
           "dashboard: a disabled writer should not claim to have written a "
           "page");
}

TEST(test_mean_objectives_labels_and_averages) {
    const std::vector<std::string> names = {"syntactic", "semantic"};
    const std::vector<std::vector<double>> population = {
        {1.0, 0.0}, {0.0, 1.0}, {0.5, 0.5}};
    const std::vector<std::pair<std::string, double>> means =
        mean_objectives(names, population);
    expect(means.size() == 2,
           "mean_objectives: one entry per registered objective");
    expect(means[0].first == "syntactic" && means[1].first == "semantic",
           "mean_objectives: entries should keep registration order");
    expect(means[0].second == 0.5 && means[1].second == 0.5,
           "mean_objectives: each objective should average over the "
           "population");
}

TEST(test_mean_objectives_handles_ragged_and_empty_input) {
    const std::vector<std::string> names = {"a", "b", "c"};
    // A dropped individual can leave a shorter objective vector; the missing
    // tail must not be counted as a zero.
    const std::vector<std::vector<double>> ragged = {{1.0, 1.0}, {1.0}};
    const std::vector<std::pair<std::string, double>> means =
        mean_objectives(names, ragged);
    expect(means.size() == 3,
           "mean_objectives: should still report every named objective");
    expect(means[0].second == 1.0 && means[1].second == 1.0,
           "mean_objectives: a short vector should not drag an average toward "
           "zero");
    expect(means[2].second == 0.0,
           "mean_objectives: an objective no individual reported averages to "
           "zero");
    expect(mean_objectives({}, {}).empty(),
           "mean_objectives: no objectives yields no entries");
    expect(mean_objectives(names, {}).size() == 3,
           "mean_objectives: an empty population still yields one entry per "
           "objective");
}

}  // namespace
