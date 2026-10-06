#!/bin/sh
# beard-edge diagnosis (test build nt_d8t2.exe): FACES_DBG 1 = the beard shell shaded as skin, 2 = no beard cards, 3 = both
cd /tmp/faces
until grep -q "^exit" /tmp/faces/shoot_d8b.out 2>/dev/null; do sleep 15; done
until grep -q "^exit" /tmp/faces/build_d8t2.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/faces/build_d8t2.log || exit 1
for m in 1 2 3; do
  FACES_DBG=$m FACES_LIFT=1 EXE=/tmp/faces/nt_d8t2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh dbgv$m --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat ab_cheeks.args) > /tmp/faces/shoot_dbgv$m.out 2>&1
done
