#!/bin/bash
# batch 11b (garageIntrudes reads garageKind: no phantom wings) in $S/dev15b = 590854d's world (batch 11) + interiors.cpp:
# worldcheck (interior hosts before/after), MinGW syntax, clang GNU-extension scan, nt_dev15b.exe; then the story
# regression (and the stick-up encounter) on it. One compile and one game of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/cq11b.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev15b worldcheck.cpp -o wc_dev15b -lpthread > /tmp/city/wc_dev15b_build.log 2>&1; st "wc_dev15b build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && nice -n 5 $S/wc/wc_dev15b 20 > $S/wc/dev15b.txt 2>&1
st "wc_dev15b run exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/dev15b.txt); $(grep -c 'worldcheck: passed' $S/wc/dev15b.txt) passed"
rm -f $S/wc/wc_dev15b
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I$S/dev15b worldcheck.cpp > /tmp/city/msvcproxy_dev15b.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev15b.log) diagnostics"
cd $S/dev15b && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev15b && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev15b.log 2>&1
if grep -q "error" /tmp/city/syntax_dev15b.log; then st "dev15b syntax errors"; exit 1; fi
st "dev15b syntax ok ($(grep -c warning /tmp/city/syntax_dev15b.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev15b.exe
cd $S/dev15b && { time OUT=/tmp/city/nt_dev15b.exe sh ./build.sh ; } > /tmp/city/build_dev15b.log 2>&1; st "dev15b build rc=$?"
[ -f /tmp/city/nt_dev15b.exe ] && st "dev15b exe ready" || { st "no dev15b exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt 3300 ]; do sleep 20; done; }
st "start dev15b regression"
gate; TIMEOUT=9000 EXE=/tmp/city/nt_dev15b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev15b\' > /tmp/city/reg_dev15b.out 2>&1
cp $L /tmp/city/reg_dev15b_log.txt; rm -rf /tmp/city/reg_dev15b
st "dev15b regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev15b_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev15b.out) segfaults"
st "start stick-up encounter (dev15b)"
mkdir -p /tmp/city/enc_dev15b
gate; TIMEOUT=3600 EXE=/tmp/city/nt_dev15b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest enc_stickup,enc_stickup_b --renderevery 60 --width 960 --height 540 --shotdir 'Z:\tmp\city\enc_dev15b\' > /tmp/city/enc_dev15b.out 2>&1
cp $L /tmp/city/enc_dev15b_log.txt
st "dev15b stick-up done: $(grep -c 'PASSED - outcome' /tmp/city/enc_dev15b_log.txt) passed, $(grep -c 'FAILED' /tmp/city/enc_dev15b_log.txt) failed, $(grep -c 'Segmentation' /tmp/city/enc_dev15b.out) segfaults"
st "cq11b done"
