#!/bin/bash
# batch 13 (dev20c: roofs for the aerial views, ranch and villa variety; far LOD trimmed to cap-only parapets) after the
# triangle review: worldcheck (footprints) build and run; MinGW syntax check and nt_dev20.exe (jobs20 waits for it); then
# the ASan worldcheck (no -g, memfree >= 4500). One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq20.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq20c start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev20 worldcheck.cpp -o wc_dev20 -lpthread > /tmp/city/wc_dev20_build.log 2>&1; st "wc_dev20c build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && nice -n 5 $S/wc/wc_dev20 20 > $S/wc/dev20c.txt 2>&1
st "wc_dev20c run exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/dev20c.txt); $(grep -c 'worldcheck: passed' $S/wc/dev20c.txt) passed"
cd $S/dev20 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev20 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev20.log 2>&1
if grep -q "error" /tmp/city/syntax_dev20.log; then st "dev20 syntax errors"; exit 1; fi
st "dev20 syntax ok ($(grep -c warning /tmp/city/syntax_dev20.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev20.exe
cd $S/dev20 && { time OUT=/tmp/city/nt_dev20.exe sh ./build.sh ; } > /tmp/city/build_dev20.log 2>&1; st "dev20 build rc=$?"
[ -f /tmp/city/nt_dev20.exe ] && st "dev20 exe ready" || st "no dev20 exe"
st "waiting for memfree >= 4500 (ASan)"
wmem 4500
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -I$S/dev20 worldcheck.cpp -o wc_dev20_asan -lpthread > /tmp/city/wc_dev20_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"
if [ $rc -eq 0 ]; then
  wmem 3000
  cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev20_asan 12 > /tmp/city/wc_dev20_asan.txt 2>&1
  st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev20_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev20_asan.txt) passed)"
fi
st "cq20c done"
