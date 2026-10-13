#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wc10b.status; }
until grep -q "stats perf done\|build rc=[1-9]" /tmp/city/clab10f.status 2>/dev/null; do sleep 10; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev10 worldcheck.cpp -o wc_dev10 -lpthread > /tmp/city/wc_dev10_build.log 2>&1
rc=$?; st "dev10 build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 3000 nice -n 5 $S/wc/wc_dev10 12 > $S/wc/dev10.txt 2>&1
echo "exit $?" >> $S/wc/dev10.txt
st "dev10 wc done: $(grep -c 'worldcheck: passed' $S/wc/dev10.txt) passed"
