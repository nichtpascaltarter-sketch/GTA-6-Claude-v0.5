#!/bin/sh
# batch 4 final proof: wait for the O2 build, then 30 portraits (no test switches) and the crowd perf run
cd /tmp/faces
until grep -q "^exit" /tmp/faces/build_b4_o2.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/faces/build_b4_o2.log || exit 1
EXE=/tmp/faces/nt_b4_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b4 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_b4.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_b4_o2.exe b4 > /tmp/faces/perf_b4.out 2>&1
