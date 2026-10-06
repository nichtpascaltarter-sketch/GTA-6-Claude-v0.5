#!/bin/bash
# ASan + bounds worldcheck on dev15 (batch 11) after clab14 (one compile of mine at a time): no -g, the compile only once
# memfree >= 4500
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/asan15.status; }
st "waiting for clab14"
until grep -q "citylab perf dev14 done\|citylab build dev14 rc=[1-9]" /tmp/city/clab14.status 2>/dev/null; do sleep 30; done
st "waiting for memfree >= 4500"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 4500 ]; do sleep 20; done
st "wc asan build (no -g)"
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -I$S/dev15 worldcheck.cpp -o wc_dev15_asan -lpthread > /tmp/city/wc_dev15_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 3000 ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev15_asan 12 > /tmp/city/wc_dev15_asan.txt 2>&1
st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev15_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev15_asan.txt) passed)"
st "asan15 done"
