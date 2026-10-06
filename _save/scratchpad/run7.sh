#!/bin/sh
# build (gated, retried), then one Wine screenshot pass of the touched interiors
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
$S/buildloop.sh || { echo "build failed" > $S/run7.status; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
cp bin/nt_int.exe $S/nt_int_run.exe
rm -f /tmp/int/*_7.bmp
$S/memgate.sh 2600
TIMEOUT=2400 WINEPREFIX=/tmp/wine_int EXE=$S/nt_int_run.exe nice -n 10 tools/run.sh --width 960 --height 540 --quality 1 --renderevery 20 --play --autoplay interior --autoevery 1000 --shotdir 'Z:\tmp\int\' \
  --shot 3359.0,-781.0,3.59,76.0,-4.0,13.00,r7_solaris_garden \
  --shot 3350.0,-784.8,3.59,-0.0,-3.0,13.00,r7_solaris_entrance \
  --shot 5138.8,1523.4,3.50,146.4,-12.0,13.00,r7_condo_lobby \
  --shot 3518.7,-716.0,133.72,24.2,-10.0,17.00,r7_ph_lounge_17 --shot 3518.0,-709.0,133.72,90.0,-6.0,17.00,r7_ph_view_17 > $S/run7.log 2>&1
cd /tmp/int && for f in r7_*.bmp; do convert "$f" "${f%.bmp}.png"; done
echo "done" > $S/run7.status
