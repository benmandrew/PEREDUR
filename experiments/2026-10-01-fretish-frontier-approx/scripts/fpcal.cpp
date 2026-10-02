// Scratch calibration tool: fingerprint FRETISH repairs over one word set.
// usage: fpcal <words> <max_prefix> <max_cycle> <seed> <spec.json> <list>
// <list> holds one repair path a line. The word set is drawn over the union of
// the original spec's signals and every listed repair's signals, so a call is
// only comparable with another call over the same list and spec.
// Prints "<path>\t<hex>" where hex is the packed words, low word first, each
// as 16 hex digits.
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "fingerprint/lasso.hpp"
#include "requirement.hpp"
#include "serialisation.hpp"

namespace {

void add_signals(const Specification& spec, std::vector<std::string>& out) {
    for (const std::string& signal : environment_signals(spec)) {
        out.push_back(signal);
    }
    for (const std::string& signal : spec.m_out_atoms) {
        out.push_back(signal);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        std::cerr << "usage: fpcal words max_prefix max_cycle seed spec list\n";
        return 2;
    }
    const std::size_t n_words = std::stoul(argv[1]);
    const std::size_t max_prefix = std::stoul(argv[2]);
    const std::size_t max_cycle = std::stoul(argv[3]);
    const std::uint64_t seed = std::stoull(argv[4]);
    std::vector<std::string> signals;
    add_signals(load_scored_specification(argv[5]).spec,
                signals);
    std::vector<std::string> paths;
    std::vector<Specification> specs;
    std::ifstream list(argv[6]);
    for (std::string line; std::getline(list, line);) {
        if (line.empty()) {
            continue;
        }
        Specification spec = load_scored_specification(line).spec;
        add_signals(spec, signals);
        paths.push_back(line);
        specs.push_back(std::move(spec));
    }
    const std::vector<fingerprint::LassoWord> words = fingerprint::sample_words(
        signals, n_words, seed, max_prefix, max_cycle);
    int failures = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        try {
            const fingerprint::PackedFingerprint packed = fingerprint::pack(
                fingerprint::fingerprint_of(specs[i].to_ltl(), words));
            std::string hex;
            hex.reserve(packed.size() * 16);
            char buffer[17];
            for (const std::uint64_t word : packed) {
                std::snprintf(buffer, sizeof buffer, "%016llx",
                              static_cast<unsigned long long>(word));
                hex += buffer;
            }
            std::cout << paths[i] << '\t' << hex << '\n';
        } catch (const std::exception& error) {
            std::cerr << paths[i] << ": " << error.what() << '\n';
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
