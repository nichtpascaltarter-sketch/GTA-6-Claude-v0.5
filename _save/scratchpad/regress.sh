#!/bin/bash
# harness regression over hotspot locations: 2 at a time, memgated
cd /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
OUT=../regress
mkdir -p $OUT
run() { ../memgate.sh 1800; timeout 1200 nice -n 10 ./traffic_sim --sim $1 $2 220 --cars 60 --peds 40 --minutes ${3:-3} -v > $OUT/r_$1_$2.txt 2>&1; }
set -- "3093 1600" "3300 3900" "2713 763" "3275 1200" "3275 400" "979 1300" "1850 -629" "3050 -629" "979 -200" "5000 1250" "1742 359" "2750 1529" "4000 -160" "1650 2361"
for a in "$@"; do
  run $a &
  sleep 20
  while [ $(jobs -r | wc -l) -ge 2 ]; do sleep 5; done
done
wait
for f in $OUT/r_*.txt; do
  printf "%-22s " $(basename $f .txt)
  grep -E "^collisions" $f | awk '{printf "col %s hard %s ", $2, $8}'
  grep -E "^stuck" $f | sed -E 's/stuck recoveries: ([0-9]+), three-point turns: ([0-9]+), relocalizations: ([0-9]+), deadlocks \(>60 s waits\): ([0-9]+), respawns: ([0-9]+), lifted off ledges: ([0-9]+)/stuck \1 kturn \2 reloc \3 dead \4 unhung \6/'
done
