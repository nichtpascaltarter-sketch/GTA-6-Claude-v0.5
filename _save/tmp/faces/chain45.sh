#!/bin/sh
# buzz cuts with scalp cards (dev14 = b7tree + FACES_BUZZ dev knob): QUICK build now; the shots after chain44
cd /tmp/faces/dev14 && QUICK=1 OUT=/tmp/faces/nt_d14.exe sh build.sh > /tmp/faces/build_d14.log 2>&1; echo "exit $?" >> /tmp/faces/build_d14.log
grep -q "^exit 0" /tmp/faces/build_d14.log || { echo "build failed" > /tmp/faces/chain45.done; exit 1; }
cd /tmp/faces
while [ ! -f /tmp/faces/chain44.done ]; do sleep 20; done
EXE=/tmp/faces/nt_d14.exe TIMEOUT=2400 /tmp/faces/shoot2.sh buzz_off --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzz.args) > /tmp/faces/shoot_buzz_off.out 2>&1
FACES_BUZZ=0.007,0.011,0.011,0.011,0.6,0.4 EXE=/tmp/faces/nt_d14.exe TIMEOUT=2400 /tmp/faces/shoot2.sh buzz_on --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzz.args) > /tmp/faces/shoot_buzz_on.out 2>&1
echo done > /tmp/faces/chain45.done
