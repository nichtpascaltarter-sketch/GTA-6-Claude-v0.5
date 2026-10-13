#!/bin/bash
# triangle counts for batch 11's base (dev14 = main b907098 + batch 10) after cq20c (one compile of mine at a time)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab14.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "waiting for cq20c"
until grep -q "cq20c done\|dev20 syntax errors" /tmp/city/cq20.status 2>/dev/null; do sleep 30; done
wmem 2000
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev14 citylab.cpp -o citylab_dev14 -lpthread > /tmp/city/clab_dev14_build.log 2>&1
st "citylab build dev14 rc=$?"
wmem 1500
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev14 /tmp/city/clab_dev14_perf.txt; st "citylab perf dev14 done"
