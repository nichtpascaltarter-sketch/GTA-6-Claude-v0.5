#!/bin/sh
# After the b3 snapshot build: port/airport shots, hedge/old-fabric shots, then tour stops 9-11, 13 and 0
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "^exit" $SP/wa/build_b3.log 2>/dev/null; do sleep 20; done
grep -q "^exit 0" $SP/wa/build_b3.log || { echo "b3 build failed"; exit 1; }
EXE=$SP/wa/snap_b3/bin/world_agent.exe
echo "b3 built"
$SP/wa/shots.sh $EXE /tmp/world/b3a "4067,-197,4.9,-146,-4,8.5,p_tour" "3975,-262,4.8,-38,-3,10,p_hold" "3955,-170,4.8,-80,-2,10,p_gate" "4085,-206,4.8,-119,-4,10,p_mouth" "4068,-150,4.8,180,-3,10,p_blvd" "745,1230,7,20,-12,11,a_fc" "768,1232,6.45,28,-4,11,a_taxi" > /dev/null 2>&1
echo "b3a done"
$SP/wa/shots.sh $EXE /tmp/world/b3b "2007.9,-3512.1,3.5,-45,-10,10,h_grove" "3000,250,8,0,-15,10,h_hall" "1504,-330,5.7,0,8,10,o_luna" "1500,-300,30,20,-18,10,o_aerial" "4230,-3907,3.4,-38.7,-10,10,h_coral" > /dev/null 2>&1
echo "b3b done"
mkdir -p /tmp/world/b3t
cd /home/user/GTA-6-Claude-v0.5
sh $SP/memgate.sh 2400
EXE=$EXE WINEPREFIX=/tmp/wine_world TIMEOUT=2400 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 9 --tourcount 3 --renderevery 6 --shotdir 'Z:\tmp\world\b3t\' > /tmp/world/b3t/run1.log 2>&1
echo "tour 9-11 exit $?"
sh $SP/memgate.sh 2400
EXE=$EXE WINEPREFIX=/tmp/wine_world TIMEOUT=2400 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 13 --tourcount 1 --renderevery 6 --shotdir 'Z:\tmp\world\b3t\' > /tmp/world/b3t/run2.log 2>&1
echo "tour 13 exit $?"
sh $SP/memgate.sh 2400
EXE=$EXE WINEPREFIX=/tmp/wine_world TIMEOUT=2400 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 0 --tourcount 1 --renderevery 6 --shotdir 'Z:\tmp\world\b3t\' > /tmp/world/b3t/run3.log 2>&1
echo "tour 0 exit $?"
for f in /tmp/world/b3t/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png"; done
ls /tmp/world/b3t
