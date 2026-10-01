#!/bin/sh
# triangle counts per cell (near detail / far LOD) at the tour stops and in the districts, and the debug-log metric,
# for a citylab binary; output to $2
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
timeout 1500 nice -n 5 $1 --logmetric \
  --tris 3168,-243,420,2300,stop2_downtown --tris 3350,-843,420,2300,stop3_solaris --tris 3093,1600,420,2300,stop4_midtown \
  --tris 5079,1500,420,2300,stop5_beach --tris 1771,357,420,2300,calle_luna --tris -1226,-594,420,2300,westbrook \
  --tris 2600,4300,420,2300,north_city --tris 1963,-3500,420,2300,grove --tris -8086,-9205,420,2300,key_solano \
  --tris -8548,-2049,420,2300,ten_palms > $2 2>&1
echo "exit $?" >> $2
