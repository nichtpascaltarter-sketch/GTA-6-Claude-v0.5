#!/bin/bash
# Part 1 so far ($S/p1 = $S/rb + the pilot's streets in world/, hand mode drawing the road cells undressed, the profile
# points fix, the grading moved out (terrainops.cpp, roadgrade.cpp) with decks, worldc grading, the game applying it).
# The tests, worldv/worldc/rbcheck on world/, MinGW, the census with terrain hashes on rb (base) and p1 (must match),
# the exe, hand mode on the pilot (shots), a procedural drive. One build and one game at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/p1.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
st "cq68 start"
cd $S/p1
wmem 2000
nice -n 5 g++ $CI tests/world/worldmodel_test.cpp -o /tmp/city/p1_wmt > /tmp/city/p1_wmt_build.log 2>&1; st "p1 worldmodel_test build rc=$? ($(grep -c 'error\|warning' /tmp/city/p1_wmt_build.log) error/warning lines)"
/tmp/city/p1_wmt > /tmp/city/p1_wmt_run.log 2>&1; st "p1 worldmodel_test run rc=$?: $(tail -1 /tmp/city/p1_wmt_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/p1_worldc > /tmp/city/p1_worldc_build.log 2>&1; st "p1 worldc build rc=$? ($(grep -c warning /tmp/city/p1_worldc_build.log) warnings)"
/tmp/city/p1_worldc --world=world --out=/tmp/city/p1_world.bin > /tmp/city/p1_worldc_world.log 2>&1; st "p1 worldc on world/ rc=$?: $(grep -E 'roads|graded' /tmp/city/p1_worldc_world.log | tr '\n' ';')"
/tmp/city/worldv --world=world --no-cache > /tmp/city/p1_worldv.log 2>&1; st "worldv on p1 world/ rc=$?: $(tail -1 /tmp/city/p1_worldv.log)"
/tmp/city/ntwfmt --check $(find world -name '*.ntw') > /tmp/city/p1_ntwfmt.log 2>&1; st "ntwfmt --check rc=$?: $(tail -1 /tmp/city/p1_ntwfmt.log)"
wmem 1500
nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I. wmtools/rbcheck.cpp -o /tmp/city/p1_rbcheck > /tmp/city/p1_rbcheck_build.log 2>&1; st "p1 rbcheck build rc=$?"
/tmp/city/p1_rbcheck /tmp/city/p1_world.bin world > /tmp/city/p1_rbcheck.log 2>&1; st "p1 rbcheck rc=$?: $(tail -1 /tmp/city/p1_rbcheck.log)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/p1_mingw.log 2>&1; st "p1 MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/p1_mingw.log) warnings $(grep -c 'warning' /tmp/city/p1_mingw.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tools/worldc.cpp > /tmp/city/p1_mingw_worldc.log 2>&1; st "p1 MinGW worldc rc=$? errors $(grep -c 'error' /tmp/city/p1_mingw_worldc.log) warnings $(grep -c 'warning' /tmp/city/p1_mingw_worldc.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tests/world/worldmodel_test.cpp > /tmp/city/p1_mingw_wmt.log 2>&1; st "p1 MinGW worldmodel_test rc=$? errors $(grep -c 'error' /tmp/city/p1_mingw_wmt.log) warnings $(grep -c 'warning' /tmp/city/p1_mingw_wmt.log)"
touch /tmp/city/cq68_tests.marker
# --- the procedural world must not change: the census with the terrain's hashes, base (rb) and p1
for V in rb p1; do
  cd $S/$V
  wmem 2500
  nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck2.cpp -o /tmp/city/rfcheck2_$V -lpthread > /tmp/city/rfcheck2_${V}_build.log 2>&1; st "rfcheck2_$V build rc=$?"
  wmem 2000
  nice -n 5 /tmp/city/rfcheck2_$V > /tmp/city/census2_$V.txt 2> /tmp/city/census2_${V}_err.txt; st "rfcheck2_$V run rc=$?: $(grep '^H ' /tmp/city/census2_$V.txt)"
done
st "census rb vs p1: record lines $(diff <(grep -E '^[A-Z] ' /tmp/city/census2_rb.txt) <(grep -E '^[A-Z] ' /tmp/city/census2_p1.txt) | grep -c '^[<>]') differ (of $(grep -cE '^[A-Z] ' /tmp/city/census2_p1.txt)); all lines, times masked: $(diff <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census2_rb.txt) <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census2_p1.txt) | grep -c '^[<>]') differ"
touch /tmp/city/cq68_census.marker
# --- the exe, hand mode on the pilot (embedded), a procedural drive
cd $S/p1
wmem 2800
rm -f /tmp/city/nt_p1.exe build/gen/world_res.o build/gen/world.bin
OUT=/tmp/city/nt_p1.exe nice -n 6 ./build.sh > /tmp/city/build_p1.log 2>&1; st "nt_p1 build rc=$?: $(ls -la /tmp/city/nt_p1.exe 2>/dev/null | awk '{print $5}') bytes; $(grep -E 'worldc:.*(roads|graded|world.bin)' /tmp/city/build_p1.log | tr '\n' ';')"
[ -f /tmp/city/nt_p1.exe ] || { st "cq68: no exe"; touch /tmp/city/cq68_done.marker; exit 1; }
until [ -f /tmp/city/cq65_done.marker ]; do sleep 20; done   # (one game of mine at a time)
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/p1smoke; rm -f /tmp/city/p1smoke/*.bmp $L
wmem 3300; TIMEOUT=1200 EXE=/tmp/city/nt_p1.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 1280 --height 720 \
  --shot 1560,-150,220,0,-35,11,p1_aerial --shot 1300,347,5.6,-90,-3,14,p1_luna_street --shot 1222,268,9,160,-10,16,p1_bridge \
  --shot 1440,300,60,-30,-45,12,p1_heroes_junction --shot 1300,347,5.6,-90,-3,21,p1_luna_night --shotdir 'Z:\tmp\city\p1smoke\' > /tmp/city/p1smoke_hand.out 2>&1
rc=$?; cp $L /tmp/city/p1smoke_hand_log.txt 2>/dev/null
st "hand mode on the pilot rc=$rc: $(ls /tmp/city/p1smoke/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/p1smoke_hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/p1smoke_hand_log.txt | head -1)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p1.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\p1smoke\' > /tmp/city/p1smoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/p1smoke_drive_log.txt 2>/dev/null
st "procedural drive smoke rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p1smoke_drive_log.txt) fatal; $(grep -o 'Road network: [0-9]* nodes, [0-9]* edges' /tmp/city/p1smoke_drive_log.txt | head -1); $(grep -E 'autoplay t=' /tmp/city/p1smoke_drive_log.txt | tail -1 | cut -c1-120)"
st "cq68 done"
touch /tmp/city/cq68_done.marker
