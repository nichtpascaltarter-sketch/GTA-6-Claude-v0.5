#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_GARDENS -I$S/dev4 citylab.cpp -o citylab_main_g -lpthread > /tmp/city/clabmg_build.log 2>&1 && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev7 citylab.cpp -o citylab_dev7m -lpthread > /tmp/city/clab7m_build.log 2>&1
echo "builds rc=$? $(date)" > /tmp/city/clabmat.status
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_main_g --gardens > /tmp/city/clab_main_gardens.txt 2>&1; timeout 900 nice -n 5 $S/citylab/citylab_dev7m --gardens > /tmp/city/clab7m_gardens.txt 2>&1
echo "done $(date)" >> /tmp/city/clabmat.status
