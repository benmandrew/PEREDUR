# syntax=docker/dockerfile:1

# Ubuntu 24.04 rather than a smaller base because cmake/black.cmake downloads a
# prebuilt black-sat .deb only on noble, and builds it from source against z3
# anywhere else. That fallback works; it costs several minutes and a z3
# toolchain in the builder for a binary that is already published.
ARG BASE_IMAGE=ubuntu:24.04

# Fixed by the `release` preset in CMakePresets.json, which puts its build tree
# at ${sourceDir}/build-release. Every stage below has to use the same path: the
# fetch modules install into ${CMAKE_BINARY_DIR}/third_party and skip their own
# work when they find a done-stamp there, and a stamp under a different prefix
# is a stamp the real build never looks for.
ARG BUILD_DIR=/src/build-release

# Caps every compile in the image build. BuildKit runs the dependency stages at
# once and ignores the daemon-side --cpu flags, so this is the knob that stops a
# build taking the whole machine; leave it unset for one job per core.
#
# It reaches the compiler by sitting in the RUN command, which puts it in the
# cache key of every stage that reads it — Spot's included. Changing the value
# therefore rebuilds Spot, so pick one and keep it rather than tuning it per
# build.
ARG BUILD_JOBS


# --- toolchain ---------------------------------------------------------------
FROM ${BASE_IMAGE} AS toolchain

# lsb-release is load-bearing: cmake/black.cmake decides between the prebuilt
# .deb and the source build on `lsb_release -cs`, and the command is absent from
# the base image. Without it the codename reads empty, the noble test fails, and
# the build silently takes the long way round.
#
# libz3-dev is what makes that long way round work, and arm64 has no other
# route: upstream publishes one Linux black binary and it is x86_64, so every
# other architecture compiles black against a solver library it finds here.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        file \
        git \
        libfmt-dev \
        libunwind-dev \
        libz3-dev \
        lsb-release \
        ninja-build \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY docker/tools/CMakeLists.txt /src/CMakeLists.txt
WORKDIR /src


# --- one stage per fetched dependency ----------------------------------------
# Each copies the single cmake module that drives it and nothing else, so its
# layer is keyed on that file alone. Spot's autotools build dominates the other
# three by orders of magnitude — they fetch prebuilt binaries where it compiles
# a library — so keeping them apart is what stops a Ganak version bump, or an
# edit to any other module under cmake/, costing that rebuild. They depend on
# nothing but the toolchain, so BuildKit runs them concurrently.

FROM toolchain AS spot
ARG BUILD_DIR
ARG BUILD_JOBS
COPY cmake/spot.cmake cmake/
RUN cmake -S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPEREDUR_TOOL_MODULE=spot.cmake \
        ${BUILD_JOBS:+-DCMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS}} \
    && cmake --build "${BUILD_DIR}" ${BUILD_JOBS:+--parallel ${BUILD_JOBS}}

FROM toolchain AS black
ARG BUILD_DIR
ARG BUILD_JOBS
COPY cmake/black.cmake cmake/
RUN cmake -S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPEREDUR_TOOL_MODULE=black.cmake \
        ${BUILD_JOBS:+-DCMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS}} \
    && cmake --build "${BUILD_DIR}" ${BUILD_JOBS:+--parallel ${BUILD_JOBS}}

FROM toolchain AS ganak
ARG BUILD_DIR
ARG BUILD_JOBS
COPY cmake/ganak.cmake cmake/
RUN cmake -S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPEREDUR_TOOL_MODULE=ganak.cmake \
        ${BUILD_JOBS:+-DCMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS}} \
    && cmake --build "${BUILD_DIR}" ${BUILD_JOBS:+--parallel ${BUILD_JOBS}}

# Eigen, cpptrace, nlohmann_json and tomlplusplus. FetchContent populates at
# configure time into ${CMAKE_BINARY_DIR}/_deps, and re-reads the stamps it
# leaves there, so staging them here keeps four git clones out of the loop that
# every source edit re-enters.
FROM toolchain AS deps
ARG BUILD_DIR
COPY cmake/dependencies.cmake cmake/
RUN cmake -S . -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPEREDUR_TOOL_MODULE=dependencies.cmake


# --- tools -------------------------------------------------------------------
# The four staged into one tree. Assembled in its own stage rather than at the
# head of the builder so that `--target tools` builds all four and nothing else,
# which is what makes them run in parallel on a cold cache.
FROM toolchain AS tools
ARG BUILD_DIR

# The install tree and the stamp, not the unpacked source tree beside them,
# which is the larger of the two by far: cmake/spot.cmake reads the stamp to
# decide whether to declare the external project at all, and nothing downstream
# of that reads the sources.
COPY --from=spot ${BUILD_DIR}/third_party/spot ${BUILD_DIR}/third_party/spot
COPY --from=spot ${BUILD_DIR}/third_party/spot_src/src/spot_project-stamp/spot_project-done \
                 ${BUILD_DIR}/third_party/spot_src/src/spot_project-stamp/
