#!/bin/bash
# AI agent (set 19): the harness's AI half from a second tree (SP/ts/tree19) = the base tree's src/game and tools with my
# (ts_build19v.sh: the same with a suffix V=... for the tree, object and binary - a second s19 build beside the first)
V=${V:-v}
# set 19 sources (AW/s19) laid over them, the other source dirs linked to the base tree; linked with the base tree's world
# half into SP/ts/traffic_sim19 (renamed into place in one step). Behind the build memory gate (3200).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
O=$SP/ts
T=$O/tree
T19=$O/tree19${V}
rm -rf $T19
mkdir -p $T19/src
for d in $(ls $T/src); do
  if [ "$d" = game ]; then cp -r $T/src/game $T19/src/game; else ln -s $T/src/$d $T19/src/$d; fi
done
cp -r $T/tools $T19/tools
for f in $AW/s19/*.cpp $AW/s19/*.h; do [ -f "$f" ] && cp "$f" $T19/src/game/ && echo "overlay $(basename $f)"; done
cd $T19
$AW/ai_memgate.sh 3200
echo "$(date +%T) ai part (s19)"
rm -f $O/ts_ai19${V}.o
nice -n 5 g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_AI -c tools/traffic_sim.cpp -o $O/ts_ai19${V}.o 2>&1 | grep -E "error|warning" | head -20
[ -f $O/ts_ai19${V}.o ] || { echo "ai part FAILED"; exit 2; }
rm -f $O/traffic_sim19${V}.new
g++ $O/ts_world.o $O/ts_ai19${V}.o -o $O/traffic_sim19${V}.new -lpthread 2>&1 | grep -E "error|undefined" | head -20
[ -x $O/traffic_sim19${V}.new ] && mv -f $O/traffic_sim19${V}.new $O/traffic_sim19${V} && echo "$(date +%T) built $O/traffic_sim19${V}" || echo "link FAILED"
