#!/bin/bash
# ASan + bounds worldcheck on the final tree $S/dev23 (batches 12 + 14 + 15 with bag pile v2). No -g, memfree >= 4500
# for the compile, one compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/asan23.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 4500
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -I$S/dev23 worldcheck.cpp -o wc_dev23_asan -lpthread > /tmp/city/wc_dev23_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"
if [ $rc -eq 0 ]; then
  wmem 3000
  cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev23_asan 12 > /tmp/city/wc_dev23_asan.txt 2>&1
  st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev23_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev23_asan.txt) passed)"
fi
rm -f $S/wc/wc_dev23_asan
st "asan23 done"
