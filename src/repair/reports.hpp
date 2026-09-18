#pragma once

// PEREDUR's end-of-run reports, printed to stdout after the search finishes.

void print_scoring_report();

// The engine-internal counters: per-tool calls and cache totals, the ltl2tgba
// tautology substitutions, the constant-folded count, and the fitness cache hit
// rate. Printed only under --diagnostics; every figure in it is also written to
// run.json, which is where a campaign should read them from. A counter added
// here has to be added to write_run_manifest too, or the flag's default loses
// it.
void print_diagnostics_report();

void print_cpu_report(double wall_s);
