#!/bin/bash
# terrain-bake prototype again (the first run had no /usr/bin/time), then the census with pilot-area counts. One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/tb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "tb2 start"
wmem 2000
cd $S/tb && nice -n 5 g++ -std=c++17 -O2 -I$S/proto_tb terrainbake.cpp -o terrainbake -lpthread > build.log 2>&1; st "build rc=$?"
wmem 1500
cd $S/tb && rm -f tb_* && { time ./terrainbake ; } > run.log 2>&1; st "run exit $?"
cd $S/tb && for f in tb_*; do printf "%s raw %d gzip9 %d\n" $f $(stat -c %s $f) $(gzip -9 -c $f | wc -c); done > sizes.txt
rm -f $S/tb/tb_* $S/tb/terrainbake
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev24 worldcheck.cpp -o wc_stats -lpthread > /tmp/city/wc_stats_build.log 2>&1; st "stats build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_STATS=1 FP_RADIUS=1560,350,600 nice -n 5 $S/wc/wc_stats 20 > $S/wc/stats2.txt 2>&1
FP_STATS=1 FP_RADIUS=1560,350,400 nice -n 5 $S/wc/wc_stats 20 > $S/wc/stats3.txt 2>&1; st "stats runs exit $?"
rm -f $S/wc/wc_stats
st "tb2 done"
