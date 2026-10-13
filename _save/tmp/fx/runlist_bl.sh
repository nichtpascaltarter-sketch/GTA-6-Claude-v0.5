#!/bin/sh
# bindless A/B: TAG and EXE1 (in bin/); two night streets + a day street in shot mode, tour stops 2, 4, 7, 8; --gfxstats
cd /tmp/fx
until [ -f /home/user/GTA-6-Claude-v0.5/bin/$EXE1 ]; do sleep 20; done
[ "${STREETS:-1}" = 1 ] && EXE=bin/$EXE1 TIMEOUT=2400 SHOTS="3000,-652,3.2,-90,-3,23.0,downtown_night 1600,1552,5.2,-90,-3,23.0,residential_night 3000,-652,3.2,-90,-3,14.0,downtown_day" sh /tmp/fx/shoot.sh ${TAG}_streets --settle 16 --quality 1 --gfxstats
[ "${STREETS:-1}" = 1 ] && cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_${TAG}_streets.txt
for N in ${STOPS:-2 4 7 8}; do EXE=bin/$EXE1 P=${TAG}_ts$N N=$N EXTRA="--gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_${TAG}_ts$N.txt; done
echo done > /tmp/fx/${TAG}_bl${DONE_SUFFIX}.done
