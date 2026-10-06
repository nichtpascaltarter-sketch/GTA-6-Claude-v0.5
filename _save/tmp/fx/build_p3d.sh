#!/bin/sh
# phase 3 with the tuned leaf canopy (gd3b)
cd /tmp/fx/gd3b && QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_p3d.exe nice -n 10 sh build.sh > /tmp/fx/build_p3d.log 2>&1; echo "exit $?" >> /tmp/fx/build_p3d.log
