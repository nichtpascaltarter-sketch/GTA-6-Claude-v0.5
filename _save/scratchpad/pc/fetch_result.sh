#!/bin/sh
# Lead side of docs/PC_WORKER.md: state of a package (queued / started / done) and its result files, extracted to
# $SP/pc/results/<id>/. usage: fetch_result.sh <id>
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5 || exit 2
git fetch -q origin pc/results 2>/dev/null || { echo "$1: no pc/results yet"; exit 1; }
if git cat-file -e origin/pc/results:results/$1/summary.md 2>/dev/null; then
  rm -rf $SP/pc/results/$1; mkdir -p $SP/pc/results/$1
  git archive origin/pc/results results/$1 | tar -x -C $SP/pc/results/$1 --strip-components=2
  echo "$1: done"; ls $SP/pc/results/$1
elif git cat-file -e origin/pc/results:results/$1/started 2>/dev/null; then echo "$1: started $(git cat-file -p origin/pc/results:results/$1/started)"
else echo "$1: queued, not started"; exit 1; fi
