#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab9v.status; }
until grep -q "done" /tmp/city/clab9t.status 2>/dev/null; do sleep 10; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1500 ]; do sleep 15; done
st "start"
mkdir -p $S/city/clay_tour9
cd /home/user/GTA-6-Claude-v0.5 && timeout 1200 nice -n 5 $S/citylab/citylab_dev9 --views $S/citylab/views_tour.txt $S/city/clay_tour9 > /tmp/city/clab9v.txt 2>&1
st "done rc=$?"
