#!/bin/bash
# after the batch-3 build: build citylab with the paved-ground queries and test them
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while ! grep -q "b3 build done\|b3 syntax errors" /tmp/city/buildq.status 2>/dev/null; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/b2 citylab.cpp -o citylab_b2p -lpthread > /tmp/city/pavedq_build.log 2>&1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_b2p --pavedtest > /tmp/city/pavedtest.txt 2>&1
echo "exit $?" >> /tmp/city/pavedtest.txt
