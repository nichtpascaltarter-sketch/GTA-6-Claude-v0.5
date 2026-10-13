#!/bin/sh
# b4: the b3 tree with the current world files (yard floor paint, holding bays, taxi rank moved), then its shots once the
# b3 chain's Wine runs are done
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
sh $SP/wa/snapworld.sh b3 b4
grep -q "^exit 0" $SP/wa/build_b4.log || { echo "b4 build failed"; exit 1; }
echo "b4 built"
until grep -q "tour 0 exit" $SP/wa/b3chain.log 2>/dev/null || grep -q "failed" $SP/wa/b3chain.log 2>/dev/null; do sleep 20; done
EXE=$SP/wa/snap_b4/bin/world_agent.exe
$SP/wa/shots.sh $EXE /tmp/world/b4 "4067,-197,4.9,-146,-4,8.5,p_tour" "4072,-214,4.8,-90,-8,10,p_mouth" "4082,-219.6,28,-90,-62,10,p_top" "4250,-150,40,-100,-20,10,p_high" "4075,-168,5.2,180,-14,10,p_walk" "4289,-120,12,180,-25,10,p_gap" "3995,-205,14,180,-40,10,p_hold" "758,1220,6.45,8,-6,11,a_taxi" "745,1225,14,-35,-24,11,a_fc" > /dev/null 2>&1
echo "b4 shots done"
