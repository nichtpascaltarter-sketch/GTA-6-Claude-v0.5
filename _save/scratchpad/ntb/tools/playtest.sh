#!/bin/sh
# Automated play-test under Wine: runs each --autoplay scenario, collects screenshots (PNG) and the telemetry log.
# Usage: tools/playtest.sh [scenario ...]   (default: walk drive bike fly boat shoot)
# Env: EXE (default bin/NeonTide.exe), OUTDIR (default /tmp/playtest), WINEPREFIX (default /tmp/wine_playtest),
#      DURATION (seconds of game time per scenario, default 30), RES (default 960x540)
cd "$(dirname "$0")/.."
EXE=${EXE:-bin/NeonTide.exe}
OUTDIR=${OUTDIR:-/tmp/playtest}
export WINEPREFIX=${WINEPREFIX:-/tmp/wine_playtest}
DURATION=${DURATION:-30}
RES=${RES:-960x540}
W=${RES%x*}
H=${RES#*x}
SCENARIOS=${*:-walk drive bike fly boat shoot}
mkdir -p "$OUTDIR"
WINDIR=$(echo "$OUTDIR" | sed 's|/|\\|g')
LOG="$WINEPREFIX/drive_c/users/root/AppData/Local/NeonTide/log.txt"
for s in $SCENARIOS; do
  echo "== $s"
  EXE=$EXE TIMEOUT=${TIMEOUT:-1200} tools/run.sh --width "$W" --height "$H" --play --autoplay "$s" \
      --autoduration "$DURATION" --autoevery 5 --shotdir "Z:${WINDIR}\\" > "$OUTDIR/run_$s.txt" 2>&1
  echo "exit $?"
  if [ -f "$LOG" ]; then
    cp "$LOG" "$OUTDIR/log_$s.txt"
    grep -E "autoplay|sanitize|error|Error|crash|Crash|exception" "$LOG" | tail -20
  fi
  for f in "$OUTDIR"/auto_"$s"_*.bmp; do
    [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"
  done
done
echo "Screenshots and logs in $OUTDIR"
