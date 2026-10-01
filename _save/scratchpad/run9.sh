#!/bin/sh
# gated build, then the Tide Customs lift test and the screenshot pass (penthouse, condo, Solaris vault spot)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
$S/buildloop.sh || { echo "build failed" > $S/run9.status; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
cp bin/nt_int.exe $S/nt_int_run.exe
rm -f /tmp/int/tide_* /tmp/int/r9_*
$S/memgate.sh 2600
TIMEOUT=3000 WINEPREFIX=/tmp/wine_int EXE=$S/nt_int_run.exe nice -n 10 tools/run.sh --width 960 --height 540 --quality 1 --renderevery 20 --play --autoplay interior --autoevery 1000 --shotdir 'Z:\tmp\int\' --tidetest "Tide Customs Calle Luna" --autoduration 60 > $S/tide5.log 2>&1
echo "tide done" > $S/run9.status
$S/memgate.sh 2600
TIMEOUT=4000 WINEPREFIX=/tmp/wine_int EXE=$S/nt_int_run.exe nice -n 10 tools/run.sh --width 960 --height 540 --quality 1 --renderevery 20 --play --autoplay interior --autoevery 1000 --shotdir 'Z:\tmp\int\' \
  --shot 3381.3,-1008.6,66.66,-160.7,-8.0,13.00,r9_downtown_penthouse_main_lounge_13 --shot 3380.0,-1011.1,66.66,-70.7,-6.0,13.00,r9_downtown_penthouse_window_lounge_13 --shot 3382.8,-1006.1,66.66,-24.2,-8.0,13.00,r9_downtown_penthouse_dining_13 --shot 3383.8,-1007.6,66.66,63.4,-8.0,13.00,r9_downtown_penthouse_kitchen_13 --shot 3385.6,-1016.3,66.66,114.2,-8.0,13.00,r9_downtown_penthouse_bedroom_13 --shot 5208.6,1612.3,34.65,-63.4,-8.0,13.00,r9_sol_beach_condo_living_13 --shot 5219.5,1615.3,34.65,-163.3,-8.0,13.00,r9_sol_beach_condo_bedroom_13 --shot 3357.7,-763.4,459.04,163.7,-6.0,13.00,r9_solaris_one_penthouse_vault_13 > $S/run9.log 2>&1
cd /tmp/int && for f in tide_*.bmp r9_*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png"; done
echo "done" > $S/run9.status
