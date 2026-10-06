#!/bin/sh
# perf of main 91a6aba (same period as batch 4's), then the batch-5 brow-depth test shots with the dev9 QUICK build
cd /tmp/faces
/tmp/faces/perf.sh /tmp/faces/nt_m91_o2.exe m91 > /tmp/faces/perf_m91.out 2>&1
until grep -q "^exit" /tmp/faces/build_d9.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/faces/build_d9.log || exit 1
EXE=/tmp/faces/nt_d9.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d9 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat brow.args) > /tmp/faces/shoot_d9.out 2>&1
