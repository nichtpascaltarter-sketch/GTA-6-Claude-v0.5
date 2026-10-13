#!/bin/sh
# after b1 finishes (chain_b1 prints "b1 done") and nt_fx4 is built: wet night run (right half: SSR confidence)
while ! grep -q "b1 done\|failed" /tmp/fx/chain_b1.log 2>/dev/null; do sleep 10; done
while ! grep -q "build exit" /tmp/fx/build_fx4.log; do sleep 10; done
grep -q "build exit 0" /tmp/fx/build_fx4.log || { echo "nt_fx4 build failed"; exit 1; }
W=640 H=360 EXE=bin/nt_fx4.exe SHOTS="1800,-260,5.4,0,-2,22.5,luna_wet_night 3400,-320,3.5,0,2,22,dt_wet_night" TIMEOUT=1700 /tmp/fx/shoot.sh c1 --rain 0.4 --settle 30 --synctimers --quality 2 --debugview 14 --debugsplit 0.5 > /tmp/fx/c1.out 2>&1
echo "c1 done"
