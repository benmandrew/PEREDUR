#pragma once

#include <string_view>
#include <vector>

/// One registered test: the suite it belongs to and the function to call.
struct TestCase {
    std::string_view m_suite;
    std::string_view m_name;
    void (*m_run)() = nullptr;
};

/// Every registered test, in registration order. Within one translation unit
/// that is definition order, which the standard fixes for dynamic
/// initialisation; across units it is unspecified, so callers only ever
/// select by suite, and each suite lives in one unit.
std::vector<TestCase>& test_registry();

/// Appends one test to the registry during static initialisation.
struct TestRegistrar {
    TestRegistrar(std::string_view suite, std::string_view name, void (*run)());
};

/// Defines and registers `name` under an explicit suite.
#define TEST_IN(suite, name)                                    \
    void name();                                                \
    const TestRegistrar name##_registrar{(suite), #name, name}; \
    void name()

/// Defines and registers `name` under the unit's `k_test_suite`.
#define TEST(name) TEST_IN(k_test_suite, name)
