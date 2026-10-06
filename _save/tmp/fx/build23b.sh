#!/bin/sh
# rebuild phase 2 (gd2b) then phase 3 (gd3b) after the placement clearance change
B=/home/user/GTA-6-Claude-v0.5/bin
cd /tmp/fx/gd2b && QUICK=1 OUT=$B/nt_p2.exe nice -n 10 sh build.sh > /tmp/fx/build_p2.log 2>&1; echo "exit $?" >> /tmp/fx/build_p2.log
cd /tmp/fx/gd3b && QUICK=1 OUT=$B/nt_p3.exe nice -n 10 sh build.sh > /tmp/fx/build_p3.log 2>&1; echo "exit $?" >> /tmp/fx/build_p3.log
