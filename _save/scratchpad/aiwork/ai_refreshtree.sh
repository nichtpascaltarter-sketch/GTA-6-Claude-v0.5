#!/bin/bash
# AI agent: a build of mine queued for the build lock (its frozen tree not compiling yet): copy the AI files' current
# versions into its tree and re-record the blobs, so the build picks up the latest. Usage: ai_refreshtree.sh TAG
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
T=$SP/tree_$1
[ -d $T ] || { echo "no tree $T"; exit 1; }
# (compiling already - a cc1plus anywhere under the build script's process tree, or the build done: too late)
BP=$(cat $AW/ai_buildloop.pid 2>/dev/null)
[ -n "$BP" ] && kill -0 $BP 2>/dev/null || { echo "no build of mine running"; exit 2; }
if ps -eo pid=,ppid=,comm= | awk -v root=$BP '{par[$1]=$2; cmd[$1]=$3} END {for (p in par) {q=p; for (i=0; i<12 && q!="" && q!=root; i++) q=par[q]; if (q==root && cmd[p]=="cc1plus") found=1} exit !found}'; then
  echo "$1 is compiling already: too late"; exit 2
fi
cd /home/user/GTA-6-Claude-v0.5
for f in ai.cpp ai_game.h app.cpp barks.cpp events.cpp pedai.cpp police.cpp traffic.cpp traffic_core.cpp lanes.cpp pednav.cpp ai_core.h wildlife.cpp wildlife.h; do cp -p src/game/$f $T/src/game/$f; done
for f in ai.cpp ai_core.h ai_game.h app.cpp barks.cpp events.cpp gameworld.h pedai.cpp pednav.cpp police.cpp traffic.cpp traffic_core.cpp lanes.cpp population.cpp peds.cpp game.h wildlife.cpp wildlife.h; do
  echo "$(git hash-object -w $T/src/game/$f) src/game/$f"
done > $AW/blobs_$1.txt
echo "$(date +%T) refreshed the AI files in the frozen tree" | tee -a $AW/refresh_$1.log
