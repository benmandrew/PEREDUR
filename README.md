# Counter

![Coverage](docs/coverage.svg)

Counter repairs unrealisable reactive specifications using a genetic algorithm.

A reactive specification is *unrealisable* when no implementation can satisfy it against every environment — the specification is at fault, not the code. Tools like `ltlsynt` will say so, but not what to change. Counter searches for the repairs: edits to the specification that make it realisable while staying as close as possible to what was originally written.

Inputs are either *FRETISH* requirements as JSON, or basic *TLSF* — the Temporal Logic Synthesis Format used by the reactive-synthesis community.

## Quickstart

Build, then repair one of the bundled examples ([building from source](docs/building.md) covers the non-Nix route):

```console
$ nix develop
$ cmake --workflow --preset release

$ ./build-release/realize examples/lily02/spec.tlsf
UNREALIZABLE

$ mkdir -p out
$ ./build-release/counter --input examples/lily02/spec.tlsf --output-dir out --seed 42
Realizable specifications: 11 (3 maximal), written to out/
Done in 4.12s
```

That writes the 3 maximal repairs to `out/`, each `repair_N.tlsf` paired with a `repair_N.fitness.json` holding its score. Expect a few seconds on 20 threads; the seed fixes the repairs, not the runtime, which swings with how the external solvers get scheduled.

The [container image](docs/docker.md) is the alternative to `nix develop`: it carries the binaries, the solvers and these examples, so `docker run --rm benmandrew/peredur:latest realize /opt/counter/share/counter/examples/lily02/spec.tlsf` needs no toolchain on the host.

The example is a grant arbiter that must answer every request within three ticks, never grant twice in a row, and withhold grants after a `cancel` until a `go` arrives. Nothing forces `go` to ever arrive, so a cancelled request can be neither granted nor refused — and the specification cannot be implemented. All three repairs rewrite that third guarantee, which is also the one `mucs` identifies as the minimal unrealisable core. The [TLSF guide](https://benmandrew.com/docs/counter/tlsf.html) walks through this run in full.

## Commands

| Command | Purpose |
|---|---|
| `counter --input <spec> --output-dir <dir>` | repair an unrealisable specification |
| `realize <spec>...` | report whether a specification is realisable |
| `ltl <spec>...` | print the LTL formulae a specification translates to |
| `compare --repairs <dir> --ideals <dir>` | compare repairs against known-ideal ones |
| `mucs <spec.tlsf>` | extract a minimal unrealisable core from a TLSF spec |
| `maximal <dir-or-file>...` | report which specifications of a set are maximal under the implication order |

A `<spec>` is either a FRETISH `.json` or a `.tlsf` file.

Run any command with `--help` for full option descriptions.

## How it works

Counter evolves a population of candidate specifications over several generations, keeping those that are realisable and close to the original.

1. **Seed** a population of specifications, each mutated slightly from the input.
2. **Score** each candidate on three weighted components: semantic similarity (bounded model counting of satisfying traces), realisability status, and syntactic similarity.
3. **Evolve** through rounds of selection, crossover, mutation, and filtering.
4. **Collect** the realisable survivors, keep the genuine weakenings of the original and, of those, only the maximal ones under implication, then write each to the output directory.

Model counting uses [Ganak](https://github.com/meelgroup/ganak) over the transition matrices of SPOT-generated automata. Satisfiability and realisability queries use [black](https://www.black-sat.org) and [ltlsynt](https://spot.lre.epita.fr) respectively.

## Documentation

The full documentation is published at [benmandrew.com/docs/counter](https://benmandrew.com/docs/counter/).

| | |
|---|---|
| [Building from source](docs/building.md) | Nix and non-Nix builds, dependencies, presets, tests |
| [Docker](docs/docker.md) | building and running the container image |
| [Virtual machine](docs/vm.md) | building a VirtualBox appliance from the image |
| [Architecture](https://benmandrew.com/docs/counter/architecture.html) | algorithm flow, key types, module layout |
| [Configuration](https://benmandrew.com/docs/counter/configuration.html) | tuning via TOML, fitness weights, selection schemes |
| [TLSF specifications](https://benmandrew.com/docs/counter/tlsf.html) | TLSF mode and a worked repair |
| [API reference](https://benmandrew.com/docs/counter/) | the `include/` headers |
| [Experiment scripts](scripts/README.md) | parameter sweeps and result analysis |
