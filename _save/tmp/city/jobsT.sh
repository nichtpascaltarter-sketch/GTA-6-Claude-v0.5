#!/bin/bash
# citylab b3: the twins of the weakest districts, the forecourt heights (memory permitting, between jobsS steps)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 3000 ] || pgrep -f "citylab/citylab_|wc/wc_b3|g\+\+ .*citylab.cpp|g\+\+ .*worldcheck.cpp" > /dev/null; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_b3 --twins 13,18,19,23,8,16 --courts > /tmp/city/twins_b3.txt 2>&1
echo "exit $?" >> /tmp/city/twins_b3.txt
