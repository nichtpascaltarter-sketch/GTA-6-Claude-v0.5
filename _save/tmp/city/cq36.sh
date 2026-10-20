#!/bin/bash
# stage 0 step 3: the game exe from $S/s1 through build.sh (worldc with the schema, LZ sections and the manifest; lz.cpp in
# the unity), then one hand-mode shot with it once the step 2 games are done. One build and one game of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/s1.status; }
until grep -q "^cq34 done" /tmp/city/s2.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq34.sh" > /dev/null; do sleep 20; done
st "cq36 start"
# step 2's games go first (its sign-off can join snapshot 57); this build would hold their memory
until grep -q "^jobs36 done" /tmp/city/s0.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/jobs36.sh" > /dev/null; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2800 ]; do sleep 20; done
cd $S/s1 && OUT=/tmp/city/nt_s1.exe nice -n 6 ./build.sh > /tmp/city/build_s1.log 2>&1; st "nt_s1 build rc=$?: $(ls -la /tmp/city/nt_s1.exe 2>/dev/null | awk '{print $5}') bytes; $(grep 'worldc:' /tmp/city/build_s1.log | tail -1)"
until grep -q "^jobs36 done" /tmp/city/s0.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/jobs36.sh" > /dev/null; do sleep 20; done
[ -f /tmp/city/nt_s1.exe ] || { st "cq36: no nt_s1.exe"; exit 1; }
mkdir -p /tmp/city/s1hand; rm -f $L
cd /home/user/GTA-6-Claude-v0.5
while [ "$(sh tools/memfree.sh)" -lt 3300 ]; do sleep 20; done
TIMEOUT=900 EXE=/tmp/city/nt_s1.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 \
  --shot 4280,-1700,260,0,-35,11,s1_hand_port_isle --shotdir 'Z:\tmp\city\s1hand\' > /tmp/city/s1hand.out 2>&1
rc=$?; cp $L /tmp/city/s1hand_log.txt 2>/dev/null
st "s1 hand shot rc=$rc: $(ls /tmp/city/s1hand/*.bmp 2>/dev/null | wc -l) shots; $(grep -c 'Unhandled\|FATAL' /tmp/city/s1hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/s1hand_log.txt)"
st "cq36 done"
