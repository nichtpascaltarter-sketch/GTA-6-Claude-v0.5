#!/bin/sh
# night fill-light proposal (private shader override /tmp/faces/shd_fill, never shipped): after chain36, the night
# close-ups at 1920x1080 and the standard night portraits at 960x540 with the ground bounce
cd /tmp/faces
while [ ! -f /tmp/faces/chain36.done ] && kill -0 16424 2>/dev/null; do sleep 15; done
W=1920 H=1080 EXE=/tmp/faces/nt_b6_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh nfill --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --shaderdir 'Z:\tmp\faces\shd_fill\' $(cat nightdiag.args) > /tmp/faces/shoot_nfill.out 2>&1
EXE=/tmp/faces/nt_b6_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh b6fill --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --shaderdir 'Z:\tmp\faces\shd_fill\' $(cat final_night.args) > /tmp/faces/shoot_b6fill.out 2>&1
echo done > /tmp/faces/chain37.done
