#!/bin/sh
# snapshot 56 (faces batch 10) starts once snapshot 55 has ended, on the HEAD it left
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
until grep -qE "COMMITTED|FAILED|HOLD" $SP/snap55_run.log; do sleep 20; done
# (then the autoupdater's starved compile gate, PID 14554, goes first: at most 40 min)
n=0; while kill -0 14554 2>/dev/null && [ $n -lt 120 ]; do sleep 20; n=$((n+1)); done
cd $REPO
if [ "$(git rev-parse HEAD:src/anim/face.cpp)" != dd935b1a745ef7c89c8381ae5ce6540ff63a4d3e ] || [ "$(git rev-parse HEAD:tests/anim/anim_test.cpp)" != 248cbf6a85676bd7060a66cfd4c8fd4ed00c9a83 ]; then
  echo "HOLD snapshot 56: a base blob changed"; exit 1
fi
rm -f "$SP/snap56_index"
GIT_INDEX_FILE=$SP/snap56_index git read-tree HEAD && GIT_INDEX_FILE=$SP/snap56_index git update-index --index-info < $SP/snap56_entries.txt && T=$(GIT_INDEX_FILE=$SP/snap56_index git write-tree) || { echo "HOLD snapshot 56: tree build failed"; exit 1; }
echo "snapshot 56 tree $T on $(git rev-parse --short HEAD) at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh $T $SP/snap_msg56.txt
