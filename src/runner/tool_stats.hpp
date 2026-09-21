#pragma once

#include "runner/process.hpp"

// Folds one exec's wall and child-CPU time into a runner's totals, counting it
// as a timeout when it was killed at its deadline. `Stats` is any class with
// static total_time_s, total_cpu_s and n_timeouts members; the caller holds
// whatever lock guards them.
template <typename Stats>
void record_exec(const ProcessResult& result) {
    Stats::total_time_s += result.m_wall_s;
    Stats::total_cpu_s += result.m_cpu_s;
    if (result.m_timed_out) {
        ++Stats::n_timeouts;
    }
}
