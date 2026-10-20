#!/bin/sh
echo $$ > /tmp/faces/c_build_d19.pid
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_d19.log 2>&1
cd /tmp/faces/dev19 && QUICK=1 OUT=/tmp/faces/nt_d19.exe sh build.sh > /tmp/faces/build_d19.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_d19.log
rm -f /tmp/faces/c_build_d19.pid
