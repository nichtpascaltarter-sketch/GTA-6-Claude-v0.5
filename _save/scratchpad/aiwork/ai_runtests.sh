#!/bin/bash
# AI agent: sequential in-game scenario runs (one Wine instance at a time) in the AI prefix /tmp/wine_ai (only this
# script uses it; any leftover instance in it is killed first: a second process opening the same log.txt truncates it
# under the first one), with per-run stdout/log files.
# Usage: ai_runtests.sh EXE mode:duration:renderevery[:autoevery] ...   (shots in /tmp/ai/MODE_EXETAG/)
cd /home/user/GTA-6-Claude-v0.5
EXE=$1; shift
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
TAG=$(basename "$EXE" .exe)
for spec in "$@"; do
  IFS=: read MODE DUR EVERY SHOTS <<< "$spec"
  PFX=/tmp/wine_ai
  OUT=$SP/wine_${MODE}_${TAG}.out
  LOGCOPY=$SP/log_${MODE}_${TAG}.txt
  SD=/tmp/ai/${MODE}_${TAG}
  mkdir -p $SD; rm -f $SD/*.bmp
  $SP/aiwork/ai_memgate.sh 3000   # memory gate (a D3D12 game under Wine takes ~2.7 GB)
  WINEPREFIX=$PFX /usr/lib/wine/wineserver64 -k 2>/dev/null; sleep 1
  rm -f $PFX/drive_c/users/root/AppData/Local/NeonTide/log.txt
  START=$(date +%s)
  WINEPREFIX=$PFX EXE=$EXE TIMEOUT=$(( ${DUR%.*} * 60 + 900 )) nice -n 10 tools/run.sh --width 480 --height 270 --quality 0 --renderevery $EVERY --play --autoplay $MODE --autoduration $DUR --autoevery ${SHOTS:-4} --shotdir "Z:\\tmp\\ai\\${MODE}_${TAG}\\" > $OUT 2>&1
  RC=$?
  cp $PFX/drive_c/users/root/AppData/Local/NeonTide/log.txt $LOGCOPY 2>/dev/null
  WINEPREFIX=$PFX /usr/lib/wine/wineserver64 -k 2>/dev/null
  echo "$MODE rc=$RC wall=$(( $(date +%s) - START ))s shots=$(ls $SD/*.bmp 2>/dev/null | wc -l) crash=$(grep -c 'Unhandled exception' $LOGCOPY 2>/dev/null) gpu=$(grep -ciE 'vkd3d:.*err|FATAL' $LOGCOPY $OUT 2>/dev/null | awk -F: '{s+=$NF} END {print s+0}') log=$LOGCOPY"
done
