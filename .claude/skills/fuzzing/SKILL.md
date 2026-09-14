---
name: fuzzing
description: Build and run the ltl_equivalence_fuzzer, which differentially tests requirement_to_ltl() against the FRET formaliser CLI. Use when fuzzing the LTL translator or replaying a libFuzzer repro.
---

# Fuzzing

`fuzz/ltl_equivalence_fuzzer` differentially tests the hand-rolled `requirement_to_ltl()` translator against the real FRET formaliser CLI: it generates random `Requirement`s, checks that the two LTL formulae are logically equivalent (via `ltlfilt --equivalent-to`), and aborts on a mismatch so libFuzzer captures a repro. It does not replace `requirement_to_ltl()` at runtime — the hand-rolled translator stays the source of truth used by fitness scoring, model counting, and `black`/`ltlsynt` (which don't all accept the CLI's bounded-interval LTL syntax); the CLI is cross-validation only.

Requires a clang++ with libFuzzer support on `PATH` (declared in `flake.nix`'s devShell) — GCC has no `-fsanitize=fuzzer` equivalent, so this target is built via a raw `clang++` invocation in `fuzz/CMakeLists.txt`, linked against the same `peredur_core`/`peredur_fitness` static libraries the active preset already built (verified safe to mix with GCC+ASAN/UBSAN in practice).

```sh
cmake --preset debug -DPEREDUR_FUZZ=ON
cmake --build build --target ltl_equivalence_fuzzer
./build/fuzz/ltl_equivalence_fuzzer -max_total_time=60 corpus_dir/   # fuzz for 60s
./build/fuzz/ltl_equivalence_fuzzer crash-<hash>                    # replay a repro
```

Each input spawns an `ltlfilt` subprocess (the formaliser CLI's own process is persistent and reused across inputs), so this runs orders of magnitude slower than a typical libFuzzer target — expected for differential testing against external tools, not a bug.
