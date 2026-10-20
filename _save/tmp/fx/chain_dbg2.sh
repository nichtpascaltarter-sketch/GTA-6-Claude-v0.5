#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
EXE=bin/nt_fx30.exe P=ts0i N=0 EXTRA="--debugview 2 --debugsplit 0.45" sh /tmp/fx/tour_stop.sh
EXE=bin/nt_fx30.exe P=ts0j N=0 EXTRA="--debugview 1 --debugsplit 0.45" sh /tmp/fx/tour_stop.sh
echo done > /tmp/fx/chain_dbg2.state
