#!/bin/bash
# ring triangle counts (buildings and open lots) before (dev9 = batches 5-7) and after (dev10 = + batch 8)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab10p.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
st "build dev9b"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev9 citylab.cpp -o citylab_dev9b -lpthread > /tmp/city/clab9b_build.log 2>&1 || { st "dev9b build failed"; exit 1; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
st "build dev10"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -I$S/dev10 citylab.cpp -o citylab_dev10 -lpthread > /tmp/city/clab10_build.log 2>&1 || { st "dev10 build failed"; exit 1; }
st "perf dev9b"
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev9b /tmp/city/clab9b_perf.txt
st "perf dev10"
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev10 /tmp/city/clab10_perf.txt
st "done"
