#!/bin/sh
# wait for a1 (nt_fx2) to finish and the nt_fx3 build, then run b1
while ps aux | grep -q "[n]t_fx2.exe --autotest" || [ ! -f /tmp/fx/log_a1.txt ]; do sleep 10; done
while ! grep -q "build exit" /tmp/fx/build_fx3.log; do sleep 10; done
grep -q "build exit 0" /tmp/fx/build_fx3.log || { echo "nt_fx3 build failed"; exit 1; }
W=640 H=360 EXE=bin/nt_fx3.exe SHOTS="4600,250,4,90,3,22,skyline_night 3400,-320,3.5,0,2,22,dt_night 3450,-250,3.4,90,2,18.3,gold_west" TIMEOUT=1700 /tmp/fx/shoot.sh b1 --clouds 0.5 --settle 14 --synctimers > /tmp/fx/b1.out 2>&1
echo "b1 done"
