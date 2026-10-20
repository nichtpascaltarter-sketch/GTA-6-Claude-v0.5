#!/bin/bash
# step 2 after the lead's review (tree $S/s0b = HEAD 0c288db + step 1 + step 2 + the asks): worldc (atomic writes, hash
# over the table), the host load test (a damaged table entry refused), MinGW syntax, the exe through build.sh, the
# skip test of build.sh's world steps; then the games: 4 hand-mode shots (no vegetation) and the drive and melee smoke.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/s0b.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq39 start"
wmem 2000
cd $S/s0b && mkdir -p build/gen && nice -n 5 g++ -O2 -std=c++17 -Wall -I. tools/worldc.cpp -o build/worldc > $S/s0test/worldc_s0b.log 2>&1; st "worldc build rc=$? ($(grep -c warning $S/s0test/worldc_s0b.log) warnings)"
cd $S/s0b && ./build/worldc --world=world --out=build/gen/world.bin > $S/s0test/worldc_s0b_run.log 2>&1; st "worldc run rc=$?: $(grep wrote $S/s0test/worldc_s0b_run.log); temp files left: $(ls build/gen | grep -c tmp)"
wmem 2000
cd $S/s0b && nice -n 5 g++ -std=c++17 -O2 -I. $S/s0test/handload.cpp -o $S/s0test/handload_b -lpthread > $S/s0test/handload_b_build.log 2>&1; st "handload build rc=$?"
wmem 1500
cd $S/s0b && $S/s0test/handload_b build/gen/world.bin > $S/s0test/handload_b_run.log 2>&1; st "handload run rc=$?: $(tail -1 $S/s0test/handload_b_run.log); $(grep 'damaged table' $S/s0test/handload_b_run.log)"
rm -f $S/s0test/handload_b
cd $S/s0b && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/s0b && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_s0b.log 2>&1; st "s0b game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_s0b.log) warnings=$(grep -c 'warning' /tmp/city/syntax_s0b.log)"
rm -f $S/s0b/build/gen/world_res.o $S/s0b/build/gen/world.bin
wmem 2800
cd $S/s0b && OUT=/tmp/city/nt_s0b.exe nice -n 6 ./build.sh > /tmp/city/build_s0b.log 2>&1; st "nt_s0b build rc=$?: $(ls -la /tmp/city/nt_s0b.exe 2>/dev/null | awk '{print $5}') bytes; $(grep -c 'worldc: wrote' /tmp/city/build_s0b.log) worldc run(s)"
# build.sh's world steps again, alone: nothing is newer than the resource, so both are skipped
T0=$(stat -c %Y $S/s0b/build/gen/world_res.o)
cd $S/s0b && sh -c 'WORLDC_SRC="tools/worldc.cpp"; if [ ! -f build/gen/world_res.o ] || [ build/worldc -nt build/gen/world_res.o ] || [ -n "$(find world -newer build/gen/world_res.o 2>/dev/null)" ]; then echo REDO; else echo SKIP; fi' > /tmp/city/skip_s0b.txt
touch $S/s0b/world/terrain/terrain.ntw
cd $S/s0b && sh -c 'if [ ! -f build/gen/world_res.o ] || [ build/worldc -nt build/gen/world_res.o ] || [ -n "$(find world -newer build/gen/world_res.o 2>/dev/null)" ]; then echo REDO; else echo SKIP; fi' >> /tmp/city/skip_s0b.txt
st "build.sh skip test: unchanged world -> $(sed -n 1p /tmp/city/skip_s0b.txt), touched terrain.ntw -> $(sed -n 2p /tmp/city/skip_s0b.txt) (resource mtime $T0 -> $(stat -c %Y $S/s0b/build/gen/world_res.o))"
[ -f /tmp/city/nt_s0b.exe ] || { st "cq39: no exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/s0hand; rm -f /tmp/city/s0hand/*.bmp; rm -f $L
wmem 3300; TIMEOUT=900 EXE=/tmp/city/nt_s0b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 \
  --shot 1780,160,8,90,-8,11,hand_calleluna_street --shot 3000,-300,320,0,-30,11,hand_downtown_aerial \
  --shot 4280,-1700,260,0,-35,11,hand_port_isle --shot -2400,4300,420,0,-12,11,hand_lake_ridge \
  --shotdir 'Z:\tmp\city\s0hand\' > /tmp/city/s0hand.out 2>&1
rc=$?; cp $L /tmp/city/s0hand_log.txt 2>/dev/null
st "hand shots rc=$rc: $(ls /tmp/city/s0hand/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/s0hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/s0hand_log.txt | head -1)"
mkdir -p /tmp/city/s0smoke; rm -f $L
wmem 3300; TIMEOUT=3000 EXE=/tmp/city/nt_s0b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\s0smoke\' > /tmp/city/s0smoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/s0smoke_drive_log.txt 2>/dev/null
st "smoke drive rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/s0smoke_drive_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/s0smoke_drive_log.txt | tail -1 | cut -c1-120)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_s0b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 640 --height 360 --play --autoplay melee --meleeweapon bat --autoduration 18 --autoevery 6 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\s0smoke\' > /tmp/city/s0smoke_melee.out 2>&1
rc=$?; cp $L /tmp/city/s0smoke_melee_log.txt 2>/dev/null
st "smoke melee rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/s0smoke_melee_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/s0smoke_melee_log.txt | tail -1 | cut -c1-120)"
st "cq39 done"
