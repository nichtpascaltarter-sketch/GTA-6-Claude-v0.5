#!/bin/sh
# After the b5 build: beach access, fixed sign text, taxi rank, node 73 turnaround, node 72 barrier; then tour stops 13 and 0
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "^exit" $SP/wa/build_b5.log 2>/dev/null; do sleep 20; done
grep -q "^exit 0" $SP/wa/build_b5.log || { echo "b5 build failed"; exit 1; }
echo "b5 built"
EXE=$SP/wa/snap_b5/bin/world_agent.exe
$SP/wa/shots.sh $EXE /tmp/world/b5 "5362,972,5.5,-100,-14,10,s_beach" "5345,965,26,-90,-38,10,s_beach_air" "4905,965,26,90,-38,10,s_bay_air" "4069,-186,5.2,-90,-2,10,p_sign" "758,1220,6.45,8,-6,11,a_taxi" "0,455,45,0,-55,10,r_node73" "150,470,22,-90,-18,10,r_node72" > /dev/null 2>&1
echo "b5 shots done"
mkdir -p /tmp/world/b5t
cd /home/user/GTA-6-Claude-v0.5
sh $SP/memgate.sh 2400
EXE=$EXE WINEPREFIX=/tmp/wine_world TIMEOUT=2400 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 13 --tourcount 1 --renderevery 6 --shotdir 'Z:\tmp\world\b5t\' > /tmp/world/b5t/run1.log 2>&1
echo "tour 13 exit $?"
sh $SP/memgate.sh 2400
EXE=$EXE WINEPREFIX=/tmp/wine_world TIMEOUT=2400 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 0 --tourcount 1 --renderevery 6 --shotdir 'Z:\tmp\world\b5t\' > /tmp/world/b5t/run2.log 2>&1
echo "tour 0 exit $?"
for f in /tmp/world/b5t/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png"; done
ls /tmp/world/b5t
