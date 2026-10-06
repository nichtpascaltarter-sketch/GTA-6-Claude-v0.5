#!/bin/sh
# batch-1 rebuild (-O2), then the baseline (-O2) for the perf comparison: one build of mine at a time
cd /tmp/faces/b1tree && OUT=/tmp/faces/nt_b1b_o2.exe sh build.sh > /tmp/faces/build_b1b_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b1b_o2.log
cd /tmp/faces/base_tree && OUT=/tmp/faces/nt_base_o2.exe sh build.sh > /tmp/faces/build_base_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_base_o2.log
