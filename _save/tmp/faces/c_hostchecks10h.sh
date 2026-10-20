#!/bin/sh
echo $$ > /tmp/faces/c_hostchecks10h.pid
cd /tmp/faces/b10h
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/anim_test.cpp -o /tmp/faces/anim_test_b10h 2>&1 | grep error | head -3
/tmp/faces/anim_test_b10h > /tmp/faces/anim_test_b10h.log 2>&1; echo "anim_test exit $?"
grep -E "lips:|FAIL|ALL PASSED" /tmp/faces/anim_test_b10h.log | cut -c1-200
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/preview.cpp -o /tmp/faces/pv_b10h 2>&1 | grep error | head -3
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/carview.cpp -o /tmp/faces/cv_b10h 2>&1 | grep error | head -3
/tmp/faces/pv_b10h /tmp/faces/pvl3/pvh.ppm --protagonist 1 --view face --tiles --w 200 --h 200 > /dev/null 2>&1; echo "preview run $?"
/tmp/faces/cv_b10h /tmp/faces/pvl3/cvh.ppm > /dev/null 2>&1; echo "carview run $?"
rm -f /tmp/faces/pvl3/pvh.ppm /tmp/faces/pvl3/cvh.ppm /tmp/faces/pv_b10h /tmp/faces/cv_b10h
echo "host checks h done $(date +%T)"
rm -f /tmp/faces/c_hostchecks10h.pid
