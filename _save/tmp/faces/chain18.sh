#!/bin/sh
# A/B of the hair shells' edge (test build with FACES_AB): one QUICK build, then three short runs
cd /tmp/faces/dev6 && QUICK=1 OUT=/tmp/faces/nt_ab.exe sh build.sh > /tmp/faces/build_ab.log 2>&1; echo "exit $?" >> /tmp/faces/build_ab.log
[ -f /tmp/faces/nt_ab.exe ] || exit 1
cd /tmp/faces && ARGS="$(cat ab_day.args) $(cat ab_night.args)"
for m in 0 1 2; do
  FACES_AB=$m EXE=/tmp/faces/nt_ab.exe TIMEOUT=2400 /tmp/faces/shoot2.sh ab$m --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_ab$m.out 2>&1
done
