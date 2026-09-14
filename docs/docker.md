# Docker

The image carries the eight PEREDUR binaries, the solvers they spawn, the dashboard page and the bundled examples under `/opt/peredur`. It runs as a non-root user with `/work` as its working directory, and it is built from the repository root with the commit it was built from passed in as a build argument. Nothing about the algorithm changes inside it; the container is a way of getting Spot, black and Ganak without building them. [`vm.md`](vm.md) covers a VirtualBox appliance built from this image, for a user who wants a virtual machine rather than a container.

## Running a command

The image is published on Docker Hub as `benmandrew/peredur`, tagged with each commit's short sha, with `latest` for `main` and a `v*` tag under its own name; a local build carries whatever tag it was given. An entrypoint dispatches the first argument over the installed binaries, so an invocation reads like the command table in [`README.md`](../README.md):

```console
$ docker run --rm -v "$PWD:/work" benmandrew/peredur:<tag> realize spec.tlsf
UNREALIZABLE
```

A bare `--help`, or no arguments at all, prints the image's own usage and the command list. An argument beginning with `-` goes to `peredur`, so `docker run <image> --input spec.tlsf --output-dir out` works without naming the command twice. Any other argument runs as it stands, which is what makes `bash` reach a shell.

A full repair run. `peredur` requires its output directory to exist already, as it does outside a container, so create it on the host side of the mount first:

```console
$ mkdir -p out
$ docker run --rm --init --cpus=8 --memory=8g \
      --user "$(id -u):$(id -g)" -v "$PWD:/work" \
      benmandrew/peredur:<tag> peredur --input examples/lily02/spec.tlsf --output-dir out --seed 42
```

Three of those flags earn their place.

**`--init`.** Untimed `ltl2tgba` calls have leaked orphaned processes over long runs, several gigabytes of them across hours. Inside a container PEREDUR is process 1 and inherits every orphan the run produces, and it does not reap them. `--init` puts a real init process at PID 1 instead.

**`--memory`.** `ltlfilt` has been measured peaking near 3.4GB and `maximal` has taken 19GB on a maximality sweep. A cap turns a blowup that would otherwise take the machine down into a killed child, which PEREDUR's `execute_and_capture` reports as a failed tool call and carries on from.

**`--cpus`.** The scoring pool sizes itself from the *control group* (cgroup) CPU quota as well as the hardware concurrency, so this flag actually bounds the run rather than being ignored. Without it the pool is sized from the host's online CPU count, whatever the daemon was told.

## Output and the working directory

Everything PEREDUR writes is relative to the working directory: the repairs and `run.json` under `--output-dir`, and the crash handler's `crashes/` directory. The image sets that directory to `/work`. Without a *bind mount* there, the output dies with the container. `--user "$(id -u):$(id -g)"` makes what lands on the mount owned by the invoking user; the default user inside the image is uid 1000, which is what an unqualified mount is most often owned by.

## The bundled examples

The image installs `examples/` at `/opt/peredur/share/peredur/examples`, so it demonstrates a repair with no files supplied at all. `$PEREDUR_EXAMPLES` names that directory inside the container, which means a command using it has to be quoted and re-entered by a shell there (`sh -c 'realize $PEREDUR_EXAMPLES/lily02/spec.tlsf'`); the path is written out in full below.

```console
$ docker run --rm benmandrew/peredur:<tag> realize /opt/peredur/share/peredur/examples/lily02/spec.tlsf
UNREALIZABLE
```

## The live dashboard

The dashboard needs three things together: `--dashboard` on the command, a published port, and a static server pointed at the same mount the run writes to. It is not the main path into the image, and [`README.md`](../README.md) links the guide that covers what the page shows.

## Building the image

`PEREDUR_GIT_COMMIT` is a required build argument and must be a full 40-character lowercase hex sha:

```console
$ docker build --build-arg PEREDUR_GIT_COMMIT="$(git rev-parse HEAD)" \
      -t benmandrew/peredur:$(git rev-parse --short HEAD) .
```

