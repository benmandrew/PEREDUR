---
name: debug-crash
description: Diagnose a PEREDUR crash log from crashes/. Use when investigating a SIGSEGV/SIGABRT/SIGFPE, an unsymbolized release stack trace, or reproducing a crash from its --seed.
---

# Debugging crashes from `crashes/`

`crash_handler.cpp` installs a SIGSEGV/SIGABRT/SIGFPE handler that forks
`signal_tracer` (cpptrace) and writes `crashes/crash_<pid>_<timestamp>.log`,
including the `--seed` and full `Config` values needed to reproduce. Both the
`release` and `relwithdebinfo` presets build with `-DNDEBUG` (CMake's default
for those build types), so:

- `assert()` is a no-op in these binaries — a failure path guarded only by
  `assert` (e.g. a non-zero subprocess exit code, a parse precondition) does
  *not* abort; it silently continues with whatever garbage/default value
  resulted. When chasing a crash, treat every `assert` between the crash site
  and the actual root cause as a place where bad data could have been let
  through instead of being caught loudly.
- `build-release/` has no debug info, so its crash logs only show raw
  addresses (`at .../peredur`, no function/line). Don't try to reverse the
  PIE load offset by hand — rebuild `relwithdebinfo` instead (it keeps
  `-DNDEBUG`, so timing/behavior stays close to release, but adds `-g`):

  ```sh
  cmake --workflow --preset relwithdebinfo
  ```

- Reproduce with the exact seed from the crash log's `Config:` block:

  ```sh
  ./build-relwithdebinfo/peredur --seed <seed from log>
  ```

  This is usually deterministic even though the crash often surfaces inside a
  worker thread (`score_population`'s `std::async` pool) — the RNG seed
  drives which specifications get generated, and the bug is typically a data
  bug (an unhandled edge case), not a true data race.
- If it reproduces, the new `crashes/` log from the `relwithdebinfo` binary is
  now fully symbolized (function + file:line per frame, including inlined
  frames) — read that first.
- To inspect the actual values at the fault (not just the stack shape), run
  under `gdb -batch`, since the program crashes itself before you'd get a
  chance to attach:

  ```sh
  gdb -q -batch -ex "run --seed <seed>" -ex "print <expr>" -ex bt \
      --args ./build-relwithdebinfo/peredur --seed <seed>
  ```

  Inspecting the live locals (e.g. an empty `std::vector` where the code
  assumed at least one element) pinpoints the precondition that was violated,
  which is usually faster than reasoning from the call chain alone.
