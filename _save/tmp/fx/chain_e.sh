#!/bin/sh
while ! grep -q "build exit" /tmp/fx/build_fx8.log; do sleep 10; done
grep -q "build exit 0" /tmp/fx/build_fx8.log || { echo "nt_fx8 build failed"; exit 1; }
W=640 H=360 EXE=bin/nt_fx8.exe SHOTS="1735,355,5.2,-90,-2,9.5,fx_day 1735,355,5.2,-90,-2,21,fx_night" TIMEOUT=1700 /tmp/fx/shoot.sh e1 --fxdemo --clouds 0.3 --settle 16 --quality 2 > /tmp/fx/e1.out 2>&1
echo "e1 done"
W=640 H=360 EXE=bin/nt_fx8.exe SHOTS="1740,352,5.2,-90,0,21.5,n4th_night 1800,-260,5.4,0,-2,21.5,luna_shops_night 1740,352,5.2,-90,0,9.5,n4th_day" TIMEOUT=1700 /tmp/fx/shoot.sh e2 --clouds 0.3 --settle 16 --quality 2 > /tmp/fx/e2.out 2>&1
echo "e2 done"
