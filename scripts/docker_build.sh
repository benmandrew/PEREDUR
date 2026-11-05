#!/usr/bin/env bash
# Builds the PEREDUR image from this working tree, stamped with the commit it
# was built from and with whether the tree differs from that commit.
#
# .dockerignore keeps .git out of the build context, so the build cannot ask git
# either question and the Dockerfile takes both as build arguments. Passed by
# hand, PEREDUR_GIT_DIRTY is easy to forget, and its default of false then
# stamps an image built from uncommitted edits as a clean build of HEAD. This
# script answers it the way cmake/write_version_header.cmake does for a build
# in a work tree: a modified tracked file makes the tree dirty, and an
# untracked one does not.
#
# Usage: scripts/docker_build.sh [docker build options]
#
# Every argument goes to `docker build`, and the context is always the
# repository root. With no arguments the image is tagged
# benmandrew/peredur:<short sha>, with -dirty appended for a dirty tree, so a
# local build cannot take the name of a published image it differs from.
set -euo pipefail

root=$(git rev-parse --show-toplevel)
commit=$(git -C "$root" rev-parse HEAD)
short=$(git -C "$root" rev-parse --short=7 HEAD)

dirty=false
if [ -n "$(git -C "$root" status --porcelain --untracked-files=no)" ]; then
    dirty=true
fi

args=("$@")
if [ "${#args[@]}" -eq 0 ]; then
    tag="benmandrew/peredur:${short}"
    if [ "$dirty" = true ]; then
        tag="${tag}-dirty"
    fi
    args=(-t "$tag")
fi

echo "Building commit ${commit} (dirty=${dirty})." >&2
exec docker build \
    --build-arg PEREDUR_GIT_COMMIT="$commit" \
    --build-arg PEREDUR_GIT_DIRTY="$dirty" \
    "${args[@]}" \
    "$root"
