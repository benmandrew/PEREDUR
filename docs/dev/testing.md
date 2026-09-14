# Tests and coverage

The test suites, the per-driver end-to-end suites, and the committed coverage badge.

## Tests

```sh
ctest --preset debug                  # run all tests
ctest --preset debug -R syntactic     # run tests matching a regex
```

Test binaries land at `build/test/counter_tests`. Tests use `expect(bool, message)` and `fail(message)` from `test/test_support.hpp`, and each suite is a free function declared in `test/test_suite.hpp`.

`test/web/*.test.mjs` (ctest `dashboard_page`) tests the dashboard script under node, evaluating the `<script>` block of the shipped `web/dashboard.html`; CMake skips it without `node`.

`test/drivers/e2e_tests.cpp` holds one suite per driver, spawning the binary through `execute_and_capture`; they alone cover `src/main.cpp`, `src/repair/`, `src/crash/` and argument handling. A new driver needs a suite function, its declaration in `test/test_suite.hpp`, both dispatch points in `test/main.cpp`, an `add_dependencies` entry in `test/CMakeLists.txt` and a ctest registration, or nothing covers it. The suites assert the driver's contract (exit status, stdout markers, `run.json` fields, `n_repairs` against the `repair_N` files) rather than which repairs are found, which `determinism` pins and every operator change would break. Fixtures are inline, independent of `examples/`. A TLSF run writes a `repair_N.fitness.json` sidecar per repair, so a `repair_` prefix filter double-counts.

## Coverage

`python scripts/coverage_badge.py` builds the clang-only `coverage` preset into `build-coverage/`, runs its ctest, merges and exports the profiles over `src/` and `include/`, and writes `docs/coverage.svg`. Only `ctest --preset coverage` sets `LLVM_PROFILE_FILE`. Stale profiles are deleted first, an old one crediting lines this build may lack, and a failing suite writes no badge. `llvm-profdata` and `llvm-cov` must match the clang release, the profile format being versioned, which is why `llvmPackages.llvm` is in the dev shell.

The committed badge must be regenerated when the number moves. `--check` re-measures as the tail of the CI `coverage` entry, which runs on pushes to `main` only, so run the script before merging a commit that moves the figure. The denominator is all eight instrumented binaries (83.6%, against 89.3% for `counter_tests` alone), and a new binary must join `BINARIES` in the script. The figure moves up to two tenths of a point between runs and 83.6% sits a tenth above a rounding boundary, so `--check` allows `CHECK_SLACK`, a quarter of a point beyond the rounding band, where a byte comparison would fail unrelated commits.
