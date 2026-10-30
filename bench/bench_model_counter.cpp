#include <benchmark/benchmark.h>

#include <cstddef>

#include "fitness/model_counter.hpp"
#include "fitness/transfer_matrix.hpp"
#include "requirement.hpp"

namespace {

// Build the TransferSystem once via ltl2tgba (SPOT subprocess), then
// benchmark only the pure-CPU matrix exponentiation inside count_traces.
// step_count is parameterised to show how timing scales with trace length.
static void BenchCountTraces(benchmark::State& state) {
    const std::size_t step_count = static_cast<std::size_t>(state.range(0));
    const Requirement req(Formula("true"), Formula("a & b"),
                          timing::for_ticks(5));
    const TransferSystem system = build_transfer_system_from_ltl(
        requirement_to_ltl(req), count_joint_atoms(req, req));
    for (auto _ : state) {
        benchmark::DoNotOptimize(count_traces(system, step_count));
    }
}
// NOLINTNEXTLINE(cert-err58-cpp)
BENCHMARK(BenchCountTraces)
    ->Name("Trace model counting - matrix exponentiation")
    ->ArgName("steps")
    ->Arg(5)
    ->Arg(10)
    ->Arg(20)
    ->Arg(50);

}  // namespace
