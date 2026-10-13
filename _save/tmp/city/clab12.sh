#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab12.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
st "start build"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -I$S/dev12 citylab.cpp -o citylab_dev12 -lpthread > /tmp/city/clab12_build.log 2>&1
rc=$?; st "build rc=$rc"; [ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5
timeout 900 nice -n 5 $S/citylab/citylab_dev12 --find 24,-8150,-9400 --find 24,-8500,-2080 --find 24,-8800,-9300 --find 24,-7900,-9300 > /tmp/city/clab12_find.txt 2>&1
st "find done"
