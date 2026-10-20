#!/bin/sh
# lighting-term diagnosis (private shader override, never shipped): debug views 30/31/32 + albedo + normals + lit,
# 1920x1080, six day portraits (c17, c38 at 0.5 and 1 m; c1, c0 at 1 m)
cd /tmp/faces
for v in 30 31 32 1 2 0; do
  if [ $v = 0 ]; then DV=""; else DV="--debugview $v"; fi
  W=1920 H=1080 EXE=/tmp/faces/nt_b5_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh diag_v$v --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --shaderdir 'Z:\tmp\faces\shd_dbg\' $DV $(cat diag.args) > /tmp/faces/shoot_diag_v$v.out 2>&1
done
echo done > /tmp/faces/chain32.done
