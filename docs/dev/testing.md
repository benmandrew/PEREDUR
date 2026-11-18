# Tests and coverage

The test harness and the rules for adding a suite or a binary. Running the tests and measuring coverage are in [`docs/building.md`](../building.md).

## Tests

Running the tests is in "Tests, linting and formatting" in [`docs/building.md`](../building.md). To time each test in one suite:

```sh
PEREDUR_TEST_TIMES=1 build/test/peredur_tests scope
```

The `debug` and `release` test presets run eight suites at once, which took the debug suite from 56.5 s to 11.2 s on an 8-core Mac. `PEREDUR_TEST_TIMES` prints each test's wall time to stderr, to find the test worth shrinking inside a slow suite.

Test binaries land at `build/test/peredur_tests`. Tests use `expect(bool, message)` and `fail(message)` from `test/test_support.hpp`. The same header has `expect_throws` for an expected exception (optionally checking its message), `first_seed` and `expect_some_seed` for a test that tries seeds until an outcome shows up, and `TempDir`, `write_text` and `read_text` for tests that need files on disk. Builders used by more than one suite live in `test/fixtures.hpp` (specifications, scripted random sources, the implication oracle) and `test/tlsf_fixtures.hpp` (TLSF documents, the unrealizable arbiter, the realizability check). A suite whose builder differs keeps its own in its anonymous namespace, which hides the shared one of the same name. Each file defines its tests with `TEST(name)` from `test/test_registry.hpp`, which registers the function under the file's `k_test_suite` during static initialisation; `TEST_IN(suite, name)` names the suite explicitly, for a file holding several. A suite runs its tests in definition order, which the standard fixes within one translation unit. Order across files is unspecified, so a suite lives in one file, and `test/main.cpp` selects by suite name only. Its table lists every suite in the order the no-argument run takes them. An entry can run a second registry suite after the first (`tlsf_mucs` runs `tlsf_guarantee_parts`) or stay out of the no-argument run (`thread_pool`). The binary refuses to start if a registered test belongs to a suite the table does not name. `test/CMakeLists.txt` adds one ctest entry per name in `peredur_test_suites`, plus the second `profile` and `thread_pool` entries that run those suites under a different environment.

"Driver tests" in [`docs/building.md`](../building.md) describes the per-driver end-to-end suites in `test/drivers/e2e_tests.cpp`. A new driver needs its tests registered with `TEST_IN` under a new suite, that suite in the table in `test/main.cpp` and in `peredur_test_suites`, and an `add_dependencies` entry in `test/CMakeLists.txt`, or nothing covers it.

## Python tests

`scripts/test_*.py` are plain scripts with no pytest, each exiting non-zero on a failure. The `python-tests` job in `.github/workflows/ci.yml` runs them once on a pull request or push that touches `scripts/` or `.github/`, under Python 3.12 with `z3-solver` as the only installed package. It sits outside the compiler matrix because the tests import the scripts and run no binary, so each leg would re-derive the same answer. 3.12 is the interpreter the campaign hosts run (av1 to av3), and `campaign.py` needs its `tomllib`.

## Coverage

"Coverage" in [`docs/building.md`](../building.md) describes `scripts/coverage_badge.py`, the `coverage` preset, the LLVM version match and how CI keeps the badge current. For a change:

- A new binary must join `BINARIES` in the script, which fails loudly when a name it holds is not built.
- Never hand-edit `docs/coverage.svg`. The `coverage` entry in CI rewrites it after a push to `main`, and the `coverage-badge` job commits it, so a pull request that moves the figure needs no badge change of its own.
- The crash handler's signal path reads as uncovered although `test_peredur_writes_a_crash_report` runs it: the handler leaves through `_exit`, which skips the profile write.
