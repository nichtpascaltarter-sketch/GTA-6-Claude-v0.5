#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
EXE=bin/nt_fx28.exe P=ts0f N=0 EXTRA="--debugview 12 --debugsplit 0.45" sh /tmp/fx/tour_stop.sh
EXE=bin/nt_fx28.exe P=ts0g N=0 EXTRA="--debugview 13 --debugsplit 0.45" sh /tmp/fx/tour_stop.sh
echo done > /tmp/fx/chain_dbg.state
