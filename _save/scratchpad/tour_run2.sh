#!/bin/sh
# Run the district tour (optionally after a fresh snapshot build) once enough memory is free (avoids the OOM killer).
# env: SNAP=1 builds a snapshot first; TOUREXE=path uses that exe; W/H/Q resolution/quality; RE=render-every N
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
if [ "${SNAP:-0}" = 1 ]; then
  TRIES=12 $SP/snap_loop.sh 2>&1 | tail -3
  cp /home/user/GTA-6-Claude-v0.5/bin/nt_snap.exe $SP/nt_tour.exe
elif [ -n "$TOUREXE" ]; then
  cp "$TOUREXE" $SP/nt_tour.exe
fi
cd /home/user/GTA-6-Claude-v0.5
rm -f /tmp/tour/*; mkdir -p /tmp/tour
for attempt in 1 2 3; do
  while [ "$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)" -lt 3000 ]; do sleep 20; done
  EXE=$SP/nt_tour.exe WINEPREFIX=/tmp/wine_tour TIMEOUT=9000 nice -n 6 tools/run.sh --width ${W:-960} --height ${H:-540} --play --autoplay tour --quality ${Q:-1} --renderevery ${RE:-6} --shotdir 'Z:\tmp\tour\' > /tmp/tour/run.txt 2>&1
  rc=$?
  echo "tour attempt $attempt exit $rc"
  grep -q "tour done" /tmp/wine_tour/drive_c/users/root/AppData/Local/NeonTide/log.txt && break
  grep -q "Killed" /tmp/tour/run.txt || break
done
grep -E "tour (shot|done)|Unhandled" /tmp/wine_tour/drive_c/users/root/AppData/Local/NeonTide/log.txt | tail -20