It is required rather than optional because the build cannot answer the question itself. `.dockerignore` keeps `.git` out of the *build context*, so the history never lands in a layer and the context hash stops changing on every commit. That leaves no repository for `cmake/version.cmake` to read, and every binary answers `--version` with a `commit=` line naming what it was built from. Without the argument that line reads `commit=unknown`, and `scripts/run_experiments.py` refuses to launch a campaign against a binary that cannot say what it was built from.

Two optional companions go with it. `PEREDUR_GIT_COMMIT_SHORT` sets the abbreviation, defaulting to the first 7 characters of the sha. `PEREDUR_GIT_DIRTY=true` marks a build whose source is not a clean checkout of the commit it names. Both are rejected on their own, a short sha and a dirty flag meaning nothing without the commit they qualify.

| Build argument | Effect |
|---|---|
| `PEREDUR_GIT_COMMIT` | required; the full 40-character sha the binaries report |
| `PEREDUR_GIT_COMMIT_SHORT` | the abbreviation, default the first 7 characters |
| `PEREDUR_GIT_DIRTY` | `true` for a source tree that is not a clean checkout |
| `BUILD_JOBS` | caps every compile in the build; unset means one job per core |
| `WITH_NODE` | `1` adds Node.js to the runtime image; the default `0` leaves it out |
| `BASE_IMAGE` | default `ubuntu:24.04` |

`BUILD_JOBS` is the only knob that bounds the build's own CPU use. BuildKit runs the dependency stages concurrently and ignores the daemon-side `--cpu` flags, so a cold build otherwise takes the whole machine. It reaches the compiler by sitting in the `RUN` command, which also puts it in the cache key of every stage that reads it, Spot's included — so changing the value rebuilds Spot. Pick one and keep it rather than tuning it per build.

Node.js is absent by default because nothing the image runs calls it, and it is a substantial addition to the image for something never invoked. `node` runs the vendored FRET formaliser command-line interface (CLI), whose only callers are `test/runner/formaliser_tests.cpp` and the fuzzer, which differentially test `requirement_to_ltl()` against it. Both input paths translate in process, so the FRETISH path reaches a repair without it. `WITH_NODE=1` is for an image meant to run the suite rather than the tools.

`BASE_IMAGE` defaults to `ubuntu:24.04` for a specific reason. [`cmake/black.cmake`](../cmake/black.cmake) downloads a prebuilt `black-sat` `.deb` on noble and builds black from source against z3 anywhere else. The source build works, and it costs several minutes and a z3 toolchain in the builder for a binary that is already published.

## Publishing

[`.github/workflows/docker.yml`](../.github/workflows/docker.yml) builds the image on every push to `main` and on every `v*` tag, and publishes one *manifest list* to Docker Hub covering `linux/amd64` and `linux/arm64`. Nothing else publishes: a pull request and a manual `workflow_dispatch` both build the image and stop there, which still catches a Dockerfile that has stopped working. The condition is the event rather than whether the secrets can be read, since a pull request raised from a branch in this repository does get them and only a fork's does not.

The primary tag is the short commit sha, and it is deliberately the same seven characters the binaries print for `commit_short`. `cmake/write_version_header.cmake` takes the first seven of the sha and `docker/metadata-action`'s `type=sha,format=short` does the same, so an image tag and the `--version` inside that image name the commit identically. `latest` follows the default branch, and a `v*` git tag publishes under its own name as well.

There is no `docker/setup-qemu-action` anywhere in the workflow. Each architecture is built on a runner of its own architecture — `ubuntu-24.04` and `ubuntu-24.04-arm`, both free to public repositories — rather than by emulating arm64 on an amd64 host. Spot is the one dependency that compiles rather than downloads, so emulation would put an autotools C++ build behind an instruction translator, and that is where the cost would land.

Each build job pushes its image *by digest*, under no tag at all, and uploads that digest as an artefact. A merge job then downloads the digests and assembles them into the tagged manifest list with `docker buildx imagetools create`. A matrix that tagged as it went would publish a tag resolving for one architecture and not the other whenever half of it failed.

