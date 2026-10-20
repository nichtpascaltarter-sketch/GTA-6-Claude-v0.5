#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab9t.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1500 ]; do sleep 15; done
st "start"
cd /home/user/GTA-6-Claude-v0.5 && (timeout 900 nice -n 5 $S/citylab/citylab_dev9 --twins 16,23,1,14,8 > /tmp/city/clab9_twins.txt 2>&1 & echo $! > /tmp/city/clab9t.pid; wait)
st "done rc=$?"
