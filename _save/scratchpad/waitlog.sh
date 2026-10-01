#!/bin/sh
# waitlog.sh <pattern> [timeout s]: wait until the story test log contains the pattern (or the runner exits)
L=/tmp/wine_story/drive_c/users/root/AppData/Local/NeonTide/log.txt
T=${2:-1800}
i=0
while [ $i -lt $T ]; do
  grep -aq "$1" $L 2>/dev/null && break
  pgrep -f nt_storytest.exe > /dev/null || break
  sleep 10; i=$((i+10))
done
grep -a "missiontest" $L | grep -v screenshot | tail -${TAILN:-40} | cut -c1-200
