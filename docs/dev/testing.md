# Tests and coverage

The test suites, the per-driver end-to-end suites, and the committed coverage badge.

## Tests

```sh
ctest --preset debug                  # run all tests
ctest --preset debug -R syntactic     # run tests matching a regex
```

Test binaries land at `build/test/peredur_tests`. Tests use `expect(bool, message)` and `fail(message)` from `test/test_support.hpp`. The same header has `expect_throws` for an expected exception (optionally checking its message), `first_seed` and `expect_some_seed` for a test that tries seeds until an outcome shows up, and `TempDir`, `write_text` and `read_text` for tests that need files on disk. Builders used by more than one suite live in `test/fixtures.hpp` (specifications, scripted random sources, the implication oracle) and `test/tlsf_fixtures.hpp` (TLSF documents, the unrealizable arbiter, the realizability check). A suite whose builder differs keeps its own in its anonymous namespace, which hides the shared one of the same name. Each file defines its tests with `TEST(name)` from `test/test_registry.hpp`, which registers the function under the file's `k_test_suite` during static initialisation; `TEST_IN(suite, name)` names the suite explicitly, for a file holding several. A suite runs its tests in definition order, which the standard fixes within one translation unit. Order across files is unspecified, so a suite lives in one file, and `test/main.cpp` selects by suite name only. Its table lists every suite in the order the no-argument run takes them. An entry can run a second registry suite after the first (`tlsf_mucs` runs `tlsf_guarantee_parts`) or stay out of the no-argument run (`thread_pool`). The binary refuses to start if a registered test belongs to a suite the table does not name. `test/CMakeLists.txt` adds one ctest entry per name in `peredur_test_suites`, plus the second `profile` and `thread_pool` entries that run those suites under a different environment.

`test/web/*.test.mjs` (ctest `dashboard_page`) tests the dashboard script under node, evaluating the `<script>` block of the shipped `web/dashboard.html`; CMake skips it without `node`.

`test/drivers/e2e_tests.cpp` holds one suite per driver, spawning the binary through `execute_and_capture`; they alone cover `src/main.cpp`, `src/repair/`, `src/crash/` and argument handling. A new driver needs its tests registered with `TEST_IN` under a new suite, that suite in the table in `test/main.cpp` and in `peredur_test_suites`, and an `add_dependencies` entry in `test/CMakeLists.txt`, or nothing covers it. The suites assert the driver's contract (exit status, stdout markers, `run.json` fields, `n_repairs` against the `repair_N` files) rather than which repairs are found, which `determinism` pins and every operator change would break. Fixtures are inline, independent of `examples/`. A TLSF run writes a `repair_N.fitness.json` sidecar per repair, so a `repair_` prefix filter double-counts.

## Coverage

`python scripts/coverage_badge.py` builds the clang-only `coverage` preset into `build-coverage/`, runs its ctest, merges and exports the profiles over `src/` and `include/`, and writes `docs/coverage.svg`. Only `ctest --preset coverage` sets `LLVM_PROFILE_FILE`. Stale profiles are deleted first, an old one crediting lines this build may lack, and a failing suite writes no badge. `llvm-profdata` and `llvm-cov` must match the clang release, the profile format being versioned, which is why `llvmPackages.llvm` is in the dev shell.

The committed badge must be regenerated when the number moves. `--check` re-measures as the tail of the CI `coverage` entry, which runs on pushes to `main` only, so run the script before merging a commit that moves the figure. The denominator is all eight instrumented binaries (83.6%, against 89.3% for `peredur_tests` alone), and a new binary must join `BINARIES` in the script. The figure moves up to two tenths of a point between runs and 83.6% sits a tenth above a rounding boundary, so `--check` allows `CHECK_SLACK`, a quarter of a point beyond the rounding band, where a byte comparison would fail unrelated commits.
