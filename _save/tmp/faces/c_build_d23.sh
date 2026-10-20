#!/bin/sh
# QUICK game build of dev23 (1a83e24 + batch 10 + dev knobs FACES_EAR / FACES_LIPB) -> /tmp/faces/nt_d23.exe, after my
# pending host compile (pv_d23) so only one build of mine runs at a time
echo $$ > /tmp/faces/c_build_d23.pid
until grep -q "pv_d23 built" /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/b7037ilgh.output 2>/dev/null; do sleep 20; done
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_d23.log 2>&1
cd /tmp/faces/dev23 && QUICK=1 OUT=/tmp/faces/nt_d23.exe sh build.sh > /tmp/faces/build_d23.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_d23.log
rm -f /tmp/faces/c_build_d23.pid
