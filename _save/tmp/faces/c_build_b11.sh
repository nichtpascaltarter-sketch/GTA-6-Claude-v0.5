#!/bin/sh
# O2 build of 0d67933 + batch 11 files (face.cpp, anim_test.cpp) -> /tmp/faces/nt_b11_o2.exe (pairs with nt_b10_o2.exe);
# waits for my host compiles (anim_test on b11tree) to finish and pass first
echo $$ > /tmp/faces/c_build_b11.pid
until grep -q "ALL PASSED\|FAILED" /tmp/faces/anim_test_b11tree.log 2>/dev/null && [ /tmp/faces/anim_test_b11tree.log -nt /tmp/faces/b11tree/src/anim/face.cpp ]; do sleep 20; done
grep -q "ALL PASSED" /tmp/faces/anim_test_b11tree.log || { echo "anim_test failed: no build"; rm -f /tmp/faces/c_build_b11.pid; exit 1; }
sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_b11.log 2>&1
cd /tmp/faces/b11g && OUT=/tmp/faces/nt_b11_o2.exe sh build.sh > /tmp/faces/build_b11_o2.log 2>&1
echo "build exit $? $(date +%T)" >> /tmp/faces/build_b11_o2.log
rm -f /tmp/faces/c_build_b11.pid
