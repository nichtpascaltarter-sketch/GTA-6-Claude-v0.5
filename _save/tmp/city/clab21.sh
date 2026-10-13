#!/bin/bash
# whole-map far-LOD building triangles by type (citylab --farbreak), batch 11 (dev15) vs batch 13 (dev20); one compile at a time
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab21.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
for T in dev15 dev20; do
  wmem 2000
  cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/$T citylab.cpp -o citylab_$T -lpthread > /tmp/city/clab_${T}_build.log 2>&1
  st "citylab build $T rc=$?"
  wmem 1200
  cd /home/user/GTA-6-Claude-v0.5 && timeout 1200 nice -n 5 $S/citylab/citylab_$T --farbreak > /tmp/city/farbreak_$T.txt 2>&1
  st "farbreak $T exit $?"
done
st "clab21 done"
