#include "repair_output.hpp"

#include <atomic>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "filter/implication.hpp"
#include "fitness/function.hpp"
#include "genetic/scored.hpp"
#include "serialisation.hpp"
#include "tlsf/specification.hpp"
#include "tlsf/writer.hpp"

namespace tlsf::internal {

void write_survivors(
    const std::vector<Scored<Specification>>& survivors,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const std::string& output_dir) {
    for (std::size_t i = 0; i < survivors.size(); ++i) {
        const std::string base = output_dir + "/repair_" + std::to_string(i);
        std::ofstream spec_file(base + ".tlsf");
        if (!spec_file) {
            throw std::runtime_error("cannot open output file: " + base +
                                     ".tlsf");
        }
        spec_file << write(survivors[i].specification);

        std::ofstream fitness_file(base + ".fitness.json");
        if (!fitness_file) {
            throw std::runtime_error("cannot open output file: " + base +
                                     ".fitness.json");
        }
        // Mirror the FRETISH per-objective breakdown: the weighted total plus
        // each component's score and weight, in registration order.
        serialisation::FitnessRecord record;
        record.total = survivors[i].fitness;
        for (const WeightedFitnessFunctionT<Specification>& wff : fitness) {
            record.components.push_back(
                {wff.name, wff.function(survivors[i].specification),
                 wff.weight});
        }
        const nlohmann::json jobj = record;
        fitness_file << jobj.dump(2) << "\n";
    }
}

void print_repair_changes(const Specification& original,
                          const std::vector<Scored<Specification>>& survivors) {
    for (std::size_t i = 0; i < survivors.size(); ++i) {
        const std::string changes =
            write_changes(original, survivors[i].specification);
        std::cout << "\nrepair_" << i << ".tlsf\n"
                  << (changes.empty() ? "  (no change)\n" : changes);
    }
}

void print_repair_summary(std::size_t n_realizable, std::size_t n_written,
                          bool implication_filter_run,
                          const std::string& output_dir) {
    std::cout << "Realizable specifications: " << n_realizable;
    if (implication_filter_run) {
        std::cout << " (" << n_written << " maximal";
        // The sweep short-circuits, so a class of k members produces exactly
        // k-1 collapse events: this is the number of repairs that were the
        // same repair written another way, which no other figure reports.
        const std::size_t collapsed =
            ImplicationFilterStats::n_equivalent_collapsed.load(
                std::memory_order_relaxed);
        if (collapsed > 0) {
            std::cout << ", " << collapsed << " equivalent";
        }
        std::cout << ")";
    }
    if (n_written > 0) {
        std::cout << ", written to " << output_dir << "/";
    }
    std::cout << "\n";
}

}  // namespace tlsf::internal
