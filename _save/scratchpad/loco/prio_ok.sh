#!/bin/sh
# exit 0 once the memory priority (main, 10-19 21:4x) lets me start a game or a compile: the autoupdater's compile gate
# (PID 14554, sh /tmp/upd/gate.sh 2500) is gone - or 22:30 UTC
[ "$(date -u +%H%M)" -ge 2230 ] && exit 0
kill -0 14554 2>/dev/null && exit 1
exit 0
