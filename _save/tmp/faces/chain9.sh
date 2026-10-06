#!/bin/sh
# the kShaderPupil flip (batch 1 + the flip): O2 build after the dev5 build, then a short Wine run after the dev5 shots
while ! grep -q "exit" /tmp/faces/build_dev5.log 2>/dev/null; do sleep 10; done
cd /tmp/faces/flipTree && OUT=/tmp/faces/nt_flip_o2.exe sh build.sh > /tmp/faces/build_flip_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_flip_o2.log
[ -f /tmp/faces/nt_flip_o2.exe ] || exit 1
while pgrep -f "chain8.sh" > /dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat flip_day.args) $(cat flip_night.args)"
EXE=/tmp/faces/nt_flip_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh flip --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_flip.out 2>&1
