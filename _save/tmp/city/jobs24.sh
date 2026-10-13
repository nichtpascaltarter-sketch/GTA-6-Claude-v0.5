#!/bin/bash
# after the lead's hold (no new game of mine before 03:55, or until /tmp/city/go): finish nt_dev20b's story test (its
# game, timeout pid 8772, keeps running), then if it didn't complete, run it once more; then the lone grove_street HQ
# "before" view; then "jobs22e done" lets jobs23 (batches 12 + 14 + 15) go.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs22.status; }
while kill -0 8772 2>/dev/null; do sleep 20; done
cp $L /tmp/city/reg_dev20_log.txt; rm -rf /tmp/city/reg_dev20
st "dev20b regression (try 1): $(grep -c 'PASSED at' /tmp/city/reg_dev20_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev20.out) killed; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev20_log.txt)"
HOLD=$(date -d "2026-10-13 03:55:00 UTC" +%s)
st "holding new games until 03:55 or /tmp/city/go"
until [ "$(date +%s)" -ge "$HOLD" ] || [ -f /tmp/city/go ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
if ! grep -q "SUMMARY: " /tmp/city/reg_dev20_log.txt; then
  mkdir -p /tmp/city/reg_dev20
  gate 3500; TIMEOUT=9000 EXE=/tmp/city/nt_dev20b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev20\' > /tmp/city/reg_dev20.out 2>&1
  cp $L /tmp/city/reg_dev20_log.txt; rm -rf /tmp/city/reg_dev20
  st "dev20b regression (try 2): $(grep -c 'PASSED at' /tmp/city/reg_dev20_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev20.out) killed; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev20_log.txt)"
fi
mkdir -p /tmp/city/dev20_hq_before3
for try in 1 2; do
  [ -f /tmp/city/dev20_hq_before3/grove_street.bmp ] && break
  gate 4500; TIMEOUT=2400 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats --shot 1962.67,-3500.00,4.24,-154.82,3.00,17.00,grove_street --shotdir 'Z:\tmp\city\dev20_hq_before3\' > /tmp/city/dev20_hq_before3.out 2>&1
  st "dev20_hq_before grove_street alone (try $try) rc=$?: $(ls /tmp/city/dev20_hq_before3 | wc -l) files"
done
st "jobs22e done"
