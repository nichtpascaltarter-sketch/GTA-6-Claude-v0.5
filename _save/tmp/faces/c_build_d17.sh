#!/bin/sh
echo $$ > /tmp/faces/c_build_d17.pid
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_d17.log 2>&1
cd /tmp/faces/dev17 && QUICK=1 OUT=/tmp/faces/nt_d17.exe sh build.sh > /tmp/faces/build_d17.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_d17.log
rm -f /tmp/faces/c_build_d17.pid
