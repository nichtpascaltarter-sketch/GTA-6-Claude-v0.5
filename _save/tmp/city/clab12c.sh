#!/bin/bash
# batch 9 clay views (dev12) and the same views on batch 8 (citylab_dev10) as the before
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab12c.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p $S/city/clay_b9 $S/city/clay_b9_before
timeout 1200 nice -n 5 $S/citylab/citylab_dev12 --views $S/citylab/views_b9.txt $S/city/clay_b9 > /tmp/city/clab12c_views.txt 2>&1
st "after views rc=$?"
timeout 1200 nice -n 5 $S/citylab/citylab_dev10 --views $S/citylab/views_b9.txt $S/city/clay_b9_before > /tmp/city/clab12c_views_before.txt 2>&1
st "before views rc=$?"
