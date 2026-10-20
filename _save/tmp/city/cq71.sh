#!/bin/bash
# Part 2a ($S/p2 = p62 + blocks: model typing, compileBlocks, blocksbin, flattenPoly, roadSurfaceHeight split, worldc,
# handworld, cellgen; catalog + Domino Park): CI's six GCC steps, worldc (p2 world/; p62's world/ must stay identical),
# rbcheck, MinGW, the census s62 vs p2, the exe, hand mode at Domino Park (day, night), a hand drive, a procedural drive.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/p2.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
until [ -f /tmp/city/cq70_done.marker ]; do sleep 20; done
st "cq71 start"
cd $S/p2
for T in compiler/bit_scan_test updater/update_logic_test world/world_format_test world/worldv_test world/worldmodel_test; do
  n=$(basename $T)
  wmem 2000
  nice -n 5 g++ $CI tests/$T.cpp -o /tmp/city/p2_$n > /tmp/city/p2_${n}_build.log 2>&1; b=$?
  /tmp/city/p2_$n > /tmp/city/p2_${n}_run.log 2>&1; st "CI $n build rc=$b ($(grep -c 'error\|warning' /tmp/city/p2_${n}_build.log) error/warning lines) run rc=$?: $(tail -1 /tmp/city/p2_${n}_run.log | cut -c1-100)"
done
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/city/p2_worldv > /tmp/city/p2_worldv_build.log 2>&1; b=$?
/tmp/city/p2_worldv --world=world --no-cache > /tmp/city/p2_worldv_run.log 2>&1; st "CI worldv on world/ build rc=$b run rc=$?: $(tail -1 /tmp/city/p2_worldv_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/p2_worldc > /tmp/city/p2_worldc_build.log 2>&1; st "worldc build rc=$? ($(grep -c warning /tmp/city/p2_worldc_build.log) warnings)"
/tmp/city/p2_worldc --world=world --out=/tmp/city/p2_world.bin > /tmp/city/p2_worldc_world.log 2>&1; st "worldc on world/ rc=$?: $(grep -E 'roads|graded|blocks' /tmp/city/p2_worldc_world.log | tr '\n' ';')"
/tmp/city/p2_worldc --world=$S/p62/world --out=/tmp/city/p2_world_p62.bin > /tmp/city/p2_worldc_p62.log 2>&1; st "worldc on p62's world/ rc=$?: vs p62's: $(cmp /tmp/city/p2_world_p62.bin /tmp/city/p62_world.bin && echo identical)"
wmem 1500
nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I. wmtools/rbcheck.cpp -o /tmp/city/p2_rbcheck > /tmp/city/p2_rbcheck_build.log 2>&1; /tmp/city/p2_rbcheck /tmp/city/p2_world.bin world > /tmp/city/p2_rbcheck.log 2>&1; st "rbcheck rc=$?: $(tail -1 /tmp/city/p2_rbcheck.log)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/p2_mingw.log 2>&1; st "MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/p2_mingw.log) warnings $(grep -c 'warning' /tmp/city/p2_mingw.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tools/worldc.cpp > /tmp/city/p2_mingw_worldc.log 2>&1; st "MinGW worldc rc=$? errors $(grep -c 'error' /tmp/city/p2_mingw_worldc.log) warnings $(grep -c 'warning' /tmp/city/p2_mingw_worldc.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tests/world/worldmodel_test.cpp > /tmp/city/p2_mingw_wmt.log 2>&1; st "MinGW worldmodel_test rc=$? errors $(grep -c 'error' /tmp/city/p2_mingw_wmt.log) warnings $(grep -c 'warning' /tmp/city/p2_mingw_wmt.log)"
touch /tmp/city/cq71_tests.marker
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck2.cpp -o /tmp/city/rfcheck2_p2 -lpthread > /tmp/city/rfcheck2_p2_build.log 2>&1; st "rfcheck2_p2 build rc=$?"
wmem 2000
nice -n 5 /tmp/city/rfcheck2_p2 > /tmp/city/census2_p2.txt 2> /tmp/city/census2_p2_err.txt; st "rfcheck2_p2 run rc=$?: $(grep '^H ' /tmp/city/census2_p2.txt)"
st "census s62 vs p2: record lines $(diff <(grep -E '^[A-Z] ' /tmp/city/census2_s62.txt) <(grep -E '^[A-Z] ' /tmp/city/census2_p2.txt) | grep -c '^[<>]') differ (of $(grep -cE '^[A-Z] ' /tmp/city/census2_p2.txt)); all lines, times masked: $(diff <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census2_s62.txt) <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census2_p2.txt) | grep -c '^[<>]') differ"
wmem 2800
rm -f /tmp/city/nt_p2.exe build/gen/world_res.o build/gen/world.bin
OUT=/tmp/city/nt_p2.exe nice -n 6 ./build.sh > /tmp/city/build_p2.log 2>&1; st "nt_p2 build rc=$?: $(ls -la /tmp/city/nt_p2.exe 2>/dev/null | awk '{print $5}') bytes; $(grep -E 'worldc:.*(roads|graded|blocks)' /tmp/city/build_p2.log | tr '\n' ';')"
[ -f /tmp/city/nt_p2.exe ] || { st "cq71: no exe"; touch /tmp/city/cq71_done.marker; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/p2smoke; rm -f /tmp/city/p2smoke/*.bmp $L
wmem 3300; TIMEOUT=1200 EXE=/tmp/city/nt_p2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 1280 --height 720 \
  --shot 1478,340,14,-55,-22,15,p2_domino_day --shot 1506,352,5.45,0,-4,16,p2_domino_street --shot 1478,340,14,-55,-22,21.5,p2_domino_night \
  --shot 1440,300,60,-30,-45,12,p2_heroes_junction --shotdir 'Z:\tmp\city\p2smoke\' > /tmp/city/p2smoke_shots.out 2>&1
rc=$?; cp $L /tmp/city/p2smoke_shots_log.txt 2>/dev/null
st "hand mode shots rc=$rc: $(ls /tmp/city/p2smoke/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/p2smoke_shots_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/p2smoke_shots_log.txt | head -1 | cut -c1-200)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 30 --autoevery 10 --renderevery 4 --quality 1 --shotdir 'Z:\tmp\city\p2smoke\' > /tmp/city/p2smoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/p2smoke_drive_log.txt 2>/dev/null
st "hand mode autoplay drive rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p2smoke_drive_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/p2smoke_drive_log.txt | tail -1 | cut -c1-160)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\p2smoke\' > /tmp/city/p2smoke_proc.out 2>&1
rc=$?; cp $L /tmp/city/p2smoke_proc_log.txt 2>/dev/null
st "procedural drive smoke rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p2smoke_proc_log.txt) fatal; $(grep -o 'Road network: [0-9]* nodes, [0-9]* edges' /tmp/city/p2smoke_proc_log.txt | head -1); $(grep -E 'autoplay t=' /tmp/city/p2smoke_proc_log.txt | tail -1 | cut -c1-120)"
st "cq71 done"
touch /tmp/city/cq71_done.marker
