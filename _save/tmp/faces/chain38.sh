#!/bin/sh
# snapshot 36's character shading (the renderer's lighting.hlsl ad2ceab2 + dynamic.hlsl ea97d07d via --shaderdir,
# copies in /tmp/faces/shd_s36) on the batch 6 build: the 30 standard portraits -> /tmp/faces/b6s36 (compare b6)
cd /tmp/faces
EXE=/tmp/faces/nt_b6_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b6s36 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --shaderdir 'Z:\tmp\faces\shd_s36\' $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_b6s36.out 2>&1
echo done > /tmp/faces/chain38.done
