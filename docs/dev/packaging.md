# External tools and packaging

The external solvers, how their paths resolve, install rules and the container image.

## External tools

- `ltl2tgba`, `ltlsynt`: SPOT, built by `cmake/spot.cmake` and found via the `SPOT_BIN_DIR` macro.
- `black`: linear temporal logic (LTL) satisfiability checker, from `PATH` or `cmake/black.cmake`. Always run it with a timeout, `black -t <seconds> ...`.
- `ganak`: model counter, a release binary fetched by `cmake/ganak.cmake`.
- `node`: runs the vendored FRET formaliser (`vendor/fretCLI.main.js`), found on `PATH` at run time and never fetched by CMake.

## Packaging

Five paths are compiled in as build-tree absolutes, and each yields to an environment override: `PEREDUR_SPOT_BIN_DIR`, `PEREDUR_GANAK_PATH`, `PEREDUR_BLACK_PATH`, `PEREDUR_FORMALISER_SCRIPT`, `PEREDUR_DASHBOARD_PAGE`. `tool_path_from_env` (`src/runner/tool_paths.hpp`) is the one reader, and callers hold its result in a function-local `static`, since `getenv` races `setenv` across the scoring pool's threads. An empty value reads as unset, because shells and container runtimes export an unset variable as empty.

Install with `--component peredur` (`cmake/install.cmake`): FetchContent's `add_subdirectory` imports the dependencies' install rules, so an unqualified install writes Eigen's headers and cpptrace's cmake config beside the binaries. Spot's binaries carry an absolute libtool RUNPATH into their build tree, so each is installed behind a wrapper (`cmake/tool-wrapper.sh.in`) setting `LD_LIBRARY_PATH` to a sibling `lib` resolved from `$0`. Without it the first `ltlsynt` call fails silently.

`Config::parallel` defaults to `available_parallelism()` (`include/thread_pool.hpp`), the minimum of `hardware_concurrency()`, the `sched_getaffinity` count and the cgroup CPU quota, each skipped when unreadable and floored at 1. `hardware_concurrency()` ignores `--cpuset-cpus` and `--cpus`, so a `--cpus=4` run on a 64-core host sized its pool at 64. The cgroup parsers are pure functions in `src/cpu_limits.hpp` so they can be tested.

`-DPEREDUR_GIT_COMMIT=<sha>` (`cmake/version.cmake`) names the commit for a build from a source copy, which is how the image is built. It takes the full 40 characters and fills in only what git cannot answer: a disagreement with git fails the build, and `PEREDUR_GIT_DIRTY` can set the dirty flag but never clear one git set. Without it the build reports `commit=unknown` and `run_experiments.py` refuses to launch.

The Dockerfile has one stage per fetched dependency, each copying only its own cmake module, so Spot's layer is keyed on `cmake/spot.cmake` alone; Spot takes 248.1s to build against black's 3.6s at `BUILD_JOBS=8`. `docs/docker.md` is the user-facing half.

`.github/workflows/docker.yml` builds each architecture on a native runner and pushes by digest under no tag. Only the merge job's `imagetools create` tags, so a half-failed matrix cannot publish one architecture alone. `cmake/black.cmake`'s architecture guard must cover only the prebuilt `.deb`: around the whole Linux branch it fails arm64 configure instead of falling through to the source build, which an amd64 build will not show.
