#!/bin/bash
# batches 12 + 14 + 15, the rest of the tests after the lead's second hold (no new game of mine before 05:45, or until
# /tmp/city/go): let the after shots (timeout pid 1069) finish, then the before shots (nt_dev23b), the story regression
# (nt_dev23, quality 0, a retry if the out-of-memory killer takes it), tour stops 2-5 on nt_dev23.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs23.status; }
while kill -0 1069 2>/dev/null; do sleep 20; done
cp $L /tmp/city/dev22_shots_log.txt
st "dev22_shots_log.txt (after, nt_dev23): $(ls /tmp/city/dev22_shots | wc -l) files, $(grep -c 'Segmentation' /tmp/city/dev22_shots_log.txt.out 2>/dev/null) segfaults"
HOLD=$(date -d "2026-10-13 05:45:00 UTC" +%s)
st "holding new games until 05:45 or /tmp/city/go"
until [ "$(date +%s)" -ge "$HOLD" ] || [ -f /tmp/city/go ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
mkdir -p /tmp/city/dev22_before_shots /tmp/city/tour_dev22
gate 3300; TIMEOUT=3600 EXE=/tmp/city/nt_dev23b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22_shot_args.txt) --shotdir 'Z:\tmp\city\dev22_before_shots\' > /tmp/city/dev22_before_shots_log.txt.out 2>&1
rc=$?; cp $L /tmp/city/dev22_before_shots_log.txt
st "dev22_before_shots_log.txt rc=$rc: $(ls /tmp/city/dev22_before_shots | wc -l) files, $(grep -c 'Segmentation' /tmp/city/dev22_before_shots_log.txt.out) segfaults"
st "start dev22 regression"
for try in 1 2; do
  grep -q "SUMMARY: " /tmp/city/reg_dev22_log.txt 2>/dev/null && break
  mkdir -p /tmp/city/reg_dev22
  gate 3500; TIMEOUT=9000 EXE=/tmp/city/nt_dev23.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev22\' > /tmp/city/reg_dev22.out 2>&1
  cp $L /tmp/city/reg_dev22_log.txt; rm -rf /tmp/city/reg_dev22
  st "dev22 regression (try $try): $(grep -c 'PASSED at' /tmp/city/reg_dev22_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev22.out) killed, $(grep -c 'Segmentation' /tmp/city/reg_dev22.out) segfaults; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev22_log.txt)"
done
st "start tour 2-5 (dev23)"
gate 3300; TIMEOUT=6000 EXE=/tmp/city/nt_dev23.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev22\' > /tmp/city/tour_dev22.out 2>&1
cp $L /tmp/city/tour_dev22_log.txt
st "jobs23 done"
