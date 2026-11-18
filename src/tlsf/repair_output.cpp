#include "repair_output.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fitness/function.hpp"
#include "genetic/scored.hpp"
#include "repair_file.hpp"
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
        std::ofstream spec_file = open_repair_file(base + ".tlsf");
        spec_file << write(survivors[i].specification);

        // The same per-objective breakdown the FRETISH repair_N.json carries.
        std::ofstream fitness_file = open_repair_file(base + ".fitness.json");
        const nlohmann::json jobj =
            repair_fitness_record(survivors[i], fitness);
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

}  // namespace tlsf::internal
