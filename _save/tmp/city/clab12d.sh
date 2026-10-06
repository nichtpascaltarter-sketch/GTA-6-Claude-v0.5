#!/bin/bash
# batch 9 (dev12) after the upper-room and signature edits: citylab build, stats, perf, far LOD, views
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab12d.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
st "citylab build"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev12 citylab.cpp -o citylab_dev12 -lpthread > /tmp/city/clab12d_build.log 2>&1
rc=$?; st "citylab build rc=$rc"; [ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5
timeout 900 nice -n 5 $S/citylab/citylab_dev12 --logmetric --stats --find 20,-991,-624 > /tmp/city/clab12d_stats.txt 2>&1
st "stats done"
mkdir -p $S/city/clay_b9b
timeout 900 nice -n 5 $S/citylab/citylab_dev12 --views $S/citylab/views_b9b.txt $S/city/clay_b9b > /tmp/city/clab12d_views.txt 2>&1
st "views done"
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev12 /tmp/city/clab12d_perf.txt
timeout 900 nice -n 5 $S/citylab/citylab_dev12 --farbreak > /tmp/city/clab12d_far.txt 2>&1
st "perf far done"
