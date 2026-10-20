#!/bin/sh
while ! grep -q "build exit" /tmp/fx/build_fx6.log; do sleep 10; done
grep -q "build exit 0" /tmp/fx/build_fx6.log || { echo "nt_fx6 build failed"; exit 1; }
while ps aux | grep -q "[n]t_fx.\.exe --autotest"; do sleep 10; done
W=640 H=360 EXE=bin/nt_fx6.exe SHOTS="1800,-262,5.4,0,-14,11,road_day 1800,-260,5.4,0,-2,22.5,luna_night 3450,-250,3.4,90,2,18.3,gold_west 4600,250,4,90,3,22,skyline_night" TIMEOUT=1700 /tmp/fx/shoot.sh d1 --clouds 0.5 --settle 16 --quality 2 > /tmp/fx/d1.out 2>&1
echo "d1 done"
