#!/bin/bash
# Part 1's game in hand mode ($S/p1 + app.cpp: game.init with streets, no menu, --play/--autoplay start a game; and
# savegame.cpp: no saves or loads in hand mode). MinGW, the exe, hand mode: shots with the game initialised, an autoplay
# drive on the pilot's streets; a procedural drive. One build and one game at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/p1b.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until [ -f /tmp/city/cq68_done.marker ]; do sleep 20; done
st "cq69 start"
cd $S/p1
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/p1b_mingw.log 2>&1; st "p1b MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/p1b_mingw.log) warnings $(grep -c 'warning' /tmp/city/p1b_mingw.log)"
wmem 2800
rm -f /tmp/city/nt_p1b.exe build/gen/world_res.o build/gen/world.bin
OUT=/tmp/city/nt_p1b.exe nice -n 6 ./build.sh > /tmp/city/build_p1b.log 2>&1; st "nt_p1b build rc=$?: $(ls -la /tmp/city/nt_p1b.exe 2>/dev/null | awk '{print $5}') bytes"
[ -f /tmp/city/nt_p1b.exe ] || { st "cq69: no exe"; touch /tmp/city/cq69_done.marker; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/p1bsmoke; rm -f /tmp/city/p1bsmoke/*.bmp $L
wmem 3300; TIMEOUT=1200 EXE=/tmp/city/nt_p1b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 1280 --height 720 \
  --shot 1300,347,5.6,-90,-3,14,p1b_luna_street --shot 1150,140,12,-60,-12,16,p1b_bridge_side --shotdir 'Z:\tmp\city\p1bsmoke\' > /tmp/city/p1bsmoke_shots.out 2>&1
rc=$?; cp $L /tmp/city/p1bsmoke_shots_log.txt 2>/dev/null
st "hand mode shots with the game initialised rc=$rc: $(ls /tmp/city/p1bsmoke/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/p1bsmoke_shots_log.txt) fatal; $(grep -oE 'Lane graph[^|]*|lane graph[^|]*' /tmp/city/p1bsmoke_shots_log.txt | head -1 | cut -c1-140)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p1b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 40 --autoevery 10 --renderevery 4 --quality 1 --shotdir 'Z:\tmp\city\p1bsmoke\' > /tmp/city/p1bsmoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/p1bsmoke_drive_log.txt 2>/dev/null
st "hand mode autoplay drive rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p1bsmoke_drive_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/p1bsmoke_drive_log.txt | tail -1 | cut -c1-160)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_p1b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\p1bsmoke\' > /tmp/city/p1bsmoke_proc.out 2>&1
rc=$?; cp $L /tmp/city/p1bsmoke_proc_log.txt 2>/dev/null
st "procedural drive smoke rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/p1bsmoke_proc_log.txt) fatal; $(grep -o 'Road network: [0-9]* nodes, [0-9]* edges' /tmp/city/p1bsmoke_proc_log.txt | head -1); $(grep -E 'autoplay t=' /tmp/city/p1bsmoke_proc_log.txt | tail -1 | cut -c1-120)"
st "cq69 done"
touch /tmp/city/cq69_done.marker
