#!/bin/sh
# after the before shots: the batch-1 shots, then the baseline perf run (one game of mine at a time)
while ! grep -q "exit" /tmp/faces/shoot_basef.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_b1b_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b1f --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b1f.out 2>&1
while ! grep -q "exit" /tmp/faces/build_base_o2.log 2>/dev/null; do sleep 10; done
/tmp/faces/perf.sh /tmp/faces/nt_base_o2.exe base > /tmp/faces/perf_base.out 2>&1
