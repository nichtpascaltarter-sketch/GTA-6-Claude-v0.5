#!/bin/bash
# batch 8 (frontage fill, temple-front conch houses, Keys motels with an office end) in $S/dev10 = 6afc8b2 + batches 5-8:
# after jobs9's build: MinGW check, nt_dev10.exe; after jobs9: the batch-8 shots on nt_dev10.exe and on nt_dev9.exe
# (before), then the story regression on nt_dev10.exe
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs10.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
until grep -q "dev9 build rc\|dev9 syntax errors" /tmp/city/jobs9.status 2>/dev/null; do sleep 30; done
cd $S/dev10 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev10 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev10.log 2>&1
if grep -q "error" /tmp/city/syntax_dev10.log; then st "dev10 syntax errors"; exit 1; fi
st "dev10 syntax ok ($(grep -c warning /tmp/city/syntax_dev10.log) warnings)"
rm -f /tmp/city/nt_dev10.exe
cd $S/dev10 && { time OUT=/tmp/city/nt_dev10.exe sh ./build.sh ; } > /tmp/city/build_dev10.log 2>&1; st "dev10 build rc=$?"
[ -f /tmp/city/nt_dev10.exe ] || { st "no dev10 exe"; exit 1; }
st "dev10 exe ready"
until grep -q "jobs9 done" /tmp/city/jobs9.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev10_shots /tmp/city/dev10_before_shots /tmp/city/reg_dev10
st "start dev10 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev10_shots\' $(cat /tmp/city/dev10_shot_args.txt) > /tmp/city/dev10_shots.out 2>&1
cp $L /tmp/city/dev10_shots_log.txt
st "start dev10 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev10\' > /tmp/city/reg_dev10.out 2>&1
cp $L /tmp/city/reg_dev10_log.txt
st "start before shots (dev9)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev10_before_shots\' $(cat /tmp/city/dev10_shot_args.txt) > /tmp/city/dev10_before_shots.out 2>&1
cp $L /tmp/city/dev10_before_shots_log.txt
st "jobs10 done"
