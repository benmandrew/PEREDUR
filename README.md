# PEREDUR

![Coverage](docs/coverage.svg)

PEREDUR repairs unrealisable reactive specifications with a multi-objective genetic algorithm.

A reactive specification is *unrealisable* when no implementation can satisfy it against every environment. A synthesis tool such as `ltlsynt` reports the verdict but does not say how to fix it. PEREDUR searches for *repairs*: edits to the specification that make it realisable while staying close to what was written.

The input is a specification in linear temporal logic (LTL), written in the basic Temporal Logic Synthesis Format (TLSF) that the reactive-synthesis community uses.

## Quickstart

The container image holds the binaries, the solvers they call and the bundled examples, so Docker is the only requirement. The example is the two-client arbiter that the paper uses as its running example ([`examples/paper-running-example`](examples/paper-running-example/spec.tlsf)). Check that it is unrealisable:

```console
$ docker run --rm benmandrew/peredur:latest \
      realize /opt/peredur/share/peredur/examples/paper-running-example/spec.tlsf
UNREALIZABLE
```

Then repair it:

```console
$ mkdir -p out
$ docker run --rm --init --user "$(id -u):$(id -g)" -v "$PWD:/work" benmandrew/peredur:latest \
      peredur --input /opt/peredur/share/peredur/examples/paper-running-example/spec.tlsf \
      --output-dir out --seed 42 --repair-summary
Seed: 42
gen: 1/10  time: 0.28s  best: 0.972  real: 37
...
gen: 10/10  time: 0.14s  best: 0.975  real: 33
Realizable specifications: 81 (11 maximal, 10 equivalent), written to out/

repair_0.tlsf
  ASSUME + G((F(g1)) || (!(r2)))

repair_1.tlsf
  GUARANTEE - G(!((g1) && (g2)))
  GUARANTEE + G((g1) && (g2))

repair_2.tlsf
  GUARANTEE - G((r1) -> (g1))
  GUARANTEE + G((r1) -> ((!(g2)) -> (g1)))

...
Done in 1.84s
```

The run writes 11 repairs to `out/`. Each `repair_N.tlsf` is a complete specification, and `repair_N.fitness.json` beside it holds its scores. `--repair-summary` adds the list after the count, which shows what each repair changed: a `-` line is a formula of the input that the repair dropped, and a `+` line is a formula that the repair added.

The arbiter shares one resource between two clients. The inputs `r1` and `r2` are the requests, and the outputs `g1` and `g2` are the grants. It has three guarantees:

```text
G !(g1 && g2)     never grant both clients together
G (r1 -> g1)      grant a request from client 1 at once
G (r2 -> g2)      grant a request from client 2 at once
```

The environment can raise both requests in the same step. The last two guarantees then force both grants, which the first guarantee forbids. Each pair of the three guarantees is realisable, so the conflict needs all three, and `mucs` reports all three as the minimal unrealisable core.

The 11 repairs resolve the conflict in different places. `repair_0` leaves the guarantees alone and adds an assumption. `repair_2` weakens the second guarantee, so that client 1 is granted only when client 2 is not. `repair_1` replaces mutual exclusion with a guarantee to grant both clients always, which is realisable and gives up the property the arbiter exists for. The search returns every maximal candidate that passes its checks, and the engineer chooses between them.

## How it works

PEREDUR evolves a population of candidate specifications. The default configuration runs 10 generations of 200 candidates.

1. **Seed.** The first population is 200 copies of the input.
2. **Breed.** Crossover and mutation rewrite the assumptions and guarantees of selected candidates. Three filters then drop duplicates, oversized candidates and vacuous ones.
3. **Score.** Each candidate has three objectives. The *realisability* objective grades a candidate by the size of a maximal realisable subset of its guarantees. The *semantic* objective compares the candidate's behaviours with the input's by bounded model counting. The *syntactic* objective compares the formulae themselves.
4. **Select.** The default scheme, `nsga2-apportion`, is a variant of the non-dominated sorting genetic algorithm NSGA-II. It ranks parents and offspring together by Pareto dominance over the three objectives. It then removes duplicates and gives each distinct candidate a number of places in the next population that falls with its rank.
5. **Collect.** Every candidate that is realisable and *well-separated* is kept, from any generation. A specification is well-separated when the system cannot satisfy it by forcing the assumptions to fail. Of the candidates kept, only the *maximal* ones under implication are written, with equivalent candidates merged into one.

Realisability queries go to `ltlsynt` from [Spot](https://spot.lre.epita.fr), satisfiability queries to [black](https://www.black-sat.org), and model counting to [Ganak](https://github.com/meelgroup/ganak) over automata that Spot builds. [Configuration](https://benmandrew.com/docs/peredur/configuration.html) describes the other selection schemes and every tuning key.

## Building locally

The local build uses [Nix](https://nixos.org/download/) with flakes enabled. The first configure builds Spot from source.

```console
$ nix develop
$ cmake --workflow --preset release
$ mkdir -p out
$ ./build-release/peredur --input examples/paper-running-example/spec.tlsf --output-dir out --seed 42 --repair-summary
```

[Building from source](docs/building.md) covers the build without Nix, the other presets and the tests.

## Documentation

The full documentation is published at [benmandrew.com/docs/peredur](https://benmandrew.com/docs/peredur/).

| Link | Description |
|---|---|
| [Examples](examples/README.md) | every bundled specification and where it comes from |
| [Docker](docs/docker.md) | running and building the container image, with resource limits |
| [Building from source](docs/building.md) | Nix and non-Nix builds, dependencies, presets, tests |
| [Virtual machine](docs/vm.md) | building a VirtualBox appliance from the image |
| [TLSF specifications](https://benmandrew.com/docs/peredur/tlsf.html) | a second worked repair, minimal unrealisable cores |
| [Architecture](https://benmandrew.com/docs/peredur/architecture.html) | algorithm flow, key types, module layout |
| [Configuration](https://benmandrew.com/docs/peredur/configuration.html) | tuning via TOML, objectives, selection schemes |
| [API reference](https://benmandrew.com/docs/peredur/) | the `include/` headers |
| [Experiment scripts](scripts/README.md) | parameter sweeps and result analysis |
