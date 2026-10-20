#!/bin/sh
echo $$ > /tmp/faces/c_build_b8.pid
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_b8.log 2>&1
cd /tmp/faces/b8tree && OUT=/tmp/faces/nt_b8_o2.exe sh build.sh > /tmp/faces/build_b8_o2.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_b8_o2.log
rm -f /tmp/faces/c_build_b8.pid
