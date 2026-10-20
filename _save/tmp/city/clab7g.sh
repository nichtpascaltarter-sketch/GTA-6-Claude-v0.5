#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab7g.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev7 citylab.cpp -o citylab_dev7 -lpthread > /tmp/city/clab7_build.log 2>&1
rc=$?; st "build rc=$rc"; [ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev7 --gardens > /tmp/city/clab7_gardens.txt 2>&1
st "gardens rc=$?"
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev7 /tmp/city/clab_dev7_perf.txt
st "perf done"
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev7 --yardtrees --stats --farbreak --frontkinds > /tmp/city/clab_dev7_report.txt 2>&1
st "report rc=$?"
for v in views_b6 views_b6b views_b6c; do timeout 900 nice -n 5 $S/citylab/citylab_dev7 --views $S/citylab/$v.txt $S/city/clay_b6 > /tmp/city/clab7_$v.txt 2>&1; done
st "views done"
st "clab7g done"
