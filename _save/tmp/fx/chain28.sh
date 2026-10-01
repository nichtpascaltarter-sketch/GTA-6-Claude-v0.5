#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
while pgrep -f "OUT=bin/nt_fx28" >/dev/null || [ ! -f bin/nt_fx28.exe ]; do sleep 15; done
sleep 5
EXE=bin/nt_fx28.exe P=ts5 N=5 EXTRA="" sh /tmp/fx/tour_stop.sh
EXE=bin/nt_fx28.exe P=ts8 N=8 EXTRA="" sh /tmp/fx/tour_stop.sh
EXE=bin/nt_fx28.exe P=ml2 EXTRA="" sh /tmp/fx/melee_run.sh
echo done > /tmp/fx/chain28.state
