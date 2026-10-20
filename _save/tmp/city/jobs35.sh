#!/bin/bash
# the lead's memory hold (21:40): start nothing until the autoupdater's compile gate (PID 14554, sh /tmp/upd/gate.sh 2500)
# is gone or 22:30 UTC. Then: tb4 (terrainbake region breakdown, -O1; one build of mine), then tour stop 5 alone on
# nt_dev27 and nt_dev24 (the 4-stop tours split at stop 5). One game at a time, behind the memory gates.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs30.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
HOLD=$(date -d "2026-10-19 22:30:00 UTC" +%s)
alive() { tr '\0' ' ' < /proc/14554/cmdline 2>/dev/null | grep -q "/tmp/upd/gate.sh"; }
st "jobs35 holding: until PID 14554 (autoupdater gate) is gone or 22:30 UTC"
until [ "$(date +%s)" -ge "$HOLD" ] || ! alive; do sleep 20; done
st "jobs35 hold over"
gate 2500
(cd $S/tb && nice -n 5 g++ -std=c++17 -O1 -I$S/proto_tb terrainbake.cpp -o terrainbake -lpthread > build4.log 2>&1); st "tb4 build rc=$?"
gate 1500
(cd $S/tb && TB_DETAIL_ONLY=1 ./terrainbake > run4.log 2>&1); st "tb4 run exit $?"
rm -f $S/tb/tb_* $S/tb/terrainbake
st "tb4 done"
for T in dev27 dev24; do
  mkdir -p /tmp/city/tour5_$T
  gate 3300; TIMEOUT=3600 EXE=/tmp/city/nt_$T.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 5 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir "Z:\\tmp\\city\\tour5_$T\\" > /tmp/city/tour5_$T.out 2>&1
  cp $L /tmp/city/tour5_${T}_log.txt
  st "tour5 $T: $(grep -c 'autoplay tour done' /tmp/city/tour5_${T}_log.txt) done, $(grep -c 'Segmentation\|Killed' /tmp/city/tour5_$T.out) segfaults/kills"
done
rm -rf /tmp/city/tour5_dev27 /tmp/city/tour5_dev24
st "jobs35 done"
