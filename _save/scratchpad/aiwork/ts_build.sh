#!/bin/bash
# AI agent: build the native traffic harness (tools/traffic_sim.cpp) from a clean archive of a commit (default HEAD; the shared
# tree holds other agents' world WIP) into SP/ts/ - the world half and the AI half one at a time behind the build memory gate
# (3200), then the link. Usage: ts_build.sh [world|ai|all] [COMMIT]
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
O=$SP/ts
WHAT=${1:-all}
COMMIT=${2:-HEAD}
T=$O/tree
if [ "$WHAT" = all ] || [ "$WHAT" = world ]; then
  rm -rf $T; mkdir -p $T
  git -C /home/user/GTA-6-Claude-v0.5 archive $COMMIT src tools | tar -x -C $T || { echo "archive FAILED"; exit 5; }
  # (the harness itself is my work in progress in the shared tree: newer than the commit's copy)
  cp /home/user/GTA-6-Claude-v0.5/tools/traffic_sim.cpp $T/tools/traffic_sim.cpp
  echo "$(date +%T) tree of $(git -C /home/user/GTA-6-Claude-v0.5 rev-parse --short $COMMIT) in $T"
fi
cd $T
if [ "$WHAT" = all ] || [ "$WHAT" = world ]; then
  $AW/ai_memgate.sh 3200
  echo "$(date +%T) world part"
  rm -f $O/ts_world.o
  nice -n 5 g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_WORLD -c tools/traffic_sim.cpp -o $O/ts_world.o 2>&1 | grep -E "error|warning" | head -20
  [ -f $O/ts_world.o ] || { echo "world part FAILED"; exit 2; }
fi
if [ "$WHAT" = all ] || [ "$WHAT" = ai ]; then
  $AW/ai_memgate.sh 3200
  echo "$(date +%T) ai part"
  rm -f $O/ts_ai.o
  nice -n 5 g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_AI -c tools/traffic_sim.cpp -o $O/ts_ai.o 2>&1 | grep -E "error|warning" | head -20
  [ -f $O/ts_ai.o ] || { echo "ai part FAILED"; exit 2; }
fi
rm -f $O/traffic_sim.new
g++ $O/ts_world.o $O/ts_ai.o -o $O/traffic_sim.new -lpthread 2>&1 | grep -E "error|undefined" | head -20
# (renamed into place in one step: a harness run starting meanwhile never execs a half-written file)
[ -x $O/traffic_sim.new ] && mv -f $O/traffic_sim.new $O/traffic_sim && echo "$(date +%T) built $O/traffic_sim" || echo "link FAILED"
