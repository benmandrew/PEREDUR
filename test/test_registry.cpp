#include "test_registry.hpp"

#include <string_view>
#include <vector>

std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

TestRegistrar::TestRegistrar(std::string_view suite, std::string_view name,
                             void (*run)()) {
    test_registry().push_back(TestCase{suite, name, run});
}
