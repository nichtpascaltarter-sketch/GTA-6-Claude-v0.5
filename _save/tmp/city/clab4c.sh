#!/bin/bash
# rebuild citylab on the final folded dev4 tree and collect perf, yard trees, courts, stats, twins
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab4c.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
waitmem 2500
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/dev4 citylab.cpp -o citylab_dev4c -lpthread > /tmp/city/clab4c_build.log 2>&1
st "build rc=$?"
[ -x $S/citylab/citylab_dev4c ] || exit 1
waitmem 2500
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev4c /tmp/city/clab_dev4c_perf.txt
st "perf done"
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev4c --yardtrees --courts --stats --farbreak > /tmp/city/clab_dev4c_report.txt 2>&1
st "report done rc=$?"
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev4c --twins 16,9,13,18,19,23,8 > /tmp/city/clab_dev4c_twins.txt 2>&1
st "twins done rc=$?"
st "clab4c done"
