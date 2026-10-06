#!/bin/sh
# debug views of the A/B build (no rolled hem: FACES_AB=1): 11 AO, 4 shadow, 1 albedo
cd /tmp/faces && ARGS="$(cat dbg.args)"
for v in 11 4 1; do
  FACES_AB=1 EXE=/tmp/faces/nt_ab.exe TIMEOUT=2400 /tmp/faces/shoot2.sh dbg$v --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --debugview $v $ARGS > /tmp/faces/shoot_dbg$v.out 2>&1
done
