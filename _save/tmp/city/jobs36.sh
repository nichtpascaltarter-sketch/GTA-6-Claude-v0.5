#!/bin/bash
# stage 0 step 2 in game (nt_s0.exe): hand mode --shot of the bare terrain, then the lead's snapshot smoke tests in
# procedural mode (drive, melee) with the same arguments. One game at a time, behind the gate. After cq32.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/s0.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
until grep -q "^cq32 done" /tmp/city/s0.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq32.sh" > /dev/null; do sleep 20; done
[ -f /tmp/city/nt_s0.exe ] || { st "jobs36: no nt_s0.exe"; exit 1; }
st "jobs36 start"
mkdir -p /tmp/city/s0hand
rm -f $L
gate 3300; TIMEOUT=900 EXE=/tmp/city/nt_s0.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 \
  --shot 1780,160,8,90,-8,11,hand_calleluna_street --shot 3000,-300,320,0,-30,11,hand_downtown_aerial \
  --shot 4280,-1700,260,0,-35,11,hand_port_isle --shot -2400,4300,420,0,-12,11,hand_lake_ridge \
  --shotdir 'Z:\tmp\city\s0hand\' > /tmp/city/s0hand.out 2>&1
rc=$?; cp $L /tmp/city/s0hand_log.txt 2>/dev/null
st "hand shots rc=$rc: $(ls /tmp/city/s0hand/*.bmp 2>/dev/null | wc -l) shots; $(grep -c 'Unhandled\|FATAL' /tmp/city/s0hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/s0hand_log.txt)"
mkdir -p /tmp/city/s0smoke; rm -f $L
gate 3300; TIMEOUT=3000 EXE=/tmp/city/nt_s0.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\s0smoke\' > /tmp/city/s0smoke_drive.out 2>&1
rc=$?; cp $L /tmp/city/s0smoke_drive_log.txt 2>/dev/null
st "smoke drive rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/s0smoke_drive_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/s0smoke_drive_log.txt | tail -1 | cut -c1-120)"
rm -f $L
gate 3300; TIMEOUT=1500 EXE=/tmp/city/nt_s0.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 640 --height 360 --play --autoplay melee --meleeweapon bat --autoduration 18 --autoevery 6 --renderevery 6 --quality 1 --shotdir 'Z:\tmp\city\s0smoke\' > /tmp/city/s0smoke_melee.out 2>&1
rc=$?; cp $L /tmp/city/s0smoke_melee_log.txt 2>/dev/null
st "smoke melee rc=$rc: $(grep -cE 'Unhandled|FATAL' /tmp/city/s0smoke_melee_log.txt) fatal; $(grep -E 'autoplay t=' /tmp/city/s0smoke_melee_log.txt | tail -1 | cut -c1-120)"
st "jobs36 done"
