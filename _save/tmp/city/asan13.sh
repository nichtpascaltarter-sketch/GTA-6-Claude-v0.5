#!/bin/bash
# ASan + bounds worldcheck on dev13 (main d3c2c3f + batches 6, 8, 9): no -g; the compile only once memfree >= 4500 and no
# other compile of mine runs (the coordinator's limits)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/asan13.status; }
st "waiting for memfree >= 4500"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 4500 ]; do sleep 20; done
st "wc asan build (no -g)"
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize=bounds -I$S/dev13 worldcheck.cpp -o wc_dev13_asan -lpthread > /tmp/city/wc13_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 3000 ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0 timeout 5400 nice -n 5 $S/wc/wc_dev13_asan 12 > /tmp/city/wc_dev13_asan.txt 2>&1
st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev13_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev13_asan.txt) passed)"
