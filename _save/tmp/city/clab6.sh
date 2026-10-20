#!/bin/bash
# build citylab on dev6 (batch 6: yards) and render the batch-6 views with it and with the batch-4 binary
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab6.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
waitmem 2500
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/dev6 citylab.cpp -o citylab_dev6 -lpthread > /tmp/city/clab6_build.log 2>&1
st "build rc=$?"
[ -x $S/citylab/citylab_dev6 ] && [ $S/citylab/citylab_dev6 -nt $S/dev6/src/world/facadedetail.cpp ] || { st "no binary"; exit 1; }
mkdir -p $S/city/clay_b6 $S/city/clay_b6_before
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev6 --views $S/citylab/views_b6.txt $S/city/clay_b6 > /tmp/city/clab6_views.txt 2>&1
st "views rc=$?"
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev4c --views $S/citylab/views_b6.txt $S/city/clay_b6_before > /tmp/city/clab6_views_before.txt 2>&1
st "before views rc=$?"
st "clab6 done"
