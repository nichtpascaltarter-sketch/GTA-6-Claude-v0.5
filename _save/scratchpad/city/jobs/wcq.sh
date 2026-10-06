#!/bin/bash
# after the paved-ground test: worldcheck on the exact batch-2 sources
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while ! grep -q "^exit" /tmp/city/pavedtest.txt 2>/dev/null; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/b2 worldcheck.cpp -o wc_b2 -lpthread > /tmp/city/wcq_build.log 2>&1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 1800 nice -n 5 $S/wc/wc_b2 12 > $S/wc/b2.txt 2>&1
echo "exit $?" >> $S/wc/b2.txt
