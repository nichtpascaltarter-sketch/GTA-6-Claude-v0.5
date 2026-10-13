#!/bin/sh
# the "before" for batch 4: main 91a6aba built as is (O2) and the same 30 portraits, after the batch 4 runs
cd /tmp/faces
until grep -q "^exit" /tmp/faces/build_b4_o2.log 2>/dev/null; do sleep 15; done
cd /tmp/faces/b4ref && OUT=/tmp/faces/nt_m91_o2.exe sh build.sh > /tmp/faces/build_m91_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_m91_o2.log
cd /tmp/faces
while kill -0 32497 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/faces/build_m91_o2.log || exit 1
EXE=/tmp/faces/nt_m91_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh m91 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_m91.out 2>&1
