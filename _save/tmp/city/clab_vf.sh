#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab_vf.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev20 citylab.cpp -o citylab_dev20 -lpthread > /tmp/city/clab_dev20_build.log 2>&1
st "build rc=$?"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1200 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev20 --villaforms 4500,-4200 --villaforms 1963,-3500 --villaforms -1207,-593 > /tmp/city/villaforms.txt 2>&1
st "run exit $?"
