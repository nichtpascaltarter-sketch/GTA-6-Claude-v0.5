#!/bin/bash
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -I$S/dev7 citylab.cpp -o citylab_dev7 -lpthread > /tmp/city/clab7_build.log 2>&1
rc=$?; echo "native build rc=$rc $(date)" > /tmp/city/pre7.status
[ $rc -eq 0 ] || exit 1
cd /home/user/GTA-6-Claude-v0.5 && timeout 900 nice -n 5 $S/citylab/citylab_dev7 --yardtrees --frontkinds > /tmp/city/clab_dev7_report2.txt 2>&1
echo "report rc=$? $(date)" >> /tmp/city/pre7.status
rm -f /tmp/city/jobs7.status
exec /tmp/city/jobs7.sh
