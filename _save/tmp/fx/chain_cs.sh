#!/bin/sh
# after the c0 close-ups: ts7 crowd timing with nt_cs0, then (once nt_cs1 exists) the close-ups and ts7 with nt_cs1
until [ -f /tmp/fx/fc/c0.done ]; do sleep 20; done
cd /tmp/fx
EXE=bin/nt_cs0.exe P=t0_ts7 N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_t0_ts7.txt
until [ -f /home/user/GTA-6-Claude-v0.5/bin/nt_cs1.exe ]; do sleep 20; done
SETTLE=16 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh c1 bin/nt_cs1.exe 27,7,17,10 0.5,1,4 15.5,23 -132
EXE=bin/nt_cs1.exe P=t1_ts7 N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_t1_ts7.txt
echo done > /tmp/fx/chain_cs.done
