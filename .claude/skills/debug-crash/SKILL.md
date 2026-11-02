---
name: debug-crash
description: Diagnose a PEREDUR crash log from crashes/. Use when investigating a SIGSEGV/SIGABRT/SIGFPE, an unsymbolized release stack trace, or reproducing a crash from its --seed.
---

# Debugging crashes from `crashes/`

Only `peredur` installs the crash handler (`src/crash/crash_handler.cpp`); the other binaries write no log. On SIGSEGV, SIGABRT or SIGFPE it forks the `signal_tracer` that sits beside the executable, which writes `crashes/crash_<pid>_<timestamp>.log` under the working directory of the run. `cpptrace::register_terminate_handler()` also traces an uncaught exception.

## What the log holds

`format_crash_metadata` (`src/repair/driver.cpp`) records the input path, the seed and ten config values: generations, population, crossover and mutation rates, `p_trigger`, `p_response`, `p_timing` and the three fitness weights. Every other key is missing. To reproduce, find the config file the run used (for a campaign run, under `experiments/<name>/`). A repro with the seed alone and default config runs a different search.

## Symbols and asserts

Both `release` and `relwithdebinfo` build with `-DNDEBUG`, so `assert()` does nothing in them. A failure path guarded only by an `assert` lets bad data through instead of stopping. Treat every `assert` between the crash site and the root cause as a place the bad value could have entered.

`build-release/` has no debug info, so its frames show raw addresses only. Do not reverse the PIE load offset by hand. Rebuild with debug info, which keeps `-DNDEBUG` and so stays close to release behaviour:

```sh
cmake --workflow --preset relwithdebinfo
```

## Reproduce

`peredur` exits 1 without `--input` and `--output-dir`, and the output directory must exist:

```sh
mkdir -p /tmp/repro
./build-relwithdebinfo/peredur --input <Input: from log> --output-dir /tmp/repro \
    --seed <Seed: from log> [--config <original config.toml>]
```

The repro is usually deterministic, even when the crash surfaces on a `ThreadPool` worker (`src/thread_pool.cpp`) during scoring. The seed fixes which candidates are bred, and the bug is usually an unhandled edge case in the data. A crash that does not reproduce under the same seed and config points to a data race: try the `tsan` preset.

If it reproduces, the new log from the `relwithdebinfo` binary is symbolised, with function and file:line per frame, including inlined frames. Read that first.

## Inspect the values at the fault

The program traces itself and exits, so attaching later is too late. Launch it under a debugger, which stops on the signal before the handler runs. An empty `std::vector` where the code assumed one element pinpoints the violated precondition faster than reasoning from the call chain.

On macOS use `lldb`, since Homebrew `gdb` cannot run programs on arm64:

```sh
lldb --batch -o run -o bt -o "frame variable" -- \
    ./build-relwithdebinfo/peredur --input <spec> --output-dir /tmp/repro --seed <seed>
```

On the Linux campaign hosts use `gdb`:

```sh
gdb -q -batch -ex run -ex bt -ex "info locals" \
    --args ./build-relwithdebinfo/peredur --input <spec> --output-dir /tmp/repro --seed <seed>
```
