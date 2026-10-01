#!/bin/sh
# Runs the game under Wine + Xvfb (software GL) for automated testing on Linux.
# Usage: tools/run.sh [args...]   e.g. tools/run.sh --frames 30 --screenshot 'Z:\tmp\out.bmp'
export WINEDEBUG=${WINEDEBUG:--all}
export WINEPREFIX=${WINEPREFIX:-/tmp/neontide_wine}
export WINEDLLOVERRIDES="d3dcompiler_47=n"
cd "$(dirname "$0")/.."
if [ ! -f "$WINEPREFIX/drive_c/windows/system32/d3dcompiler_47.dll.native" ]; then
  mkdir -p "$WINEPREFIX"
  /usr/lib/wine/wine64 wineboot -i >/dev/null 2>&1 || true
  # wait until the new prefix is completely set up (the first game launch on a fresh prefix races it otherwise)
  /usr/lib/wine/wineserver64 -w >/dev/null 2>&1 || true
  cp /opt/neontide-test/d3dcompiler_47.dll "$WINEPREFIX/drive_c/windows/system32/d3dcompiler_47.dll"
  touch "$WINEPREFIX/drive_c/windows/system32/d3dcompiler_47.dll.native"
fi
# Test machines shared by several agents (opt-in: /tmp/neontide_build.lock exists) run at most 4 games at once
# (~1.8 GB each), so builds are not killed for memory. The slot's lock is held by the exec'd process until the
# game exits.
# NT_LEAD_SLOT=1: the lead's snapshot checks have a fifth slot of their own, so a verified commit never queues behind
# long test runs.
if [ -e /tmp/neontide_build.lock ] && command -v flock >/dev/null 2>&1; then
  if [ -n "$NT_LEAD_SLOT" ]; then
    exec 8>>/tmp/neontide_wine_slot.lead
    flock 8
  else
    while :; do
      for i in 1 2 3 4; do
        exec 8>>/tmp/neontide_wine_slot.$i
        if flock -n 8; then break 2; fi
        exec 8>&-
      done
      sleep 15
    done
  fi
fi
exec timeout ${TIMEOUT:-600} xvfb-run -a -s "-screen 0 1920x1080x24" /usr/lib/wine/wine64 ${EXE:-bin/NeonTide.exe} --autotest "$@"
