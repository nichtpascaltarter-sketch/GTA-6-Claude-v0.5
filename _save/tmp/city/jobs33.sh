#!/bin/bash
# the rest of jobs32.sh: let the nt_dev27 after-shot game (timeout pid 21633) finish and save its log; then, per the lead's
# memory priority, start nothing new until the AI agent's nightwalk game runs AND the faces gate (pid 17353) is gone, or
# 18:00 UTC; then the story regression (nt_dev27, quality 0, a retry if the out-of-memory killer takes it) and tours 2-5
# on nt_dev27 and nt_dev24. One game at a time, behind the memory gate.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs30.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
while kill -0 21633 2>/dev/null; do sleep 15; done
cp $L /tmp/city/dev27_after_day_log.txt
st "dev27_after_day rc=?: $(ls /tmp/city/dev27_after_day | wc -l) files, $(grep -c 'Saved' /tmp/city/dev27_after_day_log.txt) saved (log)"
HOLD=$(date -d "2026-10-19 18:00:00 UTC" +%s)
st "holding: until the AI agent's nightwalk game runs and the faces gate (pid 17353) is gone, or 18:00 UTC"
until [ "$(date +%s)" -ge "$HOLD" ] || { pgrep -f "^[^ ]*\.exe --autotest.*--autoplay nightwalk" > /dev/null && ! kill -0 17353 2>/dev/null; }; do sleep 20; done
st "hold over"
st "start dev27 regression"
for try in 1 2; do
  grep -q "SUMMARY: " /tmp/city/reg_dev27_log.txt 2>/dev/null && break
  mkdir -p /tmp/city/reg_dev27
  gate 3500; TIMEOUT=9000 EXE=/tmp/city/nt_dev27.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev27\' > /tmp/city/reg_dev27.out 2>&1
  cp $L /tmp/city/reg_dev27_log.txt; rm -rf /tmp/city/reg_dev27
  st "dev27 regression (try $try): $(grep -c 'PASSED at' /tmp/city/reg_dev27_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev27.out) killed, $(grep -c 'Segmentation' /tmp/city/reg_dev27.out) segfaults; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev27_log.txt)"
done
for T in dev27 dev24; do
  st "start tour 2-5 ($T)"
  mkdir -p /tmp/city/tour_$T
  gate 3300; TIMEOUT=6000 EXE=/tmp/city/nt_$T.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir "Z:\\tmp\\city\\tour_$T\\" > /tmp/city/tour_$T.out 2>&1
  cp $L /tmp/city/tour_${T}_log.txt
  st "tour $T: $(grep -c 'autoplay tour done' /tmp/city/tour_${T}_log.txt) done, $(grep -c 'Segmentation\|Killed' /tmp/city/tour_$T.out) segfaults/kills"
done
st "jobs33 done"
