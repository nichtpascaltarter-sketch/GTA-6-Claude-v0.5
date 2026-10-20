#!/bin/sh
# batch 6 final candidate: O2 build of b6tree (main cda29a1 + face.cpp + anim_test.cpp)
cd /tmp/faces/b6tree && OUT=/tmp/faces/nt_b6_o2.exe sh build.sh > /tmp/faces/build_b6_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b6_o2.log
echo done > /tmp/faces/chain34.done
