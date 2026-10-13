#!/bin/bash
# after jobs22c: the runs the out-of-memory killer cut short, again - the HQ "before" views (21:23) and the story
# regression on nt_dev20b (23:14, at mission 21); the regression now at --quality 0 (less memory; the missions don't
# depend on it). Up to two tries each.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs22.status; }
until grep -q "^jobs22 done" /tmp/city/jobs22.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt 3500 ]; do sleep 20; done; }
HQ="--shot 2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night --shot 4601.40,1372.03,264.22,-75.00,-28.00,18.80,sol_beach_aerial_sunset --shot 1962.67,-3500.00,4.24,-154.82,3.00,17.00,grove_street"
for try in 1 2; do
  [ "$(ls /tmp/city/dev20_hq_before | wc -l)" -ge 3 ] && break
  gate; TIMEOUT=3600 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ --shotdir 'Z:\tmp\city\dev20_hq_before\' > /tmp/city/dev20_hq_before_log.txt.out 2>&1
  rc=$?; cp $L /tmp/city/dev20_hq_before_log.txt
  st "dev20_hq_before (try $try) rc=$rc: $(ls /tmp/city/dev20_hq_before | wc -l) files"
done
for try in 1 2; do
  grep -q "SUMMARY: " /tmp/city/reg_dev20_log.txt 2>/dev/null && break
  mkdir -p /tmp/city/reg_dev20
  gate; TIMEOUT=9000 EXE=/tmp/city/nt_dev20b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev20\' > /tmp/city/reg_dev20.out 2>&1
  cp $L /tmp/city/reg_dev20_log.txt; rm -rf /tmp/city/reg_dev20
  st "dev20b regression (try $try): $(grep -c 'PASSED at' /tmp/city/reg_dev20_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev20.out) killed, $(grep -c 'Segmentation' /tmp/city/reg_dev20.out) segfaults; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev20_log.txt)"
done
st "jobs22d done"
