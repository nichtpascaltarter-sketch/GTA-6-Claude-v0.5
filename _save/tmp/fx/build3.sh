#!/bin/sh
# builds in turn (QUICK): base = main 06580c3 (phase 1), phase 2 (gd2b), phase 3 (gd3b)
B=/home/user/GTA-6-Claude-v0.5/bin
cd /tmp/fx/base24 && QUICK=1 OUT=$B/nt_p1.exe nice -n 10 sh build.sh > /tmp/fx/build_p1.log 2>&1; echo "exit $?" >> /tmp/fx/build_p1.log
cd /tmp/fx/gd2b && QUICK=1 OUT=$B/nt_p2.exe nice -n 10 sh build.sh > /tmp/fx/build_p2.log 2>&1; echo "exit $?" >> /tmp/fx/build_p2.log
cd /tmp/fx/gd3b && QUICK=1 OUT=$B/nt_p3.exe nice -n 10 sh build.sh > /tmp/fx/build_p3.log 2>&1; echo "exit $?" >> /tmp/fx/build_p3.log
