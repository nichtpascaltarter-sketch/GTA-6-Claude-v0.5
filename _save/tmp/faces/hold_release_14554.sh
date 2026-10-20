#!/bin/sh
# coordinator 21:42: hold my new games/compiles until PID 14554 (autoupdater compile gate) is gone or 22:30 UTC
# (hold_all = 2230 covers the time); this removes the hold early once 14554 has exited, unless the hold was changed
while kill -0 14554 2>/dev/null; do sleep 10; done
[ "$(cat /tmp/faces/hold_all 2>/dev/null)" = "2230" ] && rm -f /tmp/faces/hold_all && echo "released at $(date +%T): 14554 gone"
rm -f /tmp/faces/hold_release.pid