COPY --from=black ${BUILD_DIR}/third_party/black ${BUILD_DIR}/third_party/black
COPY --from=ganak ${BUILD_DIR}/third_party/ganak ${BUILD_DIR}/third_party/ganak
COPY --from=deps ${BUILD_DIR}/_deps ${BUILD_DIR}/_deps


# --- builder -----------------------------------------------------------------
FROM tools AS builder
ARG BUILD_DIR
ARG BUILD_JOBS

# --version must name the commit the binary was built from, and .dockerignore
# keeps .git out of the context so the history does not land in a layer and
# invalidate it on every commit. -DPEREDUR_GIT_COMMIT supplies what git cannot
# answer here; without it the binary reports commit=unknown, and
# scripts/run_experiments.py refuses to launch a campaign against a binary that
# cannot say what it was built from.
#
# Checked before the sources are copied so a missing argument fails in a second
# rather than after the context transfer.
ARG PEREDUR_GIT_COMMIT
ARG PEREDUR_GIT_DIRTY=false
RUN test -n "${PEREDUR_GIT_COMMIT}" || { \
        echo "build-arg PEREDUR_GIT_COMMIT is required (full 40-char sha)" >&2; \
        exit 1; \
    }

COPY . .

RUN cmake --preset release \
        -DPEREDUR_GIT_COMMIT="${PEREDUR_GIT_COMMIT}" \
        -DPEREDUR_GIT_DIRTY="${PEREDUR_GIT_DIRTY}" \
    && cmake --build "${BUILD_DIR}" ${BUILD_JOBS:+--parallel ${BUILD_JOBS}} \
    && cmake --install "${BUILD_DIR}" --prefix /opt/peredur --component peredur

# The fetched tools alone, and almost all of their weight is debug information:
# Spot's configure chooses its own CXXFLAGS and they include -g, and Ganak ships
# as an unstripped upstream release binary. CMAKE_BUILD_TYPE never reached
# either of them — it governs what cmake compiles, and cmake compiles only
# PEREDUR. Nothing in the image reads those tables, PEREDUR symbolising its own
# crashes rather than a child's.
#
# PEREDUR's own binaries under bin/ are deliberately left alone. The release
# preset builds without -g, so they carry no debug information to begin with,
# but they do carry a symbol table and cpptrace resolves the crash handler's
# frames through it. Stripping that would trade a small saving for crash reports
# that are addresses and nothing else, in the one place where re-running under a
# debugger is hardest.
RUN find /opt/peredur/libexec -type f -exec sh -c \
        'file -b "$1" | grep -q "ELF .*not stripped" && strip --strip-unneeded "$1"' _ {} \; \
        2>/dev/null || true


# --- runtime -----------------------------------------------------------------
FROM ${BASE_IMAGE} AS runtime

# libfmt9 is black's and libunwind8 is cpptrace's, and both paths need them.
#
# nodejs is off by default because nothing the image runs calls it. It runs the
# vendored FRET formaliser, and the only callers of that are
# test/runner/formaliser_tests.cpp and the fuzzer, which differentially test
# requirement_to_ltl() against it — the FRETISH path itself translates in
# process. It is a substantial addition for something never invoked, so
# --build-arg WITH_NODE=1 is for an image meant to run the suite rather than the
# tools.
ARG WITH_NODE=0
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libfmt9 \
        libunwind8 \
    && if [ "${WITH_NODE}" = "1" ]; then \
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends nodejs; \
    fi \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/peredur /opt/peredur
COPY --chmod=755 docker/entrypoint.sh /usr/local/bin/peredur-entrypoint

# The same values cmake writes into share/peredur/peredur-env.sh, set as ENV
# because there is no shell to source that file from. Each overrides a path
# compiled into the binary that points into the builder stage's build tree,
# which this image does not carry.
ENV PATH=/opt/peredur/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    PEREDUR_SPOT_BIN_DIR=/opt/peredur/libexec/peredur/spot/bin \
    PEREDUR_GANAK_PATH=/opt/peredur/libexec/peredur/ganak \
    PEREDUR_BLACK_PATH=/opt/peredur/libexec/peredur/black/bin/black \
    PEREDUR_FORMALISER_SCRIPT=/opt/peredur/share/peredur/fretCLI.main.js \
    PEREDUR_DASHBOARD_PAGE=/opt/peredur/share/peredur/dashboard.html \
    PEREDUR_EXAMPLES=/opt/peredur/share/peredur/examples

# Repairs, run.json and the crash handler's crashes/ directory are all written
# relative to the working directory, so this is the one path that has to be a
# bind mount for anything to survive the container.
WORKDIR /work

# Non-root by default. The base image ships its own uid 1000 as `ubuntu`; the
# name is taken over rather than worked around, since 1000 is the uid a bind
# mount is most often owned by and matching it is what makes the default work
# without --user. Every installed file is world readable and executable, so any
# other uid can run the image too.
RUN userdel --remove ubuntu \
    && useradd --create-home --uid 1000 --shell /bin/sh peredur \
    && chown peredur:peredur /work
USER peredur

ENTRYPOINT ["peredur-entrypoint"]
CMD ["--help"]
