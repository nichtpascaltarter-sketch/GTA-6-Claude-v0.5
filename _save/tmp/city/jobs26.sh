#!/bin/bash
# batches 12 + 14 + 15, the rest of the tests (replaces jobs25.sh, which was stopped while it held): no new game of
# mine before 05:45 or /tmp/city/go. Then: the before shots (nt_dev23b, 19 views: the old beacon views dropped), the
# beacon close views before/after with Reduce Flashing on (the blink is on 18% of the time; the setting makes it a
# steady pulse, so a capture can't fall in the off-time), the story regression (nt_dev23, quality 0, a retry if the
# out-of-memory killer takes it), tour stops 2-5 on nt_dev23.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
SET=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/settings.ini
trap 'rm -f "$SET"' EXIT
st() { echo "$1 $(date)" >> /tmp/city/jobs23.status; }
HOLD=$(date -d "2026-10-13 05:45:00 UTC" +%s)
st "jobs26: holding new games until 05:45 or /tmp/city/go"
until [ "$(date +%s)" -ge "$HOLD" ] || [ -f /tmp/city/go ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
mkdir -p /tmp/city/dev22_before_shots /tmp/city/tour_dev22 /tmp/city/dev22_beacon_before /tmp/city/dev22_beacon_after
gate 3300; TIMEOUT=3600 EXE=/tmp/city/nt_dev23b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22_shot_args_b.txt) --shotdir 'Z:\tmp\city\dev22_before_shots\' > /tmp/city/dev22_before_shots_log.txt.out 2>&1
rc=$?; cp $L /tmp/city/dev22_before_shots_log.txt
st "dev22_before_shots_log.txt rc=$rc: $(ls /tmp/city/dev22_before_shots | wc -l) files, $(grep -c 'Segmentation' /tmp/city/dev22_before_shots_log.txt.out) segfaults"
for side in before after; do
  if [ $side = before ]; then X=/tmp/city/nt_dev23b.exe; else X=/tmp/city/nt_dev23.exe; fi
  printf 'reduce_flashing = 1\n' > "$SET"
  gate 3300; TIMEOUT=1800 EXE=$X WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22_beacon_args.txt) --shotdir "Z:\\tmp\\city\\dev22_beacon_$side\\" > /tmp/city/dev22_beacon_${side}_log.txt.out 2>&1
  rc=$?; rm -f "$SET"; cp $L /tmp/city/dev22_beacon_${side}_log.txt
  st "dev22_beacon_${side}_log.txt rc=$rc: $(ls /tmp/city/dev22_beacon_$side | wc -l) files, $(grep -c 'Segmentation' /tmp/city/dev22_beacon_${side}_log.txt.out) segfaults"
done
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
