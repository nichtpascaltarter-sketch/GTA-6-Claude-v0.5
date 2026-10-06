#!/bin/bash
# batch 6 (Keys motels, painted trim, eyebrow conch houses) + batch 7 (side-gabled ranches and block houses, flat-roofed
# Mediterranean houses) in $S/dev9 = main 6afc8b2 + batches 5-7: MinGW check, nt_dev9.exe; after tour5: the shots on
# nt_dev9.exe, the story regression on nt_dev9.exe, then the before shots on nt_dev7.exe (111efa9 + batch 5)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs9.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
cd $S/dev9 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev9 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev9.log 2>&1
if grep -q "error" /tmp/city/syntax_dev9.log; then st "dev9 syntax errors"; exit 1; fi
st "dev9 syntax ok"
rm -f /tmp/city/nt_dev9.exe
cd $S/dev9 && { time OUT=/tmp/city/nt_dev9.exe sh ./build.sh ; } > /tmp/city/build_dev9.log 2>&1; st "dev9 build rc=$?"
[ -f /tmp/city/nt_dev9.exe ] || { st "no dev9 exe"; exit 1; }
st "dev9 exe ready"
until grep -q "tour5 done" /tmp/city/tour5.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev9_shots /tmp/city/dev9_before_shots /tmp/city/reg_dev9
st "start dev9 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev9_shots\' $(cat /tmp/city/dev9_shot_args.txt) > /tmp/city/dev9_shots.out 2>&1
cp $L /tmp/city/dev9_shots_log.txt
st "start dev9 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev9\' > /tmp/city/reg_dev9.out 2>&1
cp $L /tmp/city/reg_dev9_log.txt
st "start before shots (dev7)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev7.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev9_before_shots\' $(cat /tmp/city/dev9_before_shot_args.txt) > /tmp/city/dev9_before_shots.out 2>&1
cp $L /tmp/city/dev9_before_shots_log.txt
st "jobs9 done"
