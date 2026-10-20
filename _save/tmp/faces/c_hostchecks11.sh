#!/bin/sh
# batch 11 host checks on b11tree (1a83e24 + batch 11 files): preview + carview compile/run, triangle counts against
# batch 10; after my O2 build (one build at a time)
echo $$ > /tmp/faces/c_hostchecks11.pid
while [ -e /tmp/faces/c_build_b11.pid ]; do sleep 30; done
cd /tmp/faces/b11tree
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/preview.cpp -o /tmp/faces/pv_b11 2>&1 | grep error | head -3
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/carview.cpp -o /tmp/faces/cv_b11 2>&1 | grep error | head -3
/tmp/faces/pv_b11 /tmp/faces/pvl3/pv_b11.ppm --protagonist 0 --view face --tiles --w 200 --h 200 > /dev/null 2>&1; echo "preview run $?"
/tmp/faces/cv_b11 /tmp/faces/pvl3/cv_b11.ppm > /dev/null 2>&1; echo "carview run $?"
rm -f /tmp/faces/pvl3/pv_b11.ppm /tmp/faces/pvl3/cv_b11.ppm
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O1 -std=c++17 -w -I src -I . /tmp/faces/cnt/cnt.cpp -o /tmp/faces/cnt_b11tree 2>&1 | grep error | head -3
/tmp/faces/cnt_b10tree 0 7 17 27 38 10 11 20 > /tmp/faces/cnt_b10.txt; /tmp/faces/cnt_b11tree 0 7 17 27 38 10 11 20 > /tmp/faces/cnt_b11.txt
diff /tmp/faces/cnt_b10.txt /tmp/faces/cnt_b11.txt > /dev/null && echo "triangle counts identical" || echo "triangle counts DIFFER"
echo "host checks done $(date +%T)"
rm -f /tmp/faces/c_hostchecks11.pid
