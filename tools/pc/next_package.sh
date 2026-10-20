#!/bin/sh
# The PC worker's queue (docs/PC_WORKER.md): prints the id of the oldest package on origin/claude/pc-queue that has not
# been started (no results/<id>/started on origin/pc/results) and is not cancelled (no queue/<id>.cancelled), and exits
# 0. Without one it exits 1, or with --wait fetches again every 60 s until there is one.
# usage: sh tools/pc/next_package.sh [--wait]
while :; do
  git fetch -q origin claude/pc-queue 2>/dev/null
  git fetch -q origin pc/results 2>/dev/null   # (there is no pc/results until the first result)
  for f in $(git ls-tree --name-only origin/claude/pc-queue queue/ 2>/dev/null | grep '\.md$' | sort); do
    id=$(basename "$f" .md)
    git cat-file -e "origin/claude/pc-queue:queue/$id.cancelled" 2>/dev/null && continue
    git cat-file -e "origin/pc/results:results/$id/started" 2>/dev/null && continue
    echo "$id"
    exit 0
  done
  [ "$1" = "--wait" ] || exit 1
  sleep 60
done
