#!/bin/bash
# batches 12 + 14 (storefront windows only on street walls; soft trash bags) in $S/dev22 = dev20 (batches 11 + 13) + the
# change, after clab21 (one compile of mine at a time): worldcheck (footprints, lanes, the bag piles near the NPS corner),
# MinGW syntax check, nt_dev22.exe, ASan worldcheck (no -g, memfree >= 4500)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq22.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "waiting for clab21"
until grep -q "clab21 done" /tmp/city/clab21.status 2>/dev/null; do sleep 30; done
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev22 worldcheck.cpp -o wc_dev22 -lpthread > /tmp/city/wc_dev22_build.log 2>&1; st "wc_dev22 build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_BAGS="2607,4300" nice -n 5 $S/wc/wc_dev22 20 > $S/wc/dev22.txt 2>&1
st "wc_dev22 run exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/dev22.txt); $(grep -c 'worldcheck: passed' $S/wc/dev22.txt) passed"
cd $S/dev22 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev22 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev22.log 2>&1
if grep -q "error" /tmp/city/syntax_dev22.log; then st "dev22 syntax errors"; exit 1; fi
st "dev22 syntax ok ($(grep -c warning /tmp/city/syntax_dev22.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev22.exe
cd $S/dev22 && { time OUT=/tmp/city/nt_dev22.exe sh ./build.sh ; } > /tmp/city/build_dev22.log 2>&1; st "dev22 build rc=$?"
[ -f /tmp/city/nt_dev22.exe ] && st "dev22 exe ready" || st "no dev22 exe"
rm -rf $S/dev22/build/embed_shaders
st "waiting for memfree >= 4500 (ASan)"
wmem 4500
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -I$S/dev22 worldcheck.cpp -o wc_dev22_asan -lpthread > /tmp/city/wc_dev22_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"
if [ $rc -eq 0 ]; then
  wmem 3000
  cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev22_asan 12 > /tmp/city/wc_dev22_asan.txt 2>&1
  st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev22_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev22_asan.txt) passed)"
  rm -f $S/wc/wc_dev22_asan
fi
st "cq22 done"
