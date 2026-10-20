#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "perf far done" /tmp/city/clab12d.status 2>/dev/null; do sleep 10; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p $S/city/clay_b9c $S/city/clay_b9c_before
timeout 900 nice -n 5 $S/citylab/citylab_dev12 --views $S/citylab/views_b9c.txt $S/city/clay_b9c > /tmp/city/clab12e.txt 2>&1
timeout 900 nice -n 5 $S/citylab/citylab_dev10 --views $S/citylab/views_b9c.txt $S/city/clay_b9c_before >> /tmp/city/clab12e.txt 2>&1
echo "medf views done $(date)" >> /tmp/city/clab12d.status
