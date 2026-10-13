#!/bin/bash
# how many buildings garageIntrudes flags, before (the seed's wing) and after batch 11b, on dev15b; after nt_dev15b is built
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wc11b.status; }
until grep -q "dev15b exe ready\|no dev15b exe\|dev15b syntax errors" /tmp/city/cq11b.status 2>/dev/null; do sleep 30; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev15b worldcheck.cpp -o wc_dev15b -lpthread > /tmp/city/wc_dev15b_build2.log 2>&1; st "build rc=$?"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1200 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && FP_GARAGEINTR=1 nice -n 5 $S/wc/wc_dev15b 20 > $S/wc/dev15b_intr.txt 2>&1
st "run exit $?: $(grep 'garageIntrudes:' $S/wc/dev15b_intr.txt)"
rm -f $S/wc/wc_dev15b
