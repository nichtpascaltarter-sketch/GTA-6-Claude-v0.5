#!/bin/bash
# the one HQ "before" view the out-of-memory killer took twice (grove_street, the third of the three): alone, after
# jobs22d, behind a higher memory gate (the 1920x1080 ultra run is the largest game on the machine)
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs22.status; }
until grep -q "jobs22d done" /tmp/city/jobs22.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt 4500 ]; do sleep 20; done; }
mkdir -p /tmp/city/dev20_hq_before3
for try in 1 2; do
  [ -f /tmp/city/dev20_hq_before3/grove_street.bmp ] && break
  gate; TIMEOUT=2400 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats --shot 1962.67,-3500.00,4.24,-154.82,3.00,17.00,grove_street --shotdir 'Z:\tmp\city\dev20_hq_before3\' > /tmp/city/dev20_hq_before3.out 2>&1
  st "dev20_hq_before grove_street alone (try $try) rc=$?: $(ls /tmp/city/dev20_hq_before3 | wc -l) files"
done
st "jobs22e done"
