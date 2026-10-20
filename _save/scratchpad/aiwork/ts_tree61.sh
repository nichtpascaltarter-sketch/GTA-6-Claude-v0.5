#!/bin/bash
# AI agent (set 20): the harness's world half from a clean archive of HEAD (7b9146d: snapshot 61's world - the city's
# overlap fix, roads.cpp + roadnet.cpp) into SP/ts/tree61 and SP/ts/ts_world61.o (behind the build memory gate)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
O=$SP/ts
T=$O/tree61
COMMIT=${COMMIT:-7b9146d}
rm -rf $T; mkdir -p $T
git -C /home/user/GTA-6-Claude-v0.5 archive $COMMIT src tools | tar -x -C $T || { echo "archive FAILED"; exit 5; }
cp $AW/s20/traffic_sim.cpp $T/tools/traffic_sim.cpp
echo "$(date +%T) tree of $COMMIT in $T"
cd $T
$AW/ai_memgate.sh 3200
echo "$(date +%T) world part"
rm -f $O/ts_world61.o
nice -n 5 g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_WORLD -c tools/traffic_sim.cpp -o $O/ts_world61.o 2>&1 | grep -E "error|warning" | head -20
[ -f $O/ts_world61.o ] && echo "$(date +%T) built $O/ts_world61.o" || { echo "world part FAILED"; exit 2; }
