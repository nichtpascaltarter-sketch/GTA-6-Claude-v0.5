#!/bin/bash
# rebuild the before/after worldchecks (FP_ALLBOX listing), then the near runs ($1 tag, $2 x,y,r)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wcfp.status; }
for pair in "dev14 fp_b" "dev15 fp_a"; do
  set -- $pair; T=$1; TAG=$2
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
  cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/$T worldcheck.cpp -o wc_$TAG -lpthread > /tmp/city/wc_${TAG}_build.log 2>&1
  st "wc rebuild ($T $TAG) rc=$?"
done
st "wcnear2 built"
