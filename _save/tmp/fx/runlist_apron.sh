#!/bin/sh
# Airport apron and beach at low sun: baseline (nt_d12c) and fix (nt_d12d), 256 autotest materials and 1024
cd /tmp/fx
until [ -f /home/user/GTA-6-Claude-v0.5/bin/$EXE1 ]; do sleep 20; done
SH="-300,1490,6.2,0,-15,18.3,apron_183 -300,1490,6.2,-60,-12,17.9,apron_side -300,1490,4.0,30,-35,18.1,apron_close"
EXE=bin/$EXE1 TIMEOUT=2400 SHOTS="$SH" sh /tmp/fx/shoot.sh ${TAG}_ap256 --settle 8 --quality 1
EXE=bin/$EXE1 TIMEOUT=2400 SHOTS="$SH" sh /tmp/fx/shoot.sh ${TAG}_ap1024 --settle 8 --quality 1 --texsize 1024
EXE=bin/$EXE1 P=${TAG}_ts6 N=6 sh /tmp/fx/tour_stop.sh
echo done > /tmp/fx/${TAG}_apron.done
