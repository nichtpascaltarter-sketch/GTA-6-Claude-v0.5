#!/bin/bash
# building statistics for docs/HANDBUILT_WORLD.md (worldcheck FP_STATS on HEAD's world, $S/dev24), after tb.sh
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/tb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^tb done" /tmp/city/tb.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/tb.sh" > /dev/null; do sleep 20; done
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev24 worldcheck.cpp -o wc_stats -lpthread > /tmp/city/wc_stats_build.log 2>&1; st "stats build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_STATS=1 nice -n 5 $S/wc/wc_stats 20 > $S/wc/stats.txt 2>&1; st "stats run exit $?"
rm -f $S/wc/wc_stats
st "stats done"
