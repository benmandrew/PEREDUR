#include "dashboard.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "runner/tool_paths.hpp"

namespace {

constexpr const char* k_progress_filename = "progress.jsonl";
constexpr const char* k_page_filename = "index.html";

std::string page_source_path() {
#ifdef PEREDUR_DASHBOARD_PAGE_PATH
    static const ToolPath k_page = tool_path_from_env(
        "PEREDUR_DASHBOARD_PAGE", PEREDUR_DASHBOARD_PAGE_PATH);
    return k_page.m_path;
#else
    // A build with no page path writes no page, and an override cannot
    // conjure one: the contract dashboard.hpp documents is that the page is
    // a property of the build.
    return "";
#endif
}

}  // namespace

DashboardWriter::DashboardWriter(const std::string& dir, bool enabled) {
    if (!enabled) {
        return;
    }
    const std::filesystem::path path =
        std::filesystem::path(dir) / k_progress_filename;
    m_path = path.string();
    m_out.open(m_path, std::ios::out | std::ios::trunc);
    if (!m_out) {
        std::cerr << "warning: could not open " << m_path
                  << " for progress output; the run continues without it\n";
        return;
    }
    m_enabled = true;
}

std::string DashboardWriter::write_page() {
    if (!m_enabled) {
        return {};
    }
    const std::string source = page_source_path();
    if (source.empty()) {
        return {};
    }
    const std::filesystem::path destination =
        std::filesystem::path(m_path).parent_path() / k_page_filename;
    std::error_code error;
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        std::cerr << "warning: could not copy the dashboard page from "
                  << source << ": " << error.message() << "\n";
        return {};
    }
    return destination.string();
}

void DashboardWriter::write_line(const std::string& line) {
    if (!m_enabled) {
        return;
    }
    m_out << line << "\n" << std::flush;
}

void DashboardWriter::run_start(const std::string& input,
                                std::size_t generations, std::size_t population,
                                std::size_t seed,
                                const std::vector<std::string>& objectives,
                                const std::vector<std::string>& stages) {
    nlohmann::json record{{"type", "run_start"},
                          {"input", input},
                          {"generations", generations},
                          {"population", population},
                          {"seed", seed},
                          {"objectives", objectives},
                          {"stages", stages}};
    write_line(record.dump());
}

void DashboardWriter::stage(std::size_t gen, std::size_t index,
                            const StageObservation& observation,
                            std::size_t muc_iter) {
    nlohmann::json record{{"type", "stage"},
                          {"gen", gen},
                          {"i", index},
                          {"name", observation.name},
                          {"n_in", observation.n_in},
                          {"n_out", observation.n_out},
                          {"distinct", observation.distinct},
                          {"elapsed_s", observation.elapsed_s}};
    if (muc_iter > 0) {
        record["muc_iter"] = muc_iter;
    }
    write_line(record.dump());
}

void DashboardWriter::generation(
    std::size_t gen, double elapsed_s, double best_fitness, double mean_fitness,
    const std::vector<std::pair<std::string, double>>& objectives,
    std::optional<std::size_t> n_realizable, std::size_t population,
    std::size_t muc_iter) {
    nlohmann::json means = nlohmann::json::object();
    for (const auto& objective : objectives) {
        means[objective.first] = objective.second;
    }
    nlohmann::json record{
        {"type", "generation"},         {"gen", gen},
        {"elapsed_s", elapsed_s},       {"best_fitness", best_fitness},
        {"mean_fitness", mean_fitness}, {"objectives", std::move(means)},
        {"population", population}};
    if (n_realizable.has_value()) {
        record["n_realizable"] = *n_realizable;
    }
    if (muc_iter > 0) {
        record["muc_iter"] = muc_iter;
    }
    write_line(record.dump());
}

void DashboardWriter::run_end(std::size_t generations_run,
                              std::size_t n_realizable, std::size_t n_maximal,
                              double elapsed_s) {
    nlohmann::json record{{"type", "run_end"},
                          {"generations_run", generations_run},
                          {"n_realizable", n_realizable},
                          {"n_maximal", n_maximal},
                          {"elapsed_s", elapsed_s}};
    write_line(record.dump());
}

std::vector<std::pair<std::string, double>> mean_objectives(
    const std::vector<std::string>& names,
    const std::vector<std::vector<double>>& population_objectives) {
    std::vector<std::pair<std::string, double>> means;
    means.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        double total = 0.0;
        std::size_t counted = 0;
        for (const std::vector<double>& objectives : population_objectives) {
            if (i < objectives.size()) {
                total += objectives[i];
                ++counted;
            }
        }
        means.emplace_back(
            names[i],
            counted == 0 ? 0.0 : total / static_cast<double>(counted));
    }
    return means;
}
