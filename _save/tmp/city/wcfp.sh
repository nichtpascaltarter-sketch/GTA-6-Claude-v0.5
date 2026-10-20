#!/bin/bash
# worldcheck with the footprint-overlap pass on a tree ($1 = tree name under $S, $2 = output tag; FP_NEAR passes through)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$1; TAG=$2
st() { echo "$1 $(date)" >> /tmp/city/wcfp.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
st "wc build ($T $TAG)"
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/$T worldcheck.cpp -o wc_$TAG -lpthread > /tmp/city/wc_${TAG}_build.log 2>&1
rc=$?; st "wc build ($T $TAG) rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 3000 nice -n 5 $S/wc/wc_$TAG 20 > $S/wc/$TAG.txt 2>&1
st "wc run ($T $TAG) exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/$TAG.txt)"
