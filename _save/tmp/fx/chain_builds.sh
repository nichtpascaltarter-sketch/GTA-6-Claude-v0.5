#!/bin/sh
# builds in turn: nt_csf (waits for the cs1 build), nt_m0 (main 2de35c5 + viewer.cpp), nt_cs2 (frozen copy of cs2)
until grep -q "^exit" /tmp/fx/build_csf.log 2>/dev/null; do sleep 20; done
QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_m0.exe nice -n 10 sh /tmp/fx/m0/build.sh > /tmp/fx/build_m0.log 2>&1; echo "exit $?" >> /tmp/fx/build_m0.log
rm -rf /tmp/fx/cs2b && mkdir -p /tmp/fx/cs2b && cd /tmp/fx/cs2 && tar --exclude=./build --exclude=./bin --exclude=./.git -cf - . | (cd /tmp/fx/cs2b && tar xf -)
QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_cs2.exe nice -n 10 sh /tmp/fx/cs2b/build.sh > /tmp/fx/build_cs2.log 2>&1; echo "exit $?" >> /tmp/fx/build_cs2.log
