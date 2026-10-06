#!/bin/sh
# night lighting-term diagnosis (private shader override, never shipped): views 33 (lamps / rest / lamp N.L), 34
# (feature shadow / ao / albedo) and the lit image, 1920x1080, after chain35
cd /tmp/faces
while [ ! -f /tmp/faces/chain35.done ] && kill -0 9341 2>/dev/null; do sleep 15; done
for v in 33 34 0; do
  if [ $v = 0 ]; then DV=""; else DV="--debugview $v"; fi
  W=1920 H=1080 EXE=/tmp/faces/nt_b6_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh ndiag_v$v --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --shaderdir 'Z:\tmp\faces\shd_dbg\' $DV $(cat nightdiag.args) > /tmp/faces/shoot_ndiag_v$v.out 2>&1
done
echo done > /tmp/faces/chain36.done
