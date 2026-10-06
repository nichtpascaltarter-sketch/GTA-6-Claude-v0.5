#!/bin/sh
# batch 1c (the beard's lip clearance): build after the baseline build, then (after chain_runs' game) the men's shots
# and the perf run with the final exe; one build and one game of mine at a time
while ! grep -q "exit" /tmp/faces/build_base_o2.log 2>/dev/null; do sleep 10; done
cd /tmp/faces/b1tree && OUT=/tmp/faces/nt_b1c_o2.exe sh build.sh > /tmp/faces/build_b1c_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b1c_o2.log
while ! grep -q "exit" /tmp/faces/perf_base.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat men_day.args) $(cat men_night.args)"
EXE=/tmp/faces/nt_b1c_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b1m --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b1m.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_b1c_o2.exe b1c > /tmp/faces/perf_b1c.out 2>&1
