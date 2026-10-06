#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab8.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev8 citylab.cpp -o citylab_dev8 -lpthread > /tmp/city/clab8_build.log 2>&1
rc=$?; st "build rc=$rc"; [ $rc -eq 0 ] || exit 1
mkdir -p $S/city/clay_keys8
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev8 --views $S/citylab/views_keys.txt $S/city/clay_keys8 > /tmp/city/clab8_views.txt 2>&1
st "views rc=$?"
