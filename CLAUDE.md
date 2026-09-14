# PEREDUR

C++17 genetic algorithm for repairing unrealizable FRETISH and TLSF specifications, using bounded model counting (SPOT + Ganak) for semantic similarity. Binaries: `peredur`, `realize`, `compare`, `ltl`, `mucs`, `maximal`, `lint-ideals` (run each with `--help`).

A run loads a spec, breeds offspring, filters them (dedup, bloat cap, vacuity), scores the survivors (syntactic + semantic + status), selects under NSGA-II, re-checks realizability at the output gate, applies final filters (dedup, implication), and writes `repair_N.json` plus `run.json`.

## Design notes

Detail lives in `docs/dev/`. Read the file covering an area before changing it. A reference elsewhere to a named section "of CLAUDE.md" resolves to the heading of the same name in one of these files.

| File | Sections |
|---|---|
| `docs/dev/algorithm.md` | Algorithm flow, Elitism, Termination modes, Run output, Live dashboard, TLSF repair modes, Key types |
| `docs/dev/operators.md` | Crossover, Operator repairs of 2026-08-19, Implication in the TLSF grammar, Monotone rewrites, Cloned assumptions, Assumption construction, Timing donation, Removable guarantees |
| `docs/dev/fretish-scopes.md` | FRETISH scopes |
| `docs/dev/performance.md` | Cache keys, Scoring dispatch, Implication prefilter, Tool subprocesses, Profiling |
| `docs/dev/config-and-provenance.md` | Config keys (including config vintage and retired keys), Commit provenance |
| `docs/dev/campaigns.md` | Campaigns: the operating manual for `scripts/campaign.py` |
| `docs/dev/testing.md` | Tests, Coverage |
| `docs/dev/tooling.md` | Build, Lint & Format, Docs |
| `docs/dev/packaging.md` | External tools, Packaging |

## Build, test, lint

```sh
nix develop
cmake --workflow --preset debug      # ASAN+UBSAN, build/: configure + build + test
cmake --workflow --preset release    # build-release/
cmake --build build                  # incremental

ctest --preset debug [-R <regex>]
node --test "test/web/*.test.mjs"    # dashboard page script

cmake --build build --target lint    # cpplint + clang-tidy + cppcheck + config parity
cmake --build build --target format  # format-ci for a dry run
```

Tests use `expect`/`fail` from `test/test_support.hpp`, with each suite a free function declared in `test/test_suite.hpp`. Hooks live in `.githooks/`; edit them there.

## Code style

- C++17, `-Wall -Wextra -Wpedantic -Werror` on all targets. Run `format` before committing.
- Comments only where the why is non-obvious.
- `assert()` for internal invariants; `throw` only at API boundaries.
- Arithmetic on `Count` goes through `count_add_overflow` / `count_mul_overflow`, asserting on the flag.
- Dispatch on `std::variant` with `std::visit` and `if constexpr` branches, not `std::get_if` chains.
- Lint suppressions are path-scoped or an inline `NOLINT` with a reason. A blanket one hides every future instance.

## Rules that fail silently

### Determinism

- Never pass two `RandomSource` draws as arguments of one call: gcc and clang evaluate them in opposite orders. Sequence each draw into a local.
- Breeding stays one pipeline stage. Splitting crossover from mutation reorders every draw after the first.
- A new probability arm reads its probability before touching the `RandomSource`, so at 0 it costs no draw. `golden_config()` in `test/genetic/determinism_tests.cpp` pins every such key explicitly.
- The Global-scope FRETISH lowering stays byte-identical; `test_global_scope_lowering_is_unchanged` pins it.

### Config and output

- A new TOML key touches `apply_*` and `config_key_spec()` in `src/config_io.cpp`, `config_json()` in `src/repair/manifest.cpp`, `schemas/config-schema.json`, `example-config.toml` (whose values must equal the built-in defaults), and `DEFAULT_FIELDS` or `GEN_CONFIGS_FIELDS` in `scripts/check_config_schema.py`. `lint` checks them against each other.
- Moving a C++ default changes what every archived config means: record it under "Config vintage" in `experiments/README.md`. Removing a key retires the sweeps and runner profiles that emit it.
- A new `--diagnostics` counter also joins `write_run_manifest` and bumps `k_schema_version`.
- A new `peredur` flag joins the table in `src/main.cpp`, or `find_unknown_arg` rejects it.
- Anything that moves the cursor goes through `stdout_is_tty()` (`include/status_line.hpp`). Most runs are redirected to `run.log`.
- The scoring report's `N individual(s) dropped` line prints unconditionally; `run_experiments.py` parses it.
- Report best fitness as the population maximum, never `population[0]`.
- The dashboard page script does no DOM access above its `boot()` call; `test/web/` evaluates it under node.

### Engine

- A new `FilterKind::Correctness` filter needs a row in the correctness table, or the output gate never applies it.
- Tombstoned requirements (`m_removed`) are skipped at every read site and never erased, because slots pair by index.
- A cache storing a verdict or count keys on `formula_key::renamed()`; one storing a formula or automaton keys on `canonical()`.
- Pipes are created with `pipe2(..., O_CLOEXEC)`, and nothing forks outside `src/runner/process.cpp`.
- Tool paths come from `tool_path_from_env`, held in a function-local `static`.
- Always run `black` with a timeout (`-t <seconds>`).
- Wall-time A/B comparisons on the shared box interleave the arms per case and read a median.

### Docs and tests

- Every header in `include/` needs a `docs/api/` `.rst` page and a `docs/index.rst` toctree entry.
- Only `///` reaches Doxygen. Wrap text like `<input>` in backticks, or `WARN_AS_ERROR` fails the docs build.
- A new driver's end-to-end suite needs its function in `test/drivers/e2e_tests.cpp`, its declaration in `test/test_suite.hpp`, both dispatch points in `test/main.cpp`, an `add_dependencies` entry and a ctest registration in `test/CMakeLists.txt`.
- A new binary joins `BINARIES` in `scripts/coverage_badge.py`. Run that script before merging a change that moves coverage.

### Campaigns and provenance

- Everything that acts on a campaign goes through `scripts/campaign.py`, reading `experiments/<name>/campaign.toml`. Never hand-roll an ssh launch or choose a seed range at the prompt.
- `campaign.py status` is the source of truth. Re-run it instead of recalling state, never poll in a loop, and never spawn an agent to watch a run: `enqueue` and let the tick drive it.
- `KEY_FIELDS` in `merge_experiments.py` and the resume key in `run_experiments.py` are off limits, and `commit`/`dirty` never join either. Renaming a factor directory means extending `canonical_scheme()` in both scripts.
- Never delete a `provenance/*` tag. Cut a campaign branch's tag before rebasing or splitting that branch.
