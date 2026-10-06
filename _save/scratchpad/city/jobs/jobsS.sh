#!/bin/bash
# citylab (b3, dev4) with the yard-tree count, worldcheck on b3; one compile at a time, memory permitting
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/jobsS.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done; }
st "start"
waitmem
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/b3 citylab.cpp -o citylab_b3 -lpthread > /tmp/city/clab_b3_build.log 2>&1; st "citylab_b3 built rc=$?"
waitmem
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/dev4 citylab.cpp -o citylab_dev4 -lpthread > /tmp/city/clab_dev4_build.log 2>&1; st "citylab_dev4 built rc=$?"
waitmem
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_b3 --yardtrees --courts > /tmp/city/yardtrees_b3.txt 2>&1; st "yardtrees b3 rc=$?"
waitmem
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev4 --yardtrees > /tmp/city/yardtrees_dev4.txt 2>&1; st "yardtrees dev4 rc=$?"
waitmem
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/b3 worldcheck.cpp -o wc_b3 -lpthread > /tmp/city/wc_b3_build.log 2>&1; st "wc_b3 built rc=$?"
waitmem
cd /home/user/GTA-6-Claude-v0.5 && timeout 1800 nice -n 5 $S/wc/wc_b3 12 > $S/wc/b3.txt 2>&1; echo "exit $?" >> $S/wc/b3.txt
st "jobsS done"
