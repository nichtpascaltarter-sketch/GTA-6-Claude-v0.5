#!/bin/bash
# after the dev4 report: the buildings round tour stop 3 (facade glass and flags) with dev4 (= b3 for towers)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while ! grep -q "dev4 report" /tmp/city/jobsU.status 2>/dev/null; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev4 --dump 3350,-843,45 --dump 5079,1500,40 > /tmp/city/dump_stops.txt 2>&1
echo "exit $?" >> /tmp/city/dump_stops.txt
