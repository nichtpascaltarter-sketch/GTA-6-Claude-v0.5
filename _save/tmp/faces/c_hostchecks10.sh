#!/bin/sh
# batch 10 host checks: full anim_test, preview + carview compile/run, triangle counts and lipstat (main vs batch 10)
echo $$ > /tmp/faces/c_hostchecks10.pid
cd /tmp/faces/b10tree
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/anim_test.cpp -o /tmp/faces/anim_test_b10 2>&1 | grep error | head -3
/tmp/faces/anim_test_b10 > /tmp/faces/anim_test_b10.log 2>&1; echo "anim_test exit $?"
grep -E "lips:|face shape:|buzz|FAIL|ALL PASSED" /tmp/faces/anim_test_b10.log | cut -c1-200
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/preview.cpp -o /tmp/faces/pv_b10 2>&1 | grep error | head -3
sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
nice -n 5 g++ -O2 -std=c++17 -w -I src tests/anim/carview.cpp -o /tmp/faces/cv_b10 2>&1 | grep error | head -3
/tmp/faces/pv_b10 /tmp/faces/pvl3/pv_b10.ppm --protagonist 0 --view face --tiles --w 200 --h 200 > /dev/null 2>&1; echo "preview run $?"
/tmp/faces/cv_b10 /tmp/faces/pvl3/cv_b10.ppm > /dev/null 2>&1; echo "carview run $?"
rm -f /tmp/faces/pvl3/pv_b10.ppm /tmp/faces/pvl3/cv_b10.ppm
for t in m0d b10tree; do
  sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
  (cd /tmp/faces/$t && nice -n 5 g++ -O1 -std=c++17 -w -I src -I . /tmp/faces/cnt/cnt.cpp -o /tmp/faces/cnt_$t 2>&1 | grep error | head -3)
  sh /tmp/faces/gate.sh 2000 > /dev/null 2>&1
  (cd /tmp/faces/$t && nice -n 5 g++ -O2 -std=c++17 -w -I src -I . /tmp/faces/tools/lipstat.cpp -o /tmp/faces/lipstat_$t 2>&1 | grep error | head -3)
done
/tmp/faces/cnt_m0d 0 7 17 27 38 10 11 20 > /tmp/faces/cnt_m0d.txt; /tmp/faces/cnt_b10tree 0 7 17 27 38 10 11 20 > /tmp/faces/cnt_b10.txt
diff /tmp/faces/cnt_m0d.txt /tmp/faces/cnt_b10.txt > /dev/null && echo "triangle counts identical" || echo "triangle counts DIFFER"
echo "== lipstat main"; /tmp/faces/lipstat_m0d p0 p1 7 17 10 11 20 4 2 9
echo "== lipstat batch 10"; /tmp/faces/lipstat_b10tree p0 p1 7 17 10 11 20 4 2 9
echo "host checks done $(date +%T)"
rm -f /tmp/faces/c_hostchecks10.pid
