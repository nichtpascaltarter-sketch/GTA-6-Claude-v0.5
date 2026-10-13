#!/bin/sh
echo $$ > /tmp/faces/c_build_d21.pid
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_d21.log 2>&1
cd /tmp/faces/dev21 && QUICK=1 OUT=/tmp/faces/nt_d21.exe sh build.sh > /tmp/faces/build_d21.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_d21.log
rm -f /tmp/faces/c_build_d21.pid
