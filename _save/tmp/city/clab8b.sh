#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev8 citylab.cpp -o citylab_dev8 -lpthread > /tmp/city/clab8_build.log 2>&1
rc=$?; echo "build rc=$rc $(date)" > /tmp/city/clab8b.status; [ $rc -eq 0 ] || exit 1
mkdir -p $S/city/clay_keys8b
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev8 --views $S/citylab/views_keys3.txt $S/city/clay_keys8b > /dev/null 2>&1
timeout 900 nice -n 5 $S/citylab/citylab_dev7 --views $S/citylab/views_keys3.txt $S/city/clay_keys7b > /dev/null 2>&1
echo "views done $(date)" >> /tmp/city/clab8b.status
