#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "test_support.hpp"

/// One query `m_lhs -> m_rhs` for ltl_implications.
struct Implication {
    std::string m_lhs;
    std::string m_rhs;
};

/// Decides every `lhs -> rhs` in one ltlfilt run. One exec per query costs
/// 9.6 ms on the debug build, and the order tables ask over a thousand. An
/// implication holds exactly when `lhs & !rhs` is unsatisfiable, and `%L`
/// prints the input line of each satisfiable one. An undecided batch fails the
/// test.
inline std::vector<bool> ltl_implications(
    const std::vector<Implication>& queries) {
    std::string input;
    for (const Implication& query : queries) {
        input += "(" + query.m_lhs + ") & !(" + query.m_rhs + ")\n";
    }
    const ProcessResult result = execute_and_capture_with_input(
        {ltlfilt_path(), "--satisfiable", "--format=%L", "-F", "-"}, input,
        std::chrono::milliseconds{60'000});
    // ltlfilt exits 0 when some line matched and 1 when none did.
    expect(result.m_exit_code == 0 || result.m_exit_code == 1,
           "ltl_implications: ltlfilt could not decide the batch (exit " +
               std::to_string(result.m_exit_code) + "): " + result.m_output);
    std::vector<bool> holds(queries.size(), true);
    std::istringstream lines(result.m_output);
    std::string line;
    while (std::getline(lines, line)) {
        const bool numeric =
            !line.empty() &&
            std::all_of(line.begin(), line.end(), [](char character) {
                return character >= '0' && character <= '9';
            });
        expect(numeric,
               "ltl_implications: stray ltlfilt output `" + line + "`");
        const std::size_t number = std::stoul(line);
        expect(number >= 1 && number <= queries.size(),
               "ltl_implications: ltlfilt named line " + line + " of " +
                   std::to_string(queries.size()));
        holds[number - 1] = false;
    }
    return holds;
}
