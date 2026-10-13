#!/bin/sh
# final proof runs: close-ups (1 m, 4 m; day, night; people 27 7 17 10) for main (m0), my shading on today's meshes
# (s2) and with the faces batch 1 + shader pupil (f2); then tour stop 7 crowd timing for m0 and s2
cd /tmp/fx
SETTLE=16 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh m0 bin/nt_m0.exe 27,7,17,10 1,4 15.5,23 -132
until grep -q "^exit" /tmp/fx/build_cs2.log 2>/dev/null; do sleep 20; done
SETTLE=16 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh s2 bin/nt_cs2.exe 27,7,17,10 1,4 15.5,23 -132
EXE=bin/nt_m0.exe P=m0_ts7 N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_m0_ts7.txt
EXE=bin/nt_cs2.exe P=s2_ts7 N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_s2_ts7.txt
until grep -q "^exit" /tmp/fx/build_cs2f.log 2>/dev/null; do sleep 20; done
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_cs2f.exe ] && SETTLE=16 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh f2 bin/nt_cs2f.exe 27,7,17,10 1,4 15.5,23 -132
echo done > /tmp/fx/chain_final.done
