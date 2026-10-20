#!/bin/bash
# Road-overlap fix: the base corner shots on nt_s1c.exe (the same world as HEAD) while the fix exe waits for the build lock.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
touch /tmp/city/cq60.running
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/rfshots_base; rm -f /tmp/city/rfshots_base/*.bmp $L
wmem 3300; TIMEOUT=1500 EXE=/tmp/city/nt_s1c.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 1280 --height 720 --settle 8 $(cat /tmp/city/rf_shot_args.txt) --shotdir 'Z:\tmp\city\rfshots_base\' > /tmp/city/rfshots_base.out 2>&1
rc=$?; cp $L /tmp/city/rfshots_base_log.txt 2>/dev/null
st "shots base rc=$rc: $(ls /tmp/city/rfshots_base/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/rfshots_base_log.txt) fatal"
rm -f /tmp/city/cq60.running
