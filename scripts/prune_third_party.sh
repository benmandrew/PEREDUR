#!/usr/bin/env bash
# Deletes what nothing reads once a third-party tree (PEREDUR_THIRD_PARTY_DIR)
# is installed: the downloaded archives, the static and libtool archives, and
# any work directory a failed build left behind. The install prefixes and
# their .peredur-installed markers stay, which is all a later configure reads
# (cmake/third_party.cmake).
#
# CI runs this as the last step of a job that caches the tree, so that
# actions/cache saves the installed halves alone. The cache step cannot do it
# itself: actions/cache resolves its paths with implicitDescendants off and
# hands tar the directories it matched, and tar recurses into each one, so a
# `!` pattern never removes anything below a directory that matched.
#
# Usage: scripts/prune_third_party.sh <third-party dir>
set -euo pipefail

dir=${1:?usage: prune_third_party.sh <third-party dir>}
if [ ! -d "$dir" ]; then
    echo "$dir does not exist; nothing to prune."
    exit 0
fi

before=$(du -sk "$dir" | cut -f1)
# A rebuild fetches its archive again, and a cache hit never reads one.
rm -rf "$dir/downloads" "$dir/spot_src"
find "$dir" -mindepth 2 -maxdepth 3 -type d -name work -prune -exec rm -rf {} +
# Nothing links against the tools, and cmake/install.cmake keeps the archives
# out of the installed tree for the same reason.
find "$dir" -type f \( -name '*.a' -o -name '*.la' \) -delete
after=$(du -sk "$dir" | cut -f1)
echo "Pruned $dir from $((before / 1024)) MiB to $((after / 1024)) MiB."
