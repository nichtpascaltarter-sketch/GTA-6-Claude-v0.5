#!/bin/bash
# batch 5 (yards) on main 111efa9 ($S/dev7): MinGW check, nt_dev7.exe; after jobsD (the batch-4 tours): the yard shots on
# nt_dev7.exe and the same shots on nt_dev4b.exe (the world of main), then the story regression on nt_dev7.exe
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs7.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
cd $S/dev7 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev7 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev7.log 2>&1
if grep -q "error" /tmp/city/syntax_dev7.log; then st "dev7 syntax errors"; exit 1; fi
st "dev7 syntax ok"
rm -f /tmp/city/nt_dev7.exe
cd $S/dev7 && { time OUT=/tmp/city/nt_dev7.exe sh ./build.sh ; } > /tmp/city/build_dev7.log 2>&1; st "dev7 build rc=$?"
[ -f /tmp/city/nt_dev7.exe ] || { st "no dev7 exe"; exit 1; }
st "dev7 exe ready"
until grep -q "jobsD done\|no dev4b" /tmp/city/jobsD.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev7_shots /tmp/city/dev7_before_shots /tmp/city/reg_dev7
st "start dev7 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev7.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev7_shots\' $(cat /tmp/city/dev7_shot_args.txt) > /tmp/city/dev7_shots.out 2>&1
cp $L /tmp/city/dev7_shots_log.txt
st "start before shots (dev4b)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev7_before_shots\' $(cat /tmp/city/dev7_before_shot_args.txt) > /tmp/city/dev7_before_shots.out 2>&1
cp $L /tmp/city/dev7_before_shots_log.txt
st "start dev7 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev7.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev7\' > /tmp/city/reg_dev7.out 2>&1
cp $L /tmp/city/reg_dev7_log.txt
st "jobs7 done"
