# Building

PEREDUR builds with CMake presets. There are two workflows: a Nix dev shell that provides every dependency, and a manual one that needs a handful of system packages installed first.

## With Nix

Requires [Nix](https://nixos.org/download/) with flakes enabled.

```sh
nix develop                          # enter dev shell (first run fetches dependencies)
cmake --workflow --preset debug      # configure + build + test
```

A `.envrc` (`use flake`) is committed, so the dev shell can be entered automatically on `cd` by installing [direnv](https://direnv.net) and allowing it:

```sh
direnv allow
```

## Without Nix

The following must be installed system-wide:

| Requirement | Notes |
|---|---|
| CMake ≥ 3.25 | |
| [Ninja](https://ninja-build.org) | the presets' generator |
| C++17 compiler | gcc ≥ 7 or clang ≥ 5 |
| `libunwind` | |
| `libfmt` version 9 | see [below](#libfmt) |
| [Node.js](https://nodejs.org) | see [below](#nodejs) |

Everything else is fetched or built by CMake at configure time.

```sh
cmake --workflow --preset debug      # configure + build + test
```

## Presets

| Preset | Build directory | Notes |
|---|---|---|
| `debug` | `build/` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `release` | `build-release/` | no debug info |
| `relwithdebinfo` | `build-relwithdebinfo/` | release-like, with debug info |
| `tsan` | `build-tsan/` | ThreadSanitizer; disables address space layout randomisation (ASLR) for the tests |
| `bench` | `build-bench/` | release, plus the benchmarks in [`bench/`](../bench) registered as tests |
| `coverage` | `build-coverage/` | clang only; source-based coverage instrumentation, see [below](#coverage) |

`cmake --workflow --preset <name>` configures, builds and tests in one step. After the first configure, `cmake --build build` rebuilds incrementally.

Always go through a preset. `cmake -B build` on its own configures with CMake's default generator — Unix Makefiles on Linux — which conflicts with the Ninja tree every preset produces. The conflict reaches the sub-builds that CMake uses to fetch dependencies too, so the failure can be reported against one of those rather than against the generator. To recover, delete `CMakeCache.txt` and `CMakeFiles/` from the affected build directory and configure again with the preset. `build/third_party` survives that, so Spot is not rebuilt.

## Fetched dependencies

These are obtained by CMake regardless of workflow, so they need no manual installation:

| Dependency | How obtained |
|---|---|
| [Ganak](https://github.com/meelgroup/ganak) | pre-built binary — [`cmake/ganak.cmake`](../cmake/ganak.cmake) |
| [Spot](https://spot.lre.epita.fr) | built from source — [`cmake/spot.cmake`](../cmake/spot.cmake) |
| [Black](https://www.black-sat.org) | pre-built `.deb` (Ubuntu 24.04 x86\_64) or built from source — [`cmake/black.cmake`](../cmake/black.cmake) |
| Eigen, nlohmann\_json, tomlplusplus, cpptrace | FetchContent, header-only — [`cmake/dependencies.cmake`](../cmake/dependencies.cmake) |

Building Spot from source dominates the first configure. Subsequent configures reuse it.

### Node.js

The FRET requirement-formaliser command-line interface (CLI) is vendored as a plain script ([`vendor/fretCLI.main.js`](../vendor/fretCLI.main.js), see [`vendor/README.md`](../vendor/README.md)) and run with `node` looked up on `PATH` at run time. CMake neither builds nor fetches it, so `node` must be installed separately. The Nix dev shell provides it via the `nodejs` package.

### libfmt

`black` needs `libfmt.so.9` at run time, whether it is a system binary found on `PATH` or the pre-built `.deb` that `cmake/black.cmake` downloads as a fallback — the `.deb` does not bundle it. The Nix dev shell provides this via the `fmt_9` package; otherwise install `libfmt-dev` (or the equivalent) system-wide.

## Installing

```sh
cmake --install build-release --prefix <prefix> --component peredur
```

`--component peredur` is what keeps the install to this project. FetchContent brings its dependencies in with `add_subdirectory`, which brings their install rules along with them, so an unqualified install writes Eigen's headers and cpptrace's CMake config beside the binaries, a large amount of material belonging to neither.

The tree has three parts: `bin/` for the eight binaries, `libexec/peredur/` for the solvers, and `share/peredur/` for the dashboard page, the vendored formaliser script, the bundled examples and `peredur-env.sh`. The solvers sit under `libexec/` because they are private to PEREDUR, so a host with its own `ltlsynt` or `ganak` on `PATH` keeps getting that one for its own use.

```sh
. <prefix>/share/peredur/peredur-env.sh
```

Sourcing that file points the installed binaries at the solvers installed beside them. Without it they still look in the build tree they were compiled against, since every tool path is compiled in as an absolute path. The file resolves the prefix from its own location, so the tree is relocatable — verified by moving a prefix and re-running.

### Environment overrides

Five paths are compiled in, and each takes an environment variable that wins over it. `peredur-env.sh` sets all five; setting one by hand overrides that single path and leaves the rest alone.

| Variable | Overrides |
|---|---|
| `PEREDUR_SPOT_BIN_DIR` | the directory holding `ltlsynt`, `ltl2tgba` and `ltlfilt` |
| `PEREDUR_GANAK_PATH` | the `ganak` binary |
| `PEREDUR_BLACK_PATH` | the `black` binary |
| `PEREDUR_FORMALISER_SCRIPT` | the vendored FRET formaliser script |
| `PEREDUR_DASHBOARD_PAGE` | the dashboard page `--dashboard` copies into the output directory |

Each is read once, on first use. An unset or empty variable falls back to the compiled-in default, since a shell or container runtime that carries an unset variable around exports it as empty.

The container image is the packaged form of the same install tree, with the five variables baked in as `ENV` because there is no shell to source anything from. [Docker](docker.md) covers building and running it.

## Tests, linting and formatting

```sh
ctest --preset debug                       # run all tests
ctest --preset debug -R syntactic          # run tests matching a regex

cmake --build build --target lint          # cpplint + clang-tidy + cppcheck + config key parity
cmake --build build --target format        # apply clang-format in-place
cmake --build build --target format-ci     # dry-run, fails if unformatted
```

### Driver tests

`test/drivers/e2e_tests.cpp` holds one *end-to-end* suite per built driver, registered as `peredur_tests.driver_peredur`, `driver_realize`, `driver_ltl`, `driver_mucs`, `driver_compare`, `driver_lint_ideals` and `driver_signal_tracer`. Each spawns the binary through `execute_and_capture` rather than calling into the library, so these are the only tests that cover `src/main.cpp`, `src/repair/`, `src/crash/` and each standalone tool's own argument handling. `signal_tracer` is the exception, spawned through `spawn_piped_child`: it reads its frames from stdin, which `execute_and_capture` leaves as the test process's own, so under ctest its input would be whatever invoked the run rather than anything this suite chose. They locate the binaries through the `PEREDUR_DRIVER_DIR` compile definition (`$<TARGET_FILE_DIR:peredur>`), and `test/CMakeLists.txt` declares the seven drivers as dependencies of `peredur_tests` so they are built before ctest runs. A new driver needs all three — a suite, the dependency and the ctest registration.

The fixtures are inline in the test file rather than files under `examples/`, so editing an example cannot change what the tests assert. The four are a two-signal unrealizable TLSF specification, its realizable weakening with one added assumption, a two-guarantee FRETISH JSON, and a two-generation config over eight individuals.

What they assert is the driver's contract rather than the search's result: exit status, the stdout markers, and `run.json`'s seed, input, schema version and echoed config, plus the invariant that `n_repairs` equals the number of `repair_N` files written. The `peredur` suite runs the same seed twice and requires byte-identical repairs. Which repairs the search finds is pinned by the `determinism` suite instead, since asserting it here would break the driver tests on every deliberate change to the operators.

A TLSF run writes a `repair_N.fitness.json` sidecar beside each `repair_N.tlsf`, so a file filter matching the `repair_` prefix alone counts every repair twice. The seven entries add about 2.5 seconds, against 14.5 seconds for the whole suite under the `coverage` preset and 24 seconds under `debug`, where every binary they spawn is sanitised too.

### Coverage

The `coverage` preset compiles with clang's *source-based coverage* instrumentation, `-fprofile-instr-generate -fcoverage-mapping`, as a Debug build under `build-coverage/`. It is clang only, since gcc rejects both flags and `--coverage` with gcov would report a different number from a different tool. The preset's test half points `LLVM_PROFILE_FILE` at `build-coverage/profraw/%p.profraw`, so running the suite any other way leaves no profile to measure.

One script is the whole workflow.

```sh
python scripts/coverage_badge.py           # build, test, write docs/coverage.svg
python scripts/coverage_badge.py --check   # fail if the committed badge is stale
```

It configures, builds, runs `ctest --preset coverage`, merges the raw profiles with `llvm-profdata`, exports a summary with `llvm-cov export --summary-only` over `src/` and `include/`, and writes `docs/coverage.svg`. Stale profiles are deleted first, because they accumulate and an old one credits lines this build may not even contain. A failing suite refuses to write the badge. `--json <file>` reuses an llvm-cov export already made, and `--no-run` re-exports the profiles already on disk.

The badge is a committed file rather than a call out to a badge service, so the README renders on a fork with no secrets and in an offline clone. That holds only while the file is regenerated when the number moves, which is what `--check` is for. It runs as the tail of the `coverage` entry in the build matrix of [`.github/workflows/ci.yml`](../.github/workflows/ci.yml), on pushes to `main`, with `--no-run` so that it reads the profiles that entry's own `ctest` wrote rather than measuring the tree a second time. A pull request never runs that entry, so a commit that moves the figure and leaves `docs/coverage.svg` where it was fails after the merge rather than on the branch. Regenerate the badge before merging.

The measurement covers every instrumented binary rather than the test binary alone: `peredur`, `compare`, `lint-ideals`, `ltl`, `mucs`, `realize`, `signal_tracer` and `test/peredur_tests`. Over `peredur_tests` by itself the figure is 89.3%, and over all eight it is 83.6%. Every file in `src/` and `include/` now has non-zero coverage, and what remains uncovered is error and terminal branches — `include/status_line.hpp` at 54.8% for its tty-only paths, and the per-driver argument-error paths that not every suite exercises. A new binary goes into `BINARIES` in the script, which fails loudly when a name it holds is not built.

`llvm-profdata` and `llvm-cov` must come from the same LLVM release as the clang that built the tree. The profile format is versioned, so an older tool reports a current profile as malformed. The Nix dev shell carries `llvmPackages.llvm` for this; on a host with several LLVMs installed, `LLVM_COV` and `LLVM_PROFDATA` override the lookup.

Without help, the first coverage configure rebuilds Spot, black and Ganak into `build-coverage/third_party`, and Spot alone is a 30-minute build. `PEREDUR_THIRD_PARTY_DIR` points a preset at a tree that already exists, and `cmake/spot.cmake` skips the external project when its done-stamp is there:

```sh
cmake --preset coverage -DPEREDUR_THIRD_PARTY_DIR="$PWD/build/third_party"
```

It is a cache variable, so a later `cmake --preset coverage` with no arguments keeps it, which is what `scripts/coverage_badge.py` runs. CI sets the same variable across its matrix, pointing every leg at one directory so that Spot is built once rather than once per preset.

The figure moves by up to two tenths of a percentage point between runs of one binary — 83.60% to 83.80% over five runs — because the suite spawns real tools and branches on their timings and peak resident set. The badge prints a whole number, so that jitter matters only near a rounding boundary, and the current figure sits a tenth of a point above one. `--check` therefore carries `CHECK_SLACK`, a quarter of a point of tolerance beyond the committed number's rounding band: a run that lands the other side of the boundary passes, and a real drop of a third of a point or more still fails. Regenerating the badge and committing it is what a genuine move calls for.

## Documentation

```sh
cmake --build build --target docs   # Doxygen + Sphinx + KaTeX, all three required
```

This builds the curated public site from the `include/` headers.

Maths in doc comments is written as Doxygen's `\f$ ... \f$` (inline) and `\f[ ... \f]` (display), and rendered by KaTeX rather than Sphinx's default MathJax. The site fetches nothing at view time: `sphinxcontrib-katex` copies its own JS into `_static`, and `cmake/docs.cmake` stages `katex.min.css` and its fonts beside it out of the `katex` package, which `KATEX_DIST_DIR` locates. Set that variable by hand if the package sits somewhere the search does not reach.
