#!/bin/bash
# citylab on dev16 (batch 12 rooftops / balconies, the ranch colours, the bags): after the dev15 game build, compile,
# triangle counts at the tour stops (vs dev15), clay aerial views
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab16.status; }
st "waiting for the dev15 build"
until grep -q "dev15 exe ready\|no dev15 exe\|dev15 syntax errors" /tmp/city/cq15.status 2>/dev/null; do sleep 20; done
for T in dev16 dev15; do
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
  st "citylab build $T"
  cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/$T citylab.cpp -o citylab_$T -lpthread > /tmp/city/clab_${T}_build.log 2>&1
  rc=$?; st "citylab build $T rc=$rc"; [ $rc -eq 0 ] || { st "citylab $T build failed"; continue; }
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1500 ]; do sleep 10; done
  sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_$T /tmp/city/clab_${T}_perf.txt
  st "citylab perf $T done"
done
st "clab16 done"
