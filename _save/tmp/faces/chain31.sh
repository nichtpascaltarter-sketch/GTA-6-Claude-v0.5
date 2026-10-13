#!/bin/sh
# after the batch 5 perf: the G-buffer albedo (debugview 1) of c17 / c0's brows with the batch 5 build
cd /tmp/faces
while kill -0 11990 2>/dev/null; do sleep 15; done
EXE=/tmp/faces/nt_b5_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh b5alb --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --debugview 1 $(cat dbgb.args) > /tmp/faces/shoot_b5alb.out 2>&1
