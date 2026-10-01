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
  cp /opt/neontide-test/d3dcompiler_47.dll "$WINEPREFIX/drive_c/windows/system32/d3dcompiler_47.dll"
  touch "$WINEPREFIX/drive_c/windows/system32/d3dcompiler_47.dll.native"
fi
exec timeout ${TIMEOUT:-600} xvfb-run -a -s "-screen 0 1920x1080x24" /usr/lib/wine/wine64 ${EXE:-bin/NeonTide.exe} --autotest "$@"
