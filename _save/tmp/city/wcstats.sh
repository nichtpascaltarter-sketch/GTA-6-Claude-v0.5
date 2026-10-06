#!/bin/bash
# rebuild before/after worldchecks with the garage counts and run both (light runs)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wcfp.status; }
for pair in "dev14 fp_b4" "dev15 fp_a4"; do
  set -- $pair; T=$1; TAG=$2
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
  cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/$T worldcheck.cpp -o wc_$TAG -lpthread > /tmp/city/wc_${TAG}_build.log 2>&1
  rc=$?; st "wc build ($T $TAG) rc=$rc"; [ $rc -eq 0 ] || continue
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1200 ]; do sleep 5; done
  cd /home/user/GTA-6-Claude-v0.5 && FP_SHOTS=1 nice -n 5 $S/wc/wc_$TAG 20 > $S/wc/$TAG.txt 2>&1
  st "wc run ($T $TAG) exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/$TAG.txt)"
done
st "wcstats done"
