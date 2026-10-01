#!/bin/sh
# night exposure before/after: TAG (e|f) and EXE1; tour stops 7, 8, 6, 13 plus two night streets in shot mode
cd /tmp/fx
until [ -f /home/user/GTA-6-Claude-v0.5/bin/$EXE1 ]; do sleep 20; done
EXE=bin/$EXE1 TIMEOUT=2400 SHOTS="3000,-652,3.2,-90,-3,23.0,downtown_night 1600,1552,5.2,-90,-3,23.0,residential_night" sh /tmp/fx/shoot.sh ${TAG}_streets --settle 16 --quality 1 --exposurelog
for N in 7 8 6 13; do EXE=bin/$EXE1 P=${TAG}_ts$N N=$N EXTRA="--exposurelog" sh /tmp/fx/tour_stop.sh; done
echo done > /tmp/fx/${TAG}_nightexp.done
