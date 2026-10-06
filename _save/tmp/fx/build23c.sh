#!/bin/sh
# phase 2 (gd2b) and phase 3 (gd3b) with the door / path clearance, under new names (the chain's games keep theirs)
B=/home/user/GTA-6-Claude-v0.5/bin
cd /tmp/fx/gd2b && QUICK=1 OUT=$B/nt_p2c.exe nice -n 10 sh build.sh > /tmp/fx/build_p2c.log 2>&1; echo "exit $?" >> /tmp/fx/build_p2c.log
cd /tmp/fx/gd3b && QUICK=1 OUT=$B/nt_p3c.exe nice -n 10 sh build.sh > /tmp/fx/build_p3c.log 2>&1; echo "exit $?" >> /tmp/fx/build_p3c.log
