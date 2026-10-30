#include "repair_steps.hpp"

#include <cstddef>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "filter/correctness.hpp"
#include "requirement.hpp"
#include "runner/atom_names.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/specification.hpp"

template <typename Spec>
bool screen_original(const Spec& original, const std::string& input_path,
                     const std::vector<std::string>& inputs,
                     const std::vector<std::string>& outputs,
                     const std::function<std::optional<bool>()>& realizability,
                     const Config& cfg) {
    if (const std::optional<std::string> unsafe =
            runner::first_unsafe_atom_name(inputs, outputs)) {
        std::cerr << "fatal: " << input_path << ": " << *unsafe << "\n";
        return false;
    }
    std::cerr << screen_input(
        original,
        correctness_checks<Spec>(global_sat_checker(), global_real_checker()));
    std::cerr << ltlsynt_budget_screen(realizability, cfg.ltlsynt_timeout);
    return true;
}

void announce_seed(std::optional<std::size_t> seed) {
    if (seed.has_value()) {
        std::cout << "Seed: " << *seed << "\n" << std::flush;
    }
}

template bool screen_original(const Specification&, const std::string&,
                              const std::vector<std::string>&,
                              const std::vector<std::string>&,
                              const std::function<std::optional<bool>()>&,
                              const Config&);
template bool screen_original(const tlsf::Specification&, const std::string&,
                              const std::vector<std::string>&,
                              const std::vector<std::string>&,
                              const std::function<std::optional<bool>()>&,
                              const Config&);
