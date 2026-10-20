#!/bin/bash
# Part 1 on 62 plus the roads set ($S/p62): CI's six GCC steps, MinGW, worldc/rbcheck on world/, the census s62 vs p62
# (with the terrain hashes), the exe, hand mode (shots with the game initialised, an autoplay drive), a procedural drive.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/p62.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
until [ -f /tmp/city/cq69_done.marker ]; do sleep 20; done
st "cq70 start"
cd $S/p62
for T in compiler/bit_scan_test updater/update_logic_test world/world_format_test world/worldv_test world/worldmodel_test; do
  n=$(basename $T)
  wmem 2000
  nice -n 5 g++ $CI tests/$T.cpp -o /tmp/city/p62_$n > /tmp/city/p62_${n}_build.log 2>&1; b=$?
  /tmp/city/p62_$n > /tmp/city/p62_${n}_run.log 2>&1; st "CI $n build rc=$b run rc=$?: $(tail -1 /tmp/city/p62_${n}_run.log | cut -c1-100)"
done
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/city/p62_worldv > /tmp/city/p62_worldv_build.log 2>&1; b=$?
/tmp/city/p62_worldv --world=world --no-cache > /tmp/city/p62_worldv_run.log 2>&1; st "CI worldv on world/ build rc=$b run rc=$?: $(tail -1 /tmp/city/p62_worldv_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/p62_worldc > /tmp/city/p62_worldc_build.log 2>&1; st "worldc build rc=$? ($(grep -c warning /tmp/city/p62_worldc_build.log) warnings)"
/tmp/city/p62_worldc --world=world --out=/tmp/city/p62_world.bin > /tmp/city/p62_worldc_world.log 2>&1; st "worldc on world/ rc=$?: $(grep -E 'roads|graded' /tmp/city/p62_worldc_world.log | tr '\n' ';'); vs p1's: $(cmp /tmp/city/p62_world.bin /tmp/city/p1_world.bin && echo identical)"
wmem 1500
nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I. wmtools/rbcheck.cpp -o /tmp/city/p62_rbcheck > /tmp/city/p62_rbcheck_build.log 2>&1; /tmp/city/p62_rbcheck /tmp/city/p62_world.bin world > /tmp/city/p62_rbcheck.log 2>&1; st "rbcheck rc=$?: $(tail -1 /tmp/city/p62_rbcheck.log)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/p62_mingw.log 2>&1; st "MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/p62_mingw.log) warnings $(grep -c 'warning' /tmp/city/p62_mingw.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tools/worldc.cpp > /tmp/city/p62_mingw_worldc.log 2>&1; st "MinGW worldc rc=$? errors $(grep -c 'error' /tmp/city/p62_mingw_worldc.log) warnings $(grep -c 'warning' /tmp/city/p62_mingw_worldc.log)"
touch /tmp/city/cq70_tests.marker
for V in s62 p62; do
  cd $S/$V
  wmem 2500
  nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck2.cpp -o /tmp/city/rfcheck2_$V -lpthread > /tmp/city/rfcheck2_${V}_build.log 2>&1; st "rfcheck2_$V build rc=$?"
  wmem 2000
  nice -n 5 /tmp/city/rfcheck2_$V > /tmp/city/census2_$V.txt 2> /tmp/city/census2_${V}_err.txt; st "rfcheck2_$V run rc=$?: $(grep '^H ' /tmp/city/census2_$V.txt)"
done
st "census s62 vs p62: record lines $(diff <(grep -E '^[A-Z] ' /tmp/city/census2_s62.txt) <(grep -E '^[A-Z] ' /tmp/city/census2_p62.txt) | grep -c '^[<>]') differ (of $(grep -cE '^[A-Z] ' /tmp/city/census2_p62.txt)); all lines, times masked: $(diff <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census2_s62.txt) <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census2_p62.txt) | grep -c '^[<>]') differ"
cd $S/p62
wmem 2800
rm -f /tmp/city/nt_p62.exe build/gen/world_res.o build/gen/world.bin
OUT=/tmp/city/nt_p62.exe nice -n 6 ./build.sh > /tmp/city/build_p62.log 2>&1; st "nt_p62 build rc=$?: $(ls -la /tmp/city/nt_p62.exe 2>/dev/null | awk '{print $5}') bytes; $(grep -E 'worldc:.*(roads|graded)' /tmp/city/build_p62.log | tr '\n' ';')"
[ -f /tmp/city/nt_p62.exe ] || { st "cq70: no exe"; touch /tmp/city/cq70_done.marker; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/p62smoke; rm -f /tmp/city/p62smoke/*.bmp $L
wmem 3300; TIMEOUT=1200 EXE=/tmp/city/nt_p62.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 1280 --height 720 \
  --shot 1560,-150,220,0,-35,11,p62_aerial --shot 1300,347,5.6,-90,-3,14,p62_luna_street --shot 1150,140,12,-60,-12,16,p62_bridge_side --shotdir 'Z:\tmp\city\p62smoke\' > /tmp/city/p62smoke_shots.out 2>&1
rc=$?; cp $L /tmp/city/p62smoke_shots_log.txt 2>/dev/null
st "hand mode shots rc=$rc: $(ls /tmp/city/p62smoke/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/p62smoke_shots_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/p62smoke_shots_log.txt | head -1); $(grep -oE 'Lane graph: [0-9]+ lanes, [0-9]+ connectors' /tmp/city/p62smoke_shots_log.txt | head -1)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p62.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 40 --autoevery 10 --renderevery 4 --quality 1 --shotdir 'Z:\tmp\city\p62smoke\' > /tmp/city/p62smoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/p62smoke_drive_log.txt 2>/dev/null
st "hand mode autoplay drive rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p62smoke_drive_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/p62smoke_drive_log.txt | tail -1 | cut -c1-160)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p62.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\p62smoke\' > /tmp/city/p62smoke_proc.out 2>&1
rc=$?; cp $L /tmp/city/p62smoke_proc_log.txt 2>/dev/null
st "procedural drive smoke rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p62smoke_proc_log.txt) fatal; $(grep -o 'Road network: [0-9]* nodes, [0-9]* edges' /tmp/city/p62smoke_proc_log.txt | head -1); $(grep -E 'autoplay t=' /tmp/city/p62smoke_proc_log.txt | tail -1 | cut -c1-120)"
st "cq70 done"
touch /tmp/city/cq70_done.marker
