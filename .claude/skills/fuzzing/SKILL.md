---
name: fuzzing
description: Build and run the ltl_equivalence_fuzzer, which differentially tests requirement_to_ltl() against the FRET formaliser CLI. Use when fuzzing the LTL translator or replaying a libFuzzer repro.
---

# Fuzzing

`fuzz/ltl_equivalence_fuzzer` generates random `Requirement`s, lowers each with `requirement_to_ltl()` and with the vendored FRET formaliser, and checks the two formulae are equivalent with `ltlfilt --equivalent-to`. It aborts on a mismatch, on a sentence the CLI rejects, and on an equivalence `ltlfilt` cannot decide, so libFuzzer captures a repro for each. The formaliser is a test oracle only; `requirement_to_ltl()` stays the translator every binary uses. The design, including why `F[0,-1] φ` folds to `false`, is in "FRETISH scopes" in `docs/dev/fretish-scopes.md`.

## Build and run

The target needs a `clang++` with libFuzzer on `PATH`; the `nix develop` shell provides one. `fuzz/CMakeLists.txt` builds it with a raw `clang++` call and links the `peredur_core`, `peredur_fitness` and `peredur_formaliser` libraries the active preset built. On Linux those libraries are GCC-built, and mixing them with the clang object works. On macOS the shell's compiler is already clang 22.

```sh
cmake --preset debug -DPEREDUR_FUZZ=ON
cmake --build build --target fuzz_ltl_equivalence
./build/fuzz/ltl_equivalence_fuzzer -max_total_time=60 corpus_dir/   # fuzz for 60 s
./build/fuzz/ltl_equivalence_fuzzer crash-<hash>                    # replay a repro
```

Each input spawns one `ltlfilt` process, while the formaliser's `node` process is reused across inputs. This runs orders of magnitude slower than a typical libFuzzer target, which is expected for differential testing against external tools.

## After a crash

Replay the `crash-<hash>` file: stderr shows the verdict, the requirement's parts, the FRETish sentence and both formulae. A mismatch is a bug in `requirement_to_ltl()` or in the fuzzer's folding of FRET's output; fix it, then pin the case in `test/scope_tests.cpp` so it outlives the corpus.
