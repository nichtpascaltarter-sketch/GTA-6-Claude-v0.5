#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab10d.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
st "start build"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -I$S/dev10 citylab.cpp -o citylab_dev10 -lpthread > /tmp/city/clab10_build.log 2>&1
rc=$?; st "build rc=$rc"; [ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5
timeout 900 nice -n 5 $S/citylab/citylab_dev10 --logmetric --stats --openlots 0,0,1 > /tmp/city/clab10_stats.txt 2>&1
st "stats rc=$?"
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev10 /tmp/city/clab10_perf.txt
st "perf done"
mkdir -p $S/city/clay_b8v $S/city/clay_b8v_before
timeout 900 nice -n 5 $S/citylab/citylab_dev10 --views $S/citylab/views_b8.txt $S/city/clay_b8v > /dev/null 2>&1
timeout 900 nice -n 5 $S/citylab/citylab_dev9b --views $S/citylab/views_b8.txt $S/city/clay_b8v_before > /dev/null 2>&1
st "views done"
