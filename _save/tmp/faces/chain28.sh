#!/bin/sh
# a second perf pair (batch 4, then main 91a6aba, back to back), then the batch-5 brow-depth test shots (dev9 QUICK)
cd /tmp/faces
/tmp/faces/perf.sh /tmp/faces/nt_b4_o2.exe b4b > /tmp/faces/perf_b4b.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_m91_o2.exe m91b > /tmp/faces/perf_m91b.out 2>&1
until grep -q "^exit" /tmp/faces/build_d9.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/faces/build_d9.log || exit 1
EXE=/tmp/faces/nt_d9.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d9 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat brow.args) > /tmp/faces/shoot_d9.out 2>&1
