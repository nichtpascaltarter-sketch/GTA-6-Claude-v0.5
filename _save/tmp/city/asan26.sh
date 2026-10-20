#!/bin/bash
# ASan + bounds worldcheck on $S/dev26 (batches 17 + 18 world side; corners 1.1 m), with the beacon-phase and bag checks.
# No -g, memfree >= 4500 for the compile, one compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq26.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "asan26 start"
wmem 4500
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -DWC_BEACONS -I$S/dev26 worldcheck.cpp -o wc_dev26_asan -lpthread > /tmp/city/wc_dev26_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"
if [ $rc -eq 0 ]; then
  wmem 3000
  cd /home/user/GTA-6-Claude-v0.5 && FP_BEACONS=1 FP_BAGS="2607,4300" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev26_asan 12 > /tmp/city/wc_dev26_asan.txt 2>&1
  st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev26_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev26_asan.txt) passed; $(grep -o 'aviation lights: .*' /tmp/city/wc_dev26_asan.txt); $(grep -o 'trash bag piles: [0-9]* on the map.*' /tmp/city/wc_dev26_asan.txt))"
fi
rm -f $S/wc/wc_dev26_asan
st "asan26 done"