Both build jobs read and write a layer cache through `cache-from`/`cache-to: type=gha`, scoped per architecture. The per-dependency stage split pays off here, Spot's stage copying `cmake/spot.cmake` and nothing else, so a change anywhere else in the tree reuses its layer. GitHub documents that cache as holding 10GB per repository and evicting least recently used entries, and the two architectures share it with every other workflow, so a cold Spot rebuild is a possibility the scoping makes less likely rather than one it rules out.

The two architectures genuinely differ in one place. Upstream publishes exactly one Linux `black` binary and it is x86\_64, so amd64 downloads the prebuilt `.deb` and arm64 compiles black from source against z3, which is why `libz3-dev` is installed in the toolchain stage. That source build is the route macOS has always taken, Apple Silicon included, [`cmake/black.cmake`](../cmake/black.cmake)'s Darwin branch carrying no architecture test at all. Until 2026-08-27 an architecture guard sat at the top of that file's Linux branch and failed configure outright on Linux arm64; it now guards the `.deb` alone.

Pulling a tagged image needs no architecture flag, Docker resolving the manifest list to the puller's own:

```console
$ docker pull benmandrew/peredur:latest
$ docker buildx imagetools inspect benmandrew/peredur:latest
```

The second command lists both entries of the list, with the platform each was built for. The workflow publishes to `benmandrew/peredur` on Docker Hub. Images published before 2026-09-14 sit under `benmandrew/counter`, the repository's earlier name, because Docker Hub cannot rename a repository.

## Layer structure

The stage layout is load-bearing. Spot dominates the dependency build by roughly two orders of magnitude over Ganak and black, so it gets a stage of its own that copies [`cmake/spot.cmake`](../cmake/spot.cmake) and nothing else, and its layer is keyed on that one file. Copying the whole `cmake/` directory instead would rebuild Spot whenever an unrelated module such as `lint.cmake` changed.

black and Ganak get a stage each on the same argument, as do the four FetchContent dependencies: Eigen, cpptrace, nlohmann\_json and tomlplusplus. They depend on nothing but the shared toolchain stage, so BuildKit runs all four concurrently and a version bump to one cannot invalidate the others.

```console
$ docker build --target tools --build-arg BUILD_JOBS=8 .   # the four dependency stages, nothing else
```

Spot's autotools build dominates the other three by orders of magnitude. They run alongside it and finish long before it does, so the wall time of the dependency build is Spot's alone. The concurrency therefore saves little; what the split buys is the invalidation isolation.

## What the image carries

Almost all of the fetched tools' weight is *debug information*, and the builder strips it out of the install tree. That is the single largest reduction the image gets, and it holds for any version of Spot, black or Ganak, the debug tables scaling with the code they describe rather than with a release number.

The `release` preset has nothing to do with it. `CMAKE_BUILD_TYPE` governs what CMake compiles, and CMake compiles only PEREDUR; Spot's own `configure` chooses its flags and they include `-g`, and Ganak arrives as an unstripped upstream release binary. PEREDUR's binaries carry no debug information at all, having been built without `-g`.

They are left unstripped even so. A release build still emits a symbol table, and cpptrace resolves the crash handler's frames through it, so stripping `bin/` would trade a small saving for crash reports that are addresses and nothing else — in the one environment where re-running under a debugger is hardest. The strip step therefore covers `libexec/`, where the tools are, and stops there.

Only three of Spot's sixteen binaries are ever spawned — `ltlsynt`, `ltl2tgba` and `ltlfilt` — so [`cmake/install.cmake`](../cmake/install.cmake) names those three one at a time and the other thirteen never reach the runtime stage. The static archives are excluded on the same argument, nothing in the image linking against anything, and `libspot.a` is far larger than the shared library a run actually loads.

No figure for the built image is written down here, because a Spot or Ganak version bump moves it and nothing in the repository re-checks it. `docker images benmandrew/peredur:<tag>` reports the size of whatever was built, on the version pins in force at the time.

The stripping and the per-tool selection both fall out of one observation: what the image needs at run time is a handful of executables and two shared libraries, and everything else in a Spot install tree is there to build against. Packaging is mostly the work of deciding what to leave behind.
