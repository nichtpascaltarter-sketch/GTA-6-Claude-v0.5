#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab6p.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st start
waitmem 2500
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev6 /tmp/city/clab_dev6_perf.txt
st "perf done"
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev6 --yardtrees --stats --farbreak > /tmp/city/clab_dev6_report.txt 2>&1
st "report done rc=$?"
st "clab6p done"
