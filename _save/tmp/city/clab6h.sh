#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab6h.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st start
waitmem 2500
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev7 citylab.cpp -o citylab_dev6y -lpthread > /tmp/city/clab6y_build.log 2>&1
rc=$?; st "build rc=$rc"
[ $rc -eq 0 ] || exit 1
cp $S/citylab/citylab_dev6y $S/citylab/citylab_dev6
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev6y --gardens > /tmp/city/clab6_gardens.txt 2>&1
st "gardens rc=$?"
waitmem 2500
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev6y /tmp/city/clab_dev6_perf.txt
st "perf done"
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev6y --yardtrees --stats --farbreak > /tmp/city/clab_dev6_report.txt 2>&1
st "report rc=$?"
waitmem 2500
for v in views_b6 views_b6b views_b6c; do timeout 900 nice -n 5 $S/citylab/citylab_dev6y --views $S/citylab/$v.txt $S/city/clay_b6 > /tmp/city/clab6_$v.txt 2>&1; done
st "views done"
st "clab6h done"
