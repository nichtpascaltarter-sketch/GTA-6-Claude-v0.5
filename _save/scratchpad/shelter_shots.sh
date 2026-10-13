#!/bin/sh
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
$SP/memgate.sh 2600
TIMEOUT=3600 WINEPREFIX=/tmp/wine_world EXE=$SP/nt_world_shots.exe nice -n 10 tools/run.sh --width 960 --height 540 --settle 6 \
  --shot 2262.5,-854.4,4.5,-116.6,-4,22,w_shelter_night --shot 2262.5,-854.4,4.5,-116.6,-4,12,w_shelter_day \
  --shotdir 'Z:\tmp\world\' > $SP/wine_shelter.log 2>&1
echo "wine exit $?" >> $SP/wine_shelter.log
