#!/bin/bash
# after the b3 worldcheck build: rebuild citylab_dev4 (yard-tree relocation), count the yard trees, find the batch-4
# buildings for the clay views
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/jobsU.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
while ! grep -q "wc_b3 built" /tmp/city/jobsS.status; do sleep 15; done
waitmem 2500
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/dev4 citylab.cpp -o citylab_dev4 -lpthread > /tmp/city/clab_dev4_build.log 2>&1; st "citylab_dev4 rebuilt rc=$?"
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev4 --yardtrees --courts --twins 13,18,19,23,8,16 --find 52,2495,4012 --find 52,-1317,-845,1 --find 53,144,6475,1 --find 53,4089,7290,0 --find 53,-4886,5241,1 --find 22,2495,4012 > /tmp/city/dev4_report.txt 2>&1; st "dev4 report rc=$?"
