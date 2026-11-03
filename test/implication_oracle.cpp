// Prints every ordered pair of guarantees that requirement_implies proves, for
// scripts/field_orders.py to check against SPOT. A development tool, not a
// driver: it is built with the tests and installs nowhere.

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "filter/implication_check.hpp"
#include "requirement.hpp"
#include "serialisation.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <spec.json> [<shard> <shards>]\n"
              << "\n"
              << "Prints `i j` for each pair of guarantees i != j where\n"
              << "requirement_implies proves guarantee i implies guarantee j.\n"
              << "With a shard, only rows i where i % shards == shard.\n";
}

}  // namespace

int main(int argc, const char* const argv[]) {
    if (argc != 2 && argc != 4) {
        print_usage(argc > 0 ? argv[0] : "implication_oracle");
        return 1;
    }
    try {
        const Specification spec = load_specification(argv[1]);
        const std::size_t shard = argc == 4 ? std::stoul(argv[2]) : 0;
        const std::size_t shards = argc == 4 ? std::stoul(argv[3]) : 1;
        if (shards == 0 || shard >= shards) {
            print_usage(argv[0]);
            return 1;
        }
        const std::vector<Requirement>& reqs = spec.m_guarantees;
        for (std::size_t i = shard; i < reqs.size(); i += shards) {
            for (std::size_t j = 0; j < reqs.size(); ++j) {
                if (i != j && requirement_implies(reqs[i], reqs[j])) {
                    std::cout << i << ' ' << j << '\n';
                }
            }
        }
    } catch (const std::exception& exc) {
        std::cerr << argv[1] << ": " << exc.what() << "\n";
        return 1;
    }
    return 0;
}
