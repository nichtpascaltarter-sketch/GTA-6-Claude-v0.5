#!/bin/sh
# waits for the pupilprof compile, then builds eyedepth and runs both (memfree >= 2000 before each compile)
while pgrep -f "tools/pupilprof.cpp" > /dev/null; do sleep 10; done
cd /tmp/faces/lterm
until [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -ge 2000 ]; do sleep 15; done
nice -n 5 g++ -O1 -std=c++17 -I src -I . /tmp/faces/tools/eyedepth.cpp -o /tmp/faces/eyedepth 2>&1 | grep error | head -3
/tmp/faces/eyedepth 40 > /tmp/faces/eyedepth_lterm.txt 2>&1
echo done >> /tmp/faces/eyedepth_lterm.txt
