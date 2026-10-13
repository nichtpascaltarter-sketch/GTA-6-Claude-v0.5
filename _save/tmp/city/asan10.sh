#!/bin/bash
# ASan + bounds builds of worldcheck (every full-detail cell) and citylab (--farbreak: every far cell) on the batch 8 tree
# (dev10 = batches 6 + 8): looking for memory errors behind the segfault in the first batch 6 regression
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/asan10.status; }
until grep -q "folded: wc done\|folded: wc build rc=[1-9]" /tmp/city/final10.status 2>/dev/null; do sleep 15; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 3000 ]; do sleep 15; done
st "wc asan build"
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address -fsanitize=bounds -I$S/dev10 worldcheck.cpp -o wc_dev10_asan -lpthread > /tmp/city/wc_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0 timeout 5400 nice -n 5 $S/wc/wc_dev10_asan 12 > /tmp/city/wc_dev10_asan.txt 2>&1
st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev10_asan.txt) reports)"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 3000 ]; do sleep 15; done
st "citylab asan build"
cd $S/citylab && nice -n 5 g++ -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address -fsanitize=bounds -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -I$S/dev10 citylab.cpp -o citylab_dev10_asan -lpthread > /tmp/city/clab_asan_build.log 2>&1
rc=$?; st "citylab asan build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0 timeout 5400 nice -n 5 $S/citylab/citylab_dev10_asan --farbreak > /tmp/city/clab_dev10_asan.txt 2>&1
st "citylab asan farbreak exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/clab_dev10_asan.txt) reports)"
st "asan10 done"
