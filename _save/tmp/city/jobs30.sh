#!/bin/bash
# batch 16 (plastic props) and the "before" for batch 17: once cq25.sh has built them, the day views on nt_dev24 (HEAD,
# before) and the Solaris street views at night with Reduce Flashing (settings.ini, removed after), then the day views on
# nt_dev25 (after). One game at a time, behind the memory gate.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
SET=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/settings.ini
trap 'rm -f "$SET"' EXIT
st() { echo "$1 $(date)" >> /tmp/city/jobs30.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
shots() {   # exe, args file, out dir, reduce flashing 0/1
  mkdir -p /tmp/city/$3
  if [ "$4" = 1 ]; then printf 'reduce_flashing = 1\n' > "$SET"; else rm -f "$SET"; fi
  gate 3300; TIMEOUT=3600 EXE=$1 WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat $2) --shotdir "Z:\\tmp\\city\\$3\\" > /tmp/city/$3_log.txt.out 2>&1
  rc=$?; rm -f "$SET"; cp $L /tmp/city/$3_log.txt
  st "$3 rc=$rc: $(ls /tmp/city/$3 | wc -l) files, $(grep -c 'Segmentation' /tmp/city/$3_log.txt.out) segfaults"
}
st "jobs30 start"
until grep -q "^dev24 exe ready\|^no dev24 exe\|^dev24 syntax errors" /tmp/city/cq25.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev24.exe ] || { st "no nt_dev24.exe, stopped"; exit 1; }
shots /tmp/city/nt_dev24.exe /tmp/city/dev25_day_args.txt dev25_before_day 0
shots /tmp/city/nt_dev24.exe /tmp/city/dev25_night_args.txt dev25_before_night 1
until grep -q "^dev25 exe ready\|^no dev25 exe\|^dev25 syntax errors" /tmp/city/cq25.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev25.exe ] || { st "no nt_dev25.exe, stopped"; exit 1; }
shots /tmp/city/nt_dev25.exe /tmp/city/dev25_day_args.txt dev25_after_day 0
st "jobs30 done"
