#!/bin/bash
# roomlight.h ($S/rlt) and roads into world.bin ($S/rb): the tests with CI flags, worldc on world/ and on a temp world with
# roads, rbcheck (worldc's world.bin read back = compileRoads), MinGW syntax, the exe, the game in hand mode on the roads
# world.bin, and a procedural drive smoke. One build and one game at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/rb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
st "cq65 start"
# --- roomlight.h
cd $S/rlt
wmem 2000
nice -n 5 g++ $CI tests/world/worldmodel_test.cpp -o /tmp/city/rlt_wmt > /tmp/city/rlt_wmt_build.log 2>&1; st "rlt worldmodel_test build rc=$? ($(grep -c 'error\|warning' /tmp/city/rlt_wmt_build.log) error/warning lines)"
/tmp/city/rlt_wmt > /tmp/city/rlt_wmt_run.log 2>&1; st "rlt worldmodel_test run rc=$?: $(tail -1 /tmp/city/rlt_wmt_run.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tests/world/worldmodel_test.cpp > /tmp/city/rlt_mingw.log 2>&1; st "rlt MinGW worldmodel_test rc=$? errors $(grep -c 'error' /tmp/city/rlt_mingw.log) warnings $(grep -c 'warning' /tmp/city/rlt_mingw.log)"
touch /tmp/city/cq65_rlt.marker
# --- roads into world.bin
cd $S/rb
wmem 2000
nice -n 5 g++ $CI tests/world/worldmodel_test.cpp -o /tmp/city/rb_wmt > /tmp/city/rb_wmt_build.log 2>&1; st "rb worldmodel_test build rc=$? ($(grep -c 'error\|warning' /tmp/city/rb_wmt_build.log) error/warning lines)"
/tmp/city/rb_wmt > /tmp/city/rb_wmt_run.log 2>&1; st "rb worldmodel_test run rc=$?: $(tail -1 /tmp/city/rb_wmt_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/rb_worldc > /tmp/city/rb_worldc_build.log 2>&1; st "rb worldc build rc=$? ($(grep -c warning /tmp/city/rb_worldc_build.log) warnings)"
/tmp/city/rb_worldc --world=world --out=/tmp/city/rb_world.bin > /tmp/city/rb_worldc_world.log 2>&1; st "rb worldc on world/ rc=$?: $(tail -1 /tmp/city/rb_worldc_world.log); vs 4.1's: $(cmp /tmp/city/rb_world.bin $S/s41/build/gen/world.bin && echo identical)"
# a temp world: the real terrain plus the model fixture's roads, districts, trunk, signal and transit files
T=/tmp/city/rbworld; rm -rf $T; mkdir -p $T
cp -r world/terrain $T/terrain
F=tests/world/fixtures/model
for f in world.ntw districts/cl/district.ntw districts/cl/roads.ntw districts/sb/district.ntw districts/sb/roads.ntw trunk/roads.ntw lib/signals/fourway.ntw transit/bus.1.ntw; do mkdir -p $T/$(dirname $f); cp $F/$f $T/$f; done
/tmp/city/rb_worldc --world=$T --out=/tmp/city/rbroads_world.bin > /tmp/city/rb_worldc_roads.log 2>&1; st "rb worldc on the roads world rc=$?: $(grep 'roads' /tmp/city/rb_worldc_roads.log | head -1); $(tail -1 /tmp/city/rb_worldc_roads.log)"
mkdir -p $T/districts/cl/blocks/b001; cp $F/districts/cl/blocks/b001/block.ntw $T/districts/cl/blocks/b001/
/tmp/city/rb_worldc --world=$T --out=/tmp/city/rbroads_world_bad.bin > /tmp/city/rb_worldc_block.log 2>&1; st "rb worldc with a block file rc=$? (expect 1): $(grep -c 'no compiler for block' /tmp/city/rb_worldc_block.log) 'no compiler' errors"
rm -rf $T/districts/cl/blocks
wmem 1500
nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I. wmtools/rbcheck.cpp -o /tmp/city/rbcheck > /tmp/city/rbcheck_build.log 2>&1; st "rbcheck build rc=$?"
/tmp/city/rbcheck /tmp/city/rbroads_world.bin $T > /tmp/city/rbcheck.log 2>&1; st "rbcheck rc=$?: $(tail -1 /tmp/city/rbcheck.log)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/rb_mingw.log 2>&1; st "rb MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/rb_mingw.log) warnings $(grep -c 'warning' /tmp/city/rb_mingw.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tools/worldc.cpp > /tmp/city/rb_mingw_worldc.log 2>&1; st "rb MinGW worldc rc=$? errors $(grep -c 'error' /tmp/city/rb_mingw_worldc.log) warnings $(grep -c 'warning' /tmp/city/rb_mingw_worldc.log)"
touch /tmp/city/cq65_tests.marker
# --- the exe, hand mode on the roads world.bin, a procedural drive
wmem 2800
rm -f /tmp/city/nt_rb.exe $S/rb/build/gen/world_res.o $S/rb/build/gen/world.bin
OUT=/tmp/city/nt_rb.exe nice -n 6 ./build.sh > /tmp/city/build_rb.log 2>&1; st "nt_rb build rc=$?: $(ls -la /tmp/city/nt_rb.exe 2>/dev/null | awk '{print $5}') bytes; $(grep 'worldc:.*world.bin' /tmp/city/build_rb.log)"
[ -f /tmp/city/nt_rb.exe ] || { st "cq65: no exe"; touch /tmp/city/cq65_done.marker; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/rbsmoke; rm -f /tmp/city/rbsmoke/*.bmp $L
wmem 3300; TIMEOUT=900 EXE=/tmp/city/nt_rb.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --worldfile='Z:\tmp\city\rbroads_world.bin' --width 960 --height 540 \
  --shot 200,120,40,0,-25,12,rb_hand_roads --shotdir 'Z:\tmp\city\rbsmoke\' > /tmp/city/rbsmoke_hand.out 2>&1
rc=$?; cp $L /tmp/city/rbsmoke_hand_log.txt 2>/dev/null
st "hand mode on the roads world.bin rc=$rc: $(ls /tmp/city/rbsmoke/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/rbsmoke_hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/rbsmoke_hand_log.txt | head -1)"
rm -f $L
wmem 3300; TIMEOUT=900 EXE=/tmp/city/nt_rb.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 \
  --shot 3000,-300,320,0,-30,11,rb_hand_embedded --shotdir 'Z:\tmp\city\rbsmoke\' > /tmp/city/rbsmoke_hand2.out 2>&1
rc=$?; cp $L /tmp/city/rbsmoke_hand2_log.txt 2>/dev/null
st "hand mode on the embedded world.bin rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/rbsmoke_hand2_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/rbsmoke_hand2_log.txt | head -1)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_rb.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\rbsmoke\' > /tmp/city/rbsmoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/rbsmoke_drive_log.txt 2>/dev/null
st "procedural drive smoke rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/rbsmoke_drive_log.txt) fatal; $(grep -o 'Road network: [0-9]* nodes, [0-9]* edges' /tmp/city/rbsmoke_drive_log.txt | head -1); $(grep -E 'autoplay t=' /tmp/city/rbsmoke_drive_log.txt | tail -1 | cut -c1-120)"
st "cq65 done"
touch /tmp/city/cq65_done.marker
