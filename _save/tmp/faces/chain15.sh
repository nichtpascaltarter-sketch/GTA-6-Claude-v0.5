#!/bin/sh
# batch 3 (all items so far): QUICK build of dev4, then after the before-shots finish, the dev shots
cd /tmp/faces/dev4 && QUICK=1 OUT=/tmp/faces/nt_dev4b.exe sh build.sh > /tmp/faces/build_dev4c.log 2>&1; echo "exit $?" >> /tmp/faces/build_dev4c.log
[ -f /tmp/faces/nt_dev4b.exe ] || exit 1
while ! grep -q "exit" /tmp/faces/shoot_b2full.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_dev4b.exe TIMEOUT=3600 /tmp/faces/shoot2.sh d4b --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_d4b.out 2>&1
