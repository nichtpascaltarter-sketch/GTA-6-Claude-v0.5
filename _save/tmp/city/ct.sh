#!/bin/bash
# per-cell triangles and build times today (worldcheck FP_CELLTRIS on $S/dev28 = HEAD b71d339 + beacon sync), for the
# budgets in docs/HANDBUILT_WORLD.md; after tb3.sh. One build of mine at a time, behind the memory gate.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/tb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^tb3 done" /tmp/city/tb.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/tb3.sh" > /dev/null; do sleep 20; done
st "ct start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev28 worldcheck.cpp -o wc_ct -lpthread > /tmp/city/wc_ct_build.log 2>&1; st "ct build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_CELLTRIS=1 nice -n 5 $S/wc/wc_ct 12 > $S/wc/celltris.txt 2>&1; st "ct run exit $?"
rm -f $S/wc/wc_ct
st "ct done"
