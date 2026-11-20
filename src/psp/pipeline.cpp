#include "psp/pipeline.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "psp/order_moves.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "repair_file.hpp"
#include "serialisation.hpp"

namespace psp {

std::vector<Scored<Specification>> original_population(
    const Specification& original,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    std::size_t population_size) {
    std::vector<Scored<Specification>> population;
    population.reserve(population_size);
    auto [record, total] = fitness.record_and_fitness(original);
    for (std::size_t i = 0; i < population_size; ++i) {
        Scored<Specification> scored;
        scored.specification = original;
        scored.fitness = total;
        scored.objectives = record.values;
        scored.refused_guarantees = record.refused;
        population.push_back(std::move(scored));
    }
    return population;
}

std::size_t write_repairs(
    const std::vector<Scored<Specification>>& repairs,
    const Specification& original,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const std::string& output_dir) {
    std::size_t n_gutted = 0;
    for (std::size_t i = 0; i < repairs.size(); ++i) {
        const std::string name = "repair_" + std::to_string(i);
        std::string base = output_dir;
        base += "/";
        base += name;
        const Specification& repair = repairs[i].specification;
        std::ofstream spec_file = open_repair_file(base + ".psp");
        spec_file << write_specification(repair);

        const std::vector<std::size_t> gutted =
            gutted_guarantees(original, repair);
        nlohmann::json jobj = repair_fitness_record(repairs[i], fitness);
        jobj["gutted"] = gutted;
        std::ofstream fitness_file = open_repair_file(base + ".fitness.json");
        fitness_file << jobj.dump(2) << "\n";
        if (!gutted.empty()) {
            ++n_gutted;
            std::cerr << "warning: " << name
                      << ".psp empties the scope of guarantee";
            for (const std::size_t slot : gutted) {
                std::cerr << " " << slot;
            }
            std::cerr << ", which then holds vacuously\n";
        }
    }
    GuttingStats::n_gutted_repairs = n_gutted;
    return n_gutted;
}

namespace {

std::vector<std::string> live_texts(const std::vector<Requirement>& reqs) {
    std::vector<std::string> texts;
    for (const Requirement& req : reqs) {
        if (!req.m_removed) {
            texts.push_back(strip_atom_prefix(req).m_ltl);
        }
    }
    return texts;
}

void print_unmatched(const std::string& prefix,
                     const std::vector<std::string>& from,
                     const std::vector<std::string>& against, bool& any) {
    std::vector<bool> used(against.size(), false);
    for (const std::string& text : from) {
        bool matched = false;
        for (std::size_t i = 0; i < against.size() && !matched; ++i) {
            if (!used[i] && against[i] == text) {
                used[i] = true;
                matched = true;
            }
        }
        if (!matched) {
            std::cout << prefix << text << "\n";
            any = true;
        }
    }
}

void print_side_changes(const std::string& name,
                        const std::vector<Requirement>& before,
                        const std::vector<Requirement>& after, bool& any) {
    const std::vector<std::string> old_texts = live_texts(before);
    const std::vector<std::string> new_texts = live_texts(after);
    print_unmatched("  " + name + " - ", old_texts, new_texts, any);
    print_unmatched("  " + name + " + ", new_texts, old_texts, any);
}

}  // namespace

void print_repair_changes(const Specification& original,
                          const std::vector<Scored<Specification>>& repairs) {
    for (std::size_t i = 0; i < repairs.size(); ++i) {
        const Specification& repair = repairs[i].specification;
        std::cout << "\nrepair_" << i << ".psp\n";
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

}  // namespace psp
