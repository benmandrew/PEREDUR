# PEREDUR

[![Docker image](https://img.shields.io/badge/docker-benmandrew%2Fperedur-2496ED?logo=docker&logoColor=white)](https://hub.docker.com/r/benmandrew/peredur) ![Coverage](docs/coverage.svg)

PEREDUR repairs unrealisable reactive specifications with a multi-objective genetic algorithm. It takes a linear temporal logic (LTL) specification in the Temporal Logic Synthesis Format (TLSF) and searches for edits that make it realisable while staying close to the original.

## Quickstart

The Docker image holds the binaries, the solvers they call and the bundled examples. To repair the paper's running example and write the repairs to `out/`:

```sh
mkdir -p out
docker run --rm --init --user "$(id -u):$(id -g)" -v "$PWD:/work" benmandrew/peredur:latest \
    peredur --input /opt/peredur/share/peredur/examples/paper-running-example/spec.tlsf \
    --output-dir out --repair-summary
```

To repair your own specification, put it in the current directory and pass `--input /work/spec.tlsf`. Each `repair_N.tlsf` in `out/` is a complete specification. Run `peredur --help` for the other options.

## Building locally

The local build uses [Nix](https://nixos.org/download/) with flakes enabled:

```sh
nix develop
cmake --workflow --preset release
./build-release/peredur --input examples/paper-running-example/spec.tlsf --output-dir out
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
| [TLSF specifications](https://benmandrew.com/docs/peredur/tlsf.html) | a worked repair, minimal unrealisable cores |
| [Architecture](https://benmandrew.com/docs/peredur/architecture.html) | algorithm flow, key types, module layout |
| [Configuration](https://benmandrew.com/docs/peredur/configuration.html) | tuning via TOML, objectives, selection schemes |
| [API reference](https://benmandrew.com/docs/peredur/) | the `include/` headers |
| [Experiment scripts](scripts/README.md) | parameter sweeps and result analysis |
