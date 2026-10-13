#!/bin/sh
echo $$ > /tmp/faces/c_build_b9.pid
while [ -e /tmp/faces/c_build_m46.pid ]; do sleep 30; done
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_b9.log 2>&1
cd /tmp/faces/b9tree && OUT=/tmp/faces/nt_b9_o2.exe sh build.sh > /tmp/faces/build_b9_o2.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_b9_o2.log
rm -f /tmp/faces/c_build_b9.pid
