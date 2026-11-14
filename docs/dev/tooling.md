# Build, lint and docs tooling

Build presets, lint caching, git hooks and the documentation site.

## Build

Enter the dev shell first (Nix is the primary workflow):

```sh
nix develop
```

Two presets — `debug` (ASAN+UBSAN, `build/`) and `release` (`build-release/`):

```sh
cmake --workflow --preset debug      # configure + build + test
cmake --workflow --preset release

# Incremental build only (after initial configure):
cmake --build build
cmake --build build-release
```

The release and bench presets set no `CMAKE_CXX_FLAGS`. Their former `-Wall -Werror` reached cpptrace's feature probes, `HAS_UNWIND` and `HAS_CXX_EXCEPTION_TYPE`, which failed under it, and CMake cached the failed results. `-Werror` on in-tree targets comes from `cmake/warnings.cmake` instead. On an older tree the root `CMakeLists.txt` clears that exact cached value and both probe results once.

Non-Nix: requires CMake ≥ 3.25, Ninja, a C++17 compiler, `libunwind` and Node.js; CMake fetches the rest.

## Lint & Format

```sh
cmake --build build --target lint          # cpplint + clang-tidy + cppcheck + config parity
cmake --build build --target lint-cpplint
cmake --build build --target lint-clang-tidy
cmake --build build --target lint-cppcheck
cmake --build build --target lint-config-schema

cmake --build build --target format        # apply clang-format in-place
cmake --build build --target format-ci     # dry-run (fails if unformatted)
```

clang-tidy results are cached through ctcache whenever it is on PATH, as the dev shell puts it. The cache lives in `<build-dir>/ctcache`, gitignored and per preset; `CTCACHE_DIR` overrides it, which CI sets so `actions/cache` can restore it.

ctcache's hash leaves out the clang-tidy binary, so CI's cache key and its restore prefix both carry the clang-tidy version, read from the dev shell. Without it a toolchain bump would replay the old version's clean results for every unchanged translation unit. The cached path is all of `.ctcache`, so it still matches when results are filed under a per-version subdirectory.

CI runs on Linux. `.github/workflows/macos.yml` runs `cmake --workflow --preset debug` under `nix develop` on `macos-latest` once a week, on manual dispatch, and on a pull request that touches the build files, `flake.*` or `src/runner/`, and is the only CI coverage of the Darwin branches. CI's third-party cache entries hold only the installed tools: `scripts/prune_third_party.sh` deletes `downloads/`, any leftover `spot_src` or `work/` directory, and `*.a` and `*.la` files as each job's last step, keeping the install prefixes and their markers. `actions/cache` cannot do this with `!` patterns, because it hands `tar` the directories it matched and `tar` recurses into them. Their keys hash `flake.lock` and `flake.nix` as well as the three tool modules and `cmake/third_party.cmake`.

The same checks run from the tracked `.githooks/pre-commit`. Its format check runs clang-format on the staged contents of each staged C++ file (`git show :<file>`), so a partly staged file is checked as it will be committed; cpplint and cppcheck still read the working tree. Git does not clone hooks, so `cmake/githooks.cmake` sets `core.hooksPath` at configure time, the hooks needing build targets anyway. Edit hooks in `.githooks/`, never `.git/hooks/`. `git commit --no-verify` bypasses one; CI is the real enforcement.

## Docs

Every header in `include/` needs a `.rst` page under `docs/api/` and an entry in `docs/index.rst` before committing. `src/` is deliberately not published.

Only `///` reaches Doxygen. Its text goes through Doxygen's comment parser, so a bare `<input>` parses as an HTML tag and `WARN_AS_ERROR` fails the build; wrap it in backticks.

`-DPEREDUR_DOCS_ONLY=ON` configures the `docs` target alone, then returns before the third-party tools, the FetchContent dependencies and every compiled target. The site that embeds these docs configures this way, because a full configure builds Spot from cold on each new commit, and its CI measured 785–880 s for that step against about 20 s without it. `nix build .#docs` runs the same configure in the sandbox and leaves the HTML in `result/`. Its source is a `lib.fileset` of the files that configure reads, so a commit that touches neither `include/` nor the Sphinx sources under `docs/` keeps the same store path. A file the docs start to read has to join `docsSrc` in `flake.nix`, or the sandbox build cannot see it.

Maths is `\f$ ... \f$` / `\f[ ... \f]`, rendered by KaTeX with nothing fetched at view time. `sphinxcontrib-katex` points `katex_css_path` at a CDN, so `cmake/docs.cmake` stages `katex.min.css` and its fonts into `_static/katex/` and `docs/conf.py.in` overrides the path. The stylesheet resolves its fonts relatively, so the two must share a directory.
