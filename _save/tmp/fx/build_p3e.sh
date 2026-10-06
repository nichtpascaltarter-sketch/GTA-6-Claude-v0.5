#!/bin/sh
# phase 3 with the lighter palms (fronds 9 segments, coconuts 6 x 4) and the tuned leaf canopy
cd /tmp/fx/gd3b && QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_p3e.exe nice -n 10 sh build.sh > /tmp/fx/build_p3e.log 2>&1; echo "exit $?" >> /tmp/fx/build_p3e.log
