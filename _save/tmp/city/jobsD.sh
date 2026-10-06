#!/bin/bash
# batch 4 (+ the batch-5 work folded in) on main 8ee5699: MinGW check, nt_dev4b.exe, the story regression, the tour stops
# 2-5 on main (nt_m6.exe, built) and on dev4b, the extra shots (gas stations, raised houses, roofs, re-shoots, missing views)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobsD.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
cd $S/dev4 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev4 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev4c.log 2>&1
if grep -q "error" /tmp/city/syntax_dev4c.log; then st "dev4c syntax errors"; exit 1; fi
st "dev4c syntax ok"
rm -f /tmp/city/nt_dev4b.exe
cd $S/dev4 && { time OUT=/tmp/city/nt_dev4b.exe sh ./build.sh ; } > /tmp/city/build_dev4c.log 2>&1; st "dev4c build rc=$?"
if [ ! -f /tmp/city/nt_dev4b.exe ]; then st "no dev4b exe"; exit 1; fi
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/reg_dev4b /tmp/city/tour_m6 /tmp/city/tour_dev4b /tmp/city/dev4c_shots
st "start dev4b regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev4b\' > /tmp/city/reg_dev4b.out 2>&1
cp $L /tmp/city/reg_dev4b_log.txt
st "start dev4c shots"
TIMEOUT=9000 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\dev4c_shots\' $(cat /tmp/city/dev4c_shot_args.txt) > /tmp/city/dev4c_shots.out 2>&1
cp $L /tmp/city/dev4c_shots_log.txt
st "start m6 tour"
TIMEOUT=5400 EXE=/tmp/city/nt_m6.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_m6\' > /tmp/city/tour_m6.out 2>&1
cp $L /tmp/city/tour_m6_log.txt
st "start dev4b tour"
TIMEOUT=5400 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev4b\' > /tmp/city/tour_dev4b.out 2>&1
cp $L /tmp/city/tour_dev4b_log.txt
st "jobsD done"
