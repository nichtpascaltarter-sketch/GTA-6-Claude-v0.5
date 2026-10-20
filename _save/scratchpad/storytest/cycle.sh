#!/bin/sh
# wait for a clean tree, build the runner (retrying while other agents' WIP fails to link), run the tests:
# cycle.sh <ids> [game args]
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
for attempt in 1 2 3 4 5 6 7 8; do
  until ! $SP/check.sh 3000 2>&1 | grep -q " error"; do sleep 60; done
  cd $SP/storytest && nice -n 10 ./build_storytest.sh > build.log 2>&1
  [ -f $SP/storytest/nt_storytest.exe ] && break
  echo "build attempt $attempt failed: $(grep -m1 -o "undefined reference to [^']*'\|error:.*" $SP/storytest/build_full.log)"
  sleep 240
done
if [ ! -f $SP/storytest/nt_storytest.exe ]; then echo "BUILD FAILED"; exit 1; fi
rm -f /tmp/story/*.bmp /tmp/story/*.png
$SP/storytest/run.sh "$@" > /dev/null 2>&1
grep -a "missiontest" /tmp/wine_story/drive_c/users/root/AppData/Local/NeonTide/log.txt | grep -v "screenshot" | tail -${TAILN:-60} | cut -c1-170
