#!/bin/sh
# after the dev2 QUICK build: the -O2 batch-1 build, then the -O2 baseline build (one build of mine at a time)
while ! grep -q "exit" /tmp/faces/build_dev2.log 2>/dev/null; do sleep 10; done
cd /tmp/faces/b1tree && OUT=/tmp/faces/nt_b1_o2.exe sh build.sh > /tmp/faces/build_b1_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b1_o2.log
cd /tmp/faces/base_tree && OUT=/tmp/faces/nt_base_o2.exe sh build.sh > /tmp/faces/build_base_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_base_o2.log
