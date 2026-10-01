#!/bin/sh
# Street-level day/night verification shots: Calle Luna, downtown, the Flats, a suburb (span-wire junction)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/world
TIMEOUT=4800 WINEPREFIX=/tmp/wine_world EXE=$SP/nt_world_shots.exe nice -n 10 tools/run.sh --width 960 --height 540 --settle 6 \
  --shot 1504,-330,5.7,0,3,11,w_cl_day --shot 1504,-330,5.7,0,3,22,w_cl_night \
  --shot 2704,-470,4.0,0,5,11,w_dt_day --shot 2704,-470,4.0,0,5,22,w_dt_night \
  --shot 1300,2547,6.2,-90,3,11,w_fl_day --shot 1300,2547,6.2,-90,3,22,w_fl_night \
  --shot -1593,-3830,4.9,0,3,11,w_sb_day --shot -1593,-3830,4.9,0,3,22,w_sb_night \
  --shotdir 'Z:\tmp\world\' > $SP/wine_world.log 2>&1
echo "wine exit $?" >> $SP/wine_world.log
ls -la /tmp/world/w_*.bmp >> $SP/wine_world.log 2>&1
