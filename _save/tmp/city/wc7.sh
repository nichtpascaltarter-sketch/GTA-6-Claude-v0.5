#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev7 worldcheck.cpp -o wc_dev7 -lpthread > /tmp/city/wc7_build.log 2>&1
echo "build rc=$? $(date)" > /tmp/city/wc7.status
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 3000 nice -n 5 $S/wc/wc_dev7 12 > $S/wc/dev7.txt 2>&1
echo "exit $?" >> $S/wc/dev7.txt
echo "wc done $(date)" >> /tmp/city/wc7.status
