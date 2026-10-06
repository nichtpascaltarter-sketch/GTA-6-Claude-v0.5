#!/bin/bash
# dev13 with the roads.cpp suffix fix (blob 4569edfb): MinGW check, nt_dev13.exe; then the ASan worldcheck (recovering, no
# -g, memfree >= 4500). One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq13.status; }
sa() { echo "$1 $(date)" >> /tmp/city/asan13b.status; }
st "rebuild with the roads fix: roads.cpp $(git -C /home/user/GTA-6-Claude-v0.5 hash-object $S/dev13/src/world/roads.cpp)"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/dev13 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev13b.log 2>&1
if grep -q "error" /tmp/city/syntax_dev13b.log; then st "dev13b syntax errors"; exit 1; fi
st "dev13b syntax ok ($(grep -c warning /tmp/city/syntax_dev13b.log) warnings)"
cd $S/dev13 && { time OUT=/tmp/city/nt_dev13.exe sh ./build.sh ; } > /tmp/city/build_dev13b.log 2>&1; st "dev13b build rc=$?"
[ -f /tmp/city/nt_dev13.exe ] && st "dev13b exe ready" || { st "no dev13b exe"; exit 1; }
sa "waiting for memfree >= 4500"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 4500 ]; do sleep 20; done
sa "wc asan build (no -g, recover)"
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -I$S/dev13 worldcheck.cpp -o wc_dev13_asan -lpthread > /tmp/city/wc13_asan2_build.log 2>&1
rc=$?; sa "wc asan build rc=$rc"; [ $rc -eq 0 ] || exit 1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 3000 ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev13_asan 12 > /tmp/city/wc_dev13_asan2.txt 2>&1
sa "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev13_asan2.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev13_asan2.txt) passed)"
