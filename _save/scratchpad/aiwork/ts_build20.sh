#!/bin/bash
# AI agent (set 20): the harness's AI half from a tree SP/ts/tree20$V = the base tree (SP/ts/tree: 837b39c's world) with
# src/game overlaid by AW/$SRC (s19: set 19 = the base for set 20; s20: set 20 work) and tools/traffic_sim.cpp = the set 20
# harness (AW/s20/traffic_sim.cpp: --uturntest, the parked strip rule of population.cpp), linked with the base tree's
# world half into SP/ts/traffic_sim20$V. Usage: SRC=s19|s20 V=b|... ts_build20.sh   (behind the build memory gate)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
O=$SP/ts
T=$O/${TREE:-tree}            # (TREE=tree61 WOBJ=ts_world61.o: HEAD 7b9146d's world, snapshot 61)
WOBJ=${WOBJ:-ts_world.o}
SRC=${SRC:-s20}
V=${V:-}
TT=$O/tree20${V}
rm -rf $TT
mkdir -p $TT/src
for d in $(ls $T/src); do
  if [ "$d" = game ]; then cp -r $T/src/game $TT/src/game; else ln -s $T/src/$d $TT/src/$d; fi
done
cp -r $T/tools $TT/tools
cp $AW/s20/traffic_sim.cpp $TT/tools/traffic_sim.cpp
for f in $AW/$SRC/*.cpp $AW/$SRC/*.h; do
  b=$(basename $f)
  [ "$b" = traffic_sim.cpp ] && continue
  cp "$f" $TT/src/game/ && echo "overlay $SRC/$b"
done
cd $TT
$AW/ai_memgate.sh 3200
echo "$(date +%T) ai part ($SRC)"
rm -f $O/ts_ai20${V}.o
nice -n 5 g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_AI -c tools/traffic_sim.cpp -o $O/ts_ai20${V}.o 2>&1 | grep -E "error|warning" | head -30
[ -f $O/ts_ai20${V}.o ] || { echo "ai part FAILED"; exit 2; }
rm -f $O/traffic_sim20${V}.new
g++ $O/$WOBJ $O/ts_ai20${V}.o -o $O/traffic_sim20${V}.new -lpthread 2>&1 | grep -E "error|undefined" | head -20
[ -x $O/traffic_sim20${V}.new ] && mv -f $O/traffic_sim20${V}.new $O/traffic_sim20${V} && echo "$(date +%T) built $O/traffic_sim20${V}" || echo "link FAILED"
