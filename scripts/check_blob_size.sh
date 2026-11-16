#!/bin/sh
# Reject any blob over the size limit that a commit would add to history.
#
#   check_blob_size.sh --cached       blobs staged in the index (pre-commit)
#   check_blob_size.sh <base>..<head>  blobs new in that range (CI)
#
# A blob in history stays in every clone for good, and removing it means
# rewriting every commit SHA that PROVENANCE.json files and results CSVs
# record. Large campaign outputs stay untracked instead: see "Large outputs" in
# docs/dev/campaigns.md. Paths under vendor/ are exempt, since they are pinned
# third-party sources.
set -eu

limit=512000

case "${1:-}" in
    --cached)
        # :old_mode new_mode old_sha new_sha status<TAB>path
        list=$(git diff --cached --raw --no-abbrev --no-renames \
                   --diff-filter=AM | awk -F'\t' '{split($1, f, " "); print f[4] " " $2}')
        ;;
    *..*)
        list=$(git rev-list --objects "$1" | awk 'NF >= 2')
        ;;
    *)
        echo "usage: $0 --cached | <base>..<head>" >&2
        exit 2
        ;;
esac

[ -n "$list" ] || exit 0

over=$(printf '%s\n' "$list" \
    | git cat-file --batch-check='%(objecttype) %(objectsize) %(rest)' \
    | awk -v limit="$limit" '$1 == "blob" && $2 > limit && $3 !~ /^vendor\// {
          size = $2; sub(/^blob [0-9]+ /, ""); printf "  %s (%d KiB)\n", $0, size / 1024 }')

[ -z "$over" ] && exit 0

echo "Blobs over $((limit / 1024)) KiB would enter history:"
echo "$over"
echo "Keep large outputs untracked (\"Large outputs\" in docs/dev/campaigns.md)."
exit 1
