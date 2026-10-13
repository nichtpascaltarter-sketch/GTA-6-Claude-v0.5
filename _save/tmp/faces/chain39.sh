#!/bin/sh
# brow / lash density under snapshot 36's card accumulation: QUICK build of dev13 (9e7ace0 + batch 6 + the s36 shaders
# + dev knobs FACES_BROWK=dens,tint,lash), then the 0.5 m and 1 m day shots and the 1 m night for six people per variant
cd /tmp/faces/dev13 && QUICK=1 OUT=/tmp/faces/nt_d13.exe sh build.sh > /tmp/faces/build_d13.log 2>&1; echo "exit $?" >> /tmp/faces/build_d13.log
grep -q "^exit 0" /tmp/faces/build_d13.log || exit 1
cd /tmp/faces
for v in "k1:0.55,0.55,0.7" "k3:0.6,0.4,0.7"; do
  tag=${v%%:*}; spec=${v#*:}
  FACES_BROWK=$spec EXE=/tmp/faces/nt_d13.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d13$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat brow2.args) > /tmp/faces/shoot_d13$tag.out 2>&1
done
echo done > /tmp/faces/chain39.done
