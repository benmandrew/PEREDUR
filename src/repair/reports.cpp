#include "reports.hpp"

#include <sys/resource.h>

#include <cstddef>
#include <iomanip>
#include <iostream>

#include "filter/well_separation.hpp"
#include "fitness/function.hpp"
#include "fitness/semantic_similarity.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/generation.hpp"
#include "runner/black.hpp"
#include "runner/ganak.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/spot.hpp"

// Silent when nothing was dropped, so a clean run's output is unchanged and
// any drop at all stands out.
void print_scoring_report() {
    if (ScoringStats::n_dropped == 0) {
        return;
    }
    std::cout << "\nScoring report:\n"
              << ScoringStats::n_dropped
              << " individual(s) dropped after a fitness function threw. These "
                 "were excluded from their generation rather than scored.\n";
    for (const auto& [reason, count] : ScoringStats::reasons) {
        std::cout << "  " << std::setw(6) << count << "x  " << reason << "\n";
    }
    if (ScoringStats::n_reasons_elided > 0) {
        std::cout << "  (" << ScoringStats::n_reasons_elided
                  << " further failure(s) with other messages not listed)\n";
    }
}

// Every memo in the run, with the hit rate each one actually reached. The
// per-tool rows above cannot carry this: several caches sit in front of one
// tool, and two of them (the satisfiability cache and the fitness cache) sit
// in front of no single tool at all.
void print_cache_report() {
    auto print_row = [](const char* name, std::size_t hits,
                        std::size_t misses) {
        const std::size_t lookups = hits + misses;
        const double rate = lookups > 0 ? 100.0 * static_cast<double>(hits) /
                                              static_cast<double>(lookups)
                                        : 0.0;
        std::cout << std::left << std::setw(18) << name << std::right
                  << std::setw(9) << lookups << " lookups  " << std::setw(8)
                  << misses << " misses  " << std::fixed << std::setprecision(1)
                  << std::setw(5) << rate << "% hit\n";
    };
    std::cout << "\nCache report:\n";
    print_row("fitness", FitnessCacheStats::n_hits,
              FitnessCacheStats::n_misses);
    print_row("satisfiability", SatisfiabilityChecker::n_cache_hits,
              SatisfiabilityChecker::n_cache_misses);
    print_row("realizability", RealizabilityChecker::n_cache_hits,
              RealizabilityChecker::n_cache_misses);
    print_row("count_traces", CountTracesStats::n_hits,
              CountTracesStats::n_misses);
    print_row("ltl2tgba", Ltl2tgbaStats::n_cache_hits,
              Ltl2tgbaStats::n_cache_misses);
    print_row("ganak", GanakStats::n_cache_hits, GanakStats::n_cache_misses);
    print_row("simplify_ltl", LtlfiltStats::n_cache_hits,
              LtlfiltStats::n_cache_misses);
    print_row("remove_wm", LtlfiltStats::n_remove_wm_hits,
              LtlfiltStats::n_remove_wm_execs);
    print_row("spot_satisfiable", 0, LtlfiltStats::n_satisfiable_execs);
    if (RealizabilityChecker::n_subsumed > 0) {
        std::cout << "realizability queries answered by monotone subsumption "
                     "rather than by an exec: "
                  << RealizabilityChecker::n_subsumed << "\n";
    }
}

