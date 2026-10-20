#!/bin/bash
# Road-overlap fix: the exe through build.sh in $S/rf/t2 (2c80a38), the MinGW syntax check there, the drive and melee smoke, then the
# corner shots on the base exe (nt_s1c: same world as HEAD) and on the fix, one game at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
while [ ! -f /tmp/city/cq57.census ]; do sleep 15; done
st "cq53 start"
cd $S/rf/t2
wmem 2800
rm -f /tmp/city/nt_rf.exe
OUT=/tmp/city/nt_rf.exe nice -n 6 ./build.sh > /tmp/city/build_rf.log 2>&1; st "nt_rf build rc=$?: $(ls -la /tmp/city/nt_rf.exe 2>/dev/null | awk '{print $5}') bytes"
wmem 2500
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_rf.log 2>&1; st "rf MinGW syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_rf.log) warnings=$(grep -c 'warning' /tmp/city/syntax_rf.log)"
[ -f /tmp/city/nt_rf.exe ] || { st "cq53: no exe"; touch /tmp/city/cq53.done; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/rfsmoke
while [ -f /tmp/city/cq60.running ]; do sleep 15; done
rm -f $L
wmem 3300; TIMEOUT=3000 EXE=/tmp/city/nt_rf.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\rfsmoke\' > /tmp/city/rfsmoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/rfsmoke_drive_log.txt 2>/dev/null
st "smoke drive rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/rfsmoke_drive_log.txt) fatal; $(grep -o 'Road gen: [0-9]* stretches[^)]*' /tmp/city/rfsmoke_drive_log.txt | head -1); $(grep -E 'autoplay t=' /tmp/city/rfsmoke_drive_log.txt | tail -1 | cut -c1-120)"
rm -f $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_rf.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 640 --height 360 --play --autoplay melee --meleeweapon bat --autoduration 18 --autoevery 6 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\rfsmoke\' > /tmp/city/rfsmoke_melee.out 2>&1
rc=$?; cp $L /tmp/city/rfsmoke_melee_log.txt 2>/dev/null
st "smoke melee rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/rfsmoke_melee_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/rfsmoke_melee_log.txt | tail -1 | cut -c1-120)"
for V in base fix; do
  E=/tmp/city/nt_s1c.exe; [ $V = fix ] && E=/tmp/city/nt_rf.exe
  if [ $V = base ] && [ "$(ls /tmp/city/rfshots_base/*.bmp 2>/dev/null | wc -l)" -ge 4 ]; then st "shots base: kept"; continue; fi
  while [ -f /tmp/city/cq60.running ]; do sleep 15; done
  mkdir -p /tmp/city/rfshots_$V; rm -f /tmp/city/rfshots_$V/*.bmp $L
  wmem 3300; TIMEOUT=1500 EXE=$E WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 1280 --height 720 --settle 8 $(cat /tmp/city/rf_shot_args.txt) --shotdir "Z:\\tmp\\city\\rfshots_$V\\" > /tmp/city/rfshots_$V.out 2>&1
  rc=$?; cp $L /tmp/city/rfshots_${V}_log.txt 2>/dev/null
  st "shots $V rc=$rc: $(ls /tmp/city/rfshots_$V/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/rfshots_${V}_log.txt) fatal"
done
st "cq53 done"
touch /tmp/city/cq53.done
