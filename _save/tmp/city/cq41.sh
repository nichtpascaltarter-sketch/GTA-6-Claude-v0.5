#!/bin/bash
# Step 3 ($S/s1b): the exe through build.sh once cq40's host checks are done (one build of mine at a time), then one
# --world=hand shot once cq39's games are done (one game of mine at a time).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/s1b.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq41 start"
while ! grep -q "cq40 done" /tmp/city/s1b.status; do sleep 20; done
rm -f $S/s1b/build/gen/world_res.o $S/s1b/build/gen/world.bin
wmem 2800
cd $S/s1b && OUT=/tmp/city/nt_s1b.exe nice -n 6 ./build.sh > /tmp/city/build_s1b.log 2>&1; st "nt_s1b build rc=$?: $(ls -la /tmp/city/nt_s1b.exe 2>/dev/null | awk '{print $5}') bytes; $(grep 'worldc: wrote' /tmp/city/build_s1b.log)"
[ -f /tmp/city/nt_s1b.exe ] || { st "cq41: no exe"; exit 1; }
while ! grep -q "cq39 done" /tmp/city/s0b.status; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/s1hand; rm -f /tmp/city/s1hand/*.bmp; rm -f $L
wmem 3300; TIMEOUT=900 EXE=/tmp/city/nt_s1b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 \
  --shot 3000,-300,320,0,-30,11,s1_hand_downtown_aerial --shotdir 'Z:\tmp\city\s1hand\' > /tmp/city/s1hand.out 2>&1
rc=$?; cp $L /tmp/city/s1hand_log.txt 2>/dev/null
st "s1b hand shot rc=$rc: $(ls /tmp/city/s1hand/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/s1hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/s1hand_log.txt | head -1)"
st "cq41 done"