void print_diagnostics_report() {
    // timeouts has no default argument on purpose: it used to, and three of
    // the five rows quietly took it, so the counts they were keeping never
    // reached the report. Every tool has the counter; make every caller say so.
    auto print_row = [](const char* name, std::size_t calls, double total_s,
                        std::size_t cache_hits, std::size_t timeouts) {
        const double avg_s =
            calls > 0 ? total_s / static_cast<double>(calls) : 0.0;
        std::cout << std::left << std::setw(12) << name << std::right
                  << std::setw(6) << calls << " calls  " << std::fixed
                  << std::setprecision(3) << std::setw(8) << total_s
                  << "s total  " << std::setw(8) << avg_s << "s avg";
        if (cache_hits > 0) {
            std::cout << "  (+" << cache_hits << " cache hits)";
        }
        if (timeouts > 0) {
            std::cout << "  (" << timeouts << " timeouts)";
        }
        std::cout << "\n";
    };
    std::cout << "\nTool timing report:\n";
    print_row("ltl2tgba", Ltl2tgbaStats::n_cache_misses,
              Ltl2tgbaStats::total_time_s, Ltl2tgbaStats::n_cache_hits,
              Ltl2tgbaStats::n_timeouts);
    // Every ltlfilt exec, over all three entry points. Reporting
    // simplify_ltl's misses here divided total_time_s, which covers
    // spot_satisfiable too, by a fraction of the calls it is over.
    print_row("ltlfilt", LtlfiltStats::n_execs(), LtlfiltStats::total_time_s,
              LtlfiltStats::n_cache_hits + LtlfiltStats::n_remove_wm_hits,
              LtlfiltStats::n_timeouts);
    print_row("ltlsynt", RealizabilityChecker::n_cache_misses,
              RealizabilityChecker::total_time_s,
              RealizabilityChecker::n_cache_hits,
              RealizabilityChecker::n_timeouts);
    print_row("black", SatisfiabilityChecker::n_black_calls,
              SatisfiabilityChecker::total_time_s,
              SatisfiabilityChecker::n_cache_hits,
              SatisfiabilityChecker::n_timeouts);
    // Untimed, so there is never a timeout to report.
    print_row("ganak", GanakStats::n_cache_misses, GanakStats::total_time_s,
              GanakStats::n_cache_hits, 0);
    if (Ltl2tgbaStats::n_tautology_substitutions > 0) {
        std::cout << "\nltl2tgba tautology substitutions (SPOT exit-2 bug, "
                     "treated as trivially true): "
                  << Ltl2tgbaStats::n_tautology_substitutions << "\n";
    }
    if (RealizabilityChecker::n_capability_errors > 0) {
        std::cout << "\nltlsynt queries past SPOT's acceptance-set limit "
                     "(resolved as undecided): "
                  << RealizabilityChecker::n_capability_errors << "\n";
    }
    if (WellSeparationStats::n_errors > 0) {
        std::cout << "\nWell-separation queries that raised (resolved as "
                     "undecided, candidate dropped): "
                  << WellSeparationStats::n_errors << "\n";
    }
    std::cout << "\nConstant-folded (decided by ltlfilt, no black call): "
              << SatisfiabilityChecker::n_constant_folded << "\n";
    std::cout << "Decided by SPOT (ltlfilt --satisfiable, no black call): "
              << SatisfiabilityChecker::n_spot_decided << "\n";
    std::cout << "Left to black (SPOT undecided, polarity allows escalation): "
              << SatisfiabilityChecker::n_black_calls << "\n";
    std::cout << "Not escalated (SPOT undecided, black would exhaust its "
                 "bound): "
              << SatisfiabilityChecker::n_escalations_declined << "\n";
    std::cout << "Weak-operator queries left unresolved (ltlfilt could not "
                 "rewrite W/M, black is unsound on them): "
              << SatisfiabilityChecker::n_weak_operator_unresolved << "\n";
    std::cout << "\nRepairs contributed by the cross-generation accumulator "
                 "(0 unless genetic.accumulate_repairs is set): "
              << AccumulatorStats::n_contributed << "\n";
    print_cache_report();
    std::cout << "\nFitness cache: " << FitnessCacheStats::n_hits << " hits / "
              << FitnessCacheStats::n_misses << " misses\n";
}

// Reports where CPU actually went: this process's own code (all threads) vs.
// the external CLI tools (separate child processes). getrusage gives the
// authoritative self/children split; the per-tool rows are attributed from
// each wrapper's wait4() and should sum to roughly the children total (the
// remainder is uninstrumented children, e.g. ltlfilt's equivalence check).
void print_cpu_report(double wall_s) {
    auto secs = [](const timeval& tval) {
        return static_cast<double>(tval.tv_sec) +
               (static_cast<double>(tval.tv_usec) / 1e6);
    };
    struct rusage self_ru{};
    struct rusage child_ru{};
    getrusage(RUSAGE_SELF, &self_ru);
    getrusage(RUSAGE_CHILDREN, &child_ru);
    const double self_cpu = secs(self_ru.ru_utime) + secs(self_ru.ru_stime);
    const double child_cpu = secs(child_ru.ru_utime) + secs(child_ru.ru_stime);
    const double total_cpu = self_cpu + child_cpu;

    auto pct = [total_cpu](double part) {
        return total_cpu > 0.0 ? 100.0 * part / total_cpu : 0.0;
    };
    auto line = [&pct](const char* name, double cpu_s) {
        std::cout << std::left << std::setw(20) << name << std::right
                  << std::fixed << std::setprecision(3) << std::setw(9) << cpu_s
                  << "s cpu  " << std::setprecision(1) << std::setw(5)
                  << pct(cpu_s) << "%\n";
    };

    std::cout << "\nCPU attribution (wall " << std::fixed
              << std::setprecision(3) << wall_s << "s, total cpu " << total_cpu
              << "s):\n";
    line("your code", self_cpu);
    line("CLI tools (total)", child_cpu);
    std::cout << "  per tool:\n";
    line("  ltl2tgba", Ltl2tgbaStats::total_cpu_s);
    line("  ltlfilt", LtlfiltStats::total_cpu_s);
    line("  ltlsynt", RealizabilityChecker::total_cpu_s);
    line("  black", SatisfiabilityChecker::total_cpu_s);
    line("  ganak", GanakStats::total_cpu_s);
    const double attributed =
        Ltl2tgbaStats::total_cpu_s + LtlfiltStats::total_cpu_s +
        RealizabilityChecker::total_cpu_s + SatisfiabilityChecker::total_cpu_s +
        GanakStats::total_cpu_s;
    line("  unattributed", child_cpu - attributed);
}
