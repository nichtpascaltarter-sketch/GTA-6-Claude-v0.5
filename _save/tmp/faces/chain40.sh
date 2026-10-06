#!/bin/sh
# brow / lash density variants under snapshot 36's card accumulation (after the dev13 QUICK build, PID 8476, started by
# chain39): the 0.5 m and 1 m day shots and the 1 m night for six people per variant (FACES_BROWK=dens,tint,lash: a
# dev-only knob in /tmp/faces/dev13)
cd /tmp/faces
while kill -0 8476 2>/dev/null; do sleep 15; done
[ -f /tmp/faces/nt_d13.exe ] && [ /tmp/faces/nt_d13.exe -nt /tmp/faces/dev13/src/anim/face.cpp ] || { echo "no exe" > /tmp/faces/chain40.done; exit 1; }
for v in "k4:0.38,0.5,0.75" "k5:0.48,0.45,0.75"; do
  tag=${v%%:*}; spec=${v#*:}
  FACES_BROWK=$spec EXE=/tmp/faces/nt_d13.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d13$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat brow2.args) > /tmp/faces/shoot_d13$tag.out 2>&1
done
echo done > /tmp/faces/chain40.done
