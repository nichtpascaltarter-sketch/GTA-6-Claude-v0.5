#!/bin/sh
# cycle3.sh <ids1> <ids2> ...: one build (waiting for a clean tree, retrying on link errors), then one runner process per
# id group; prints each group's summary
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
LOGF=/tmp/wine_story/drive_c/users/root/AppData/Local/NeonTide/log.txt
for attempt in 1 2 3 4 5 6 7 8; do
  until $SP/memgate.sh 1800 && ! $SP/check.sh 3000 2>&1 | grep -q " error"; do sleep 60; done
  cd $SP/storytest && nice -n 10 ./build_storytest.sh > build.log 2>&1
  [ -f $SP/storytest/nt_storytest.exe ] && break
  echo "build attempt $attempt failed: $(grep -m1 -o "undefined reference to [^']*'\|error:.*" $SP/storytest/build_full.log)"
  sleep 240
done
if [ ! -f $SP/storytest/nt_storytest.exe ]; then echo "BUILD FAILED"; exit 1; fi
for ids in "$@"; do
  rm -f /tmp/story/*.bmp
  $SP/memgate.sh 2400
  $SP/storytest/run.sh "$ids" > /dev/null 2>&1
  echo "=== $ids"
  grep -a "missiontest" $LOGF | grep -a "SUMMARY\|  [a-z0-9_]*: \|FAIL\|roam: [0-9]* checks" | cut -c1-200
  grep -aq "SUMMARY" $LOGF || { echo "(no summary: the runner died)"; tail -3 /tmp/story/run.txt; }
  mkdir -p /tmp/story/keep
  for f in /tmp/story/*_cut*.bmp; do [ -f "$f" ] && nice convert "$f" -resize 320x180 /tmp/story/keep/$(basename "$f" .bmp).png; done
done
