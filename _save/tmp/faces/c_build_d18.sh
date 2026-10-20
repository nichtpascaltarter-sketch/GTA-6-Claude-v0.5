#!/bin/sh
echo $$ > /tmp/faces/c_build_d18.pid
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_d18.log 2>&1
cd /tmp/faces/dev18 && QUICK=1 OUT=/tmp/faces/nt_d18.exe sh build.sh > /tmp/faces/build_d18.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_d18.log
rm -f /tmp/faces/c_build_d18.pid
