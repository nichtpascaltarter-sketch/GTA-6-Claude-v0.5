#!/bin/sh
echo $$ > /tmp/faces/c_build_m46.pid
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_m46.log 2>&1
cd /tmp/faces/m46 && OUT=/tmp/faces/nt_m46_o2.exe sh build.sh > /tmp/faces/build_m46_o2.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_m46_o2.log
rm -f /tmp/faces/c_build_m46.pid
