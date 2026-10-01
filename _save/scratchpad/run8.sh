#!/bin/sh
# one Wine screenshot pass of the touched interiors (positions from run7.log, same binary)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
rm -f /tmp/int/r8_*
$S/memgate.sh 2600
TIMEOUT=2400 WINEPREFIX=/tmp/wine_int EXE=$S/nt_int_run.exe nice -n 10 tools/run.sh --width 960 --height 540 --quality 1 --renderevery 20 --play --autoplay interior --autoevery 1000 --shotdir 'Z:\tmp\int\' \
  --shot 3359.0,-781.0,3.59,76.0,-4.0,13.00,r8_solaris_garden \
  --shot 3350.0,-784.8,3.59,-0.0,-3.0,13.00,r8_solaris_entrance \
  --shot 5107.3,1523.4,3.50,139.6,-12.0,13.00,r8_condo_lobby_in_front_13 --shot 3518.7,-715.7,92.36,24.2,-10.0,17.00,r8_ph_lounge_17 --shot 3518.0,-708.0,92.36,90.0,-6.0,17.00,r8_ph_view_17 > $S/run8.log 2>&1
cd /tmp/int && for f in r8_*.bmp; do convert "$f" "${f%.bmp}.png"; done
echo "done" > $S/run8.status
