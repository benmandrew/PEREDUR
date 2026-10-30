#include "evolution.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "serialisation.hpp"

std::vector<ScoredSpecification> original_population(
    Specification& original_spec,
    const AggregateWeightedFitnessFunction& fitness_function,
    std::size_t population_size) {
    std::vector<ScoredSpecification> population;
    population.reserve(population_size);
    auto [objectives, fitness] =
        fitness_function.objectives_and_fitness(original_spec);
    for (std::size_t i = 0; i < population_size; ++i) {
        ScoredSpecification scored;
        scored.specification = original_spec;
        scored.fitness = fitness;
        scored.objectives = objectives;
        population.push_back(std::move(scored));
    }
    return population;
}

namespace {

std::vector<Requirement> live_requirements(
    const std::vector<Requirement>& reqs) {
    std::vector<Requirement> live;
    for (const Requirement& req : reqs) {
        if (!req.m_removed) {
            live.push_back(req);
        }
    }
    return live;
}

// Writes a line for each requirement of @p from with no equal partner left in
// @p against. A requirement held twice on one side and once on the other is
// listed once.
void print_unmatched(const std::string& prefix,
                     const std::vector<Requirement>& from,
                     const std::vector<Requirement>& against, bool& any) {
    std::vector<bool> used(against.size(), false);
    for (const Requirement& req : from) {
        bool matched = false;
        for (std::size_t i = 0; i < against.size() && !matched; ++i) {
            if (!used[i] && against[i] == req) {
                used[i] = true;
                matched = true;
            }
        }
        if (!matched) {
            // A requirement with no condition renders with a leading space.
            const std::string text = strip_atom_prefix(req).to_string();
            std::cout << prefix << text.substr(text.find_first_not_of(' '))
                      << "\n";
            any = true;
        }
    }
}

void print_side_changes(const std::string& name,
                        const std::vector<Requirement>& before,
                        const std::vector<Requirement>& after, bool& any) {
    const std::vector<Requirement> old_reqs = live_requirements(before);
    const std::vector<Requirement> new_reqs = live_requirements(after);
    print_unmatched("  " + name + " - ", old_reqs, new_reqs, any);
    print_unmatched("  " + name + " + ", new_reqs, old_reqs, any);
}

}  // namespace

void print_repair_changes(const Specification& original,
                          const std::vector<ScoredSpecification>& scored) {
    for (std::size_t i = 0; i < scored.size(); ++i) {
        const Specification& repair = scored[i].specification;
        std::cout << "\nrepair_" << i << ".json\n";
        bool any = false;
        print_side_changes("ASSUMPTION", original.m_assumptions,
                           repair.m_assumptions, any);
        print_side_changes("GUARANTEE", original.m_guarantees,
                           repair.m_guarantees, any);
        if (!any) {
            std::cout << "  (no change)\n";
        }
    }
}

void write_specifications(
    const std::vector<ScoredSpecification>& scored,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::string& output_dir) {
    for (std::size_t i = 0; i < scored.size(); ++i) {
        const std::string path =
            output_dir + "/repair_" + std::to_string(i) + ".json";
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("cannot open output file: " + path);
        }
        serialisation::FitnessRecord record;
        record.total = scored[i].fitness;
        std::size_t objective = 0;
        for (const WeightedFitnessFunction& wff : fitness_function) {
            record.components.push_back(
                {wff.name, scored[i].objectives[objective++], wff.weight});
        }
        const serialisation::ScoredSpecification ssc{scored[i].specification,
                                                     record};
        nlohmann::json jobj = ssc;
        file << jobj.dump(2) << "\n";
    }
}
