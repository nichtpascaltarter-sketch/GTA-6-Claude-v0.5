#!/bin/sh
# gated build (3200 MB), then one Wine screenshot pass (2400 MB) of the touched rooms
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
$S/buildloop.sh || { echo "build failed" > $S/run11.status; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
cp bin/nt_int.exe $S/nt_int_run.exe
rm -f /tmp/int/r11_*
$S/memgate.sh 2400
TIMEOUT=3000 WINEPREFIX=/tmp/wine_int EXE=$S/nt_int_run.exe nice -n 10 tools/run.sh --width 960 --height 540 --quality 1 --renderevery 20 --play --autoplay interior --autoevery 1000 --shotdir 'Z:\tmp\int\' \
  --shot 3381.3,-1008.6,66.66,-160.7,-8.0,13.00,r11_downtown_penthouse_main_lounge_13 --shot 5208.6,1612.3,34.65,-63.4,-8.0,13.00,r11_sol_beach_condo_living_13 --shot 5215.2,1612.5,34.65,61.2,-8.0,13.00,r11_sol_beach_condo_sideboard_13 > $S/run11.log 2>&1
cd /tmp/int && for f in r11_*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png"; done
echo "done" > $S/run11.status
