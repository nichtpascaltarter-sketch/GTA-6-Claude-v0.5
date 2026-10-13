#!/bin/sh
# 1920x1080 proof on Wine slot 4, one game at a time, after g1 (nt_cs4 night): g0 (feature shadows off through
# --shaderdir), m0 (main 2de35c5, day + night), cs4 day, cs4f (batch-1 meshes) 1 m day + night, then the reversed
# timing pair at tour stop 7 (cs4 first, then m0), and the albedo split with the batch-1 meshes
export NT_D3D12_SLOT=1
cd /tmp/fx
until [ -f /tmp/fx/fc/g1.done ]; do sleep 10; done
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh g0 bin/nt_cs4.exe 27,7,17,10 1,4 23 -132 --shaderdir 'Z:\tmp\fx\shd_off'
W=1920 H=1080 SETTLE=12 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh M0 bin/nt_m0.exe 27,7,17,10 1,4 15.5,23 -132
[ -f /tmp/fx/hold_after_m0 ] && until [ ! -f /tmp/fx/hold_after_m0 ]; do sleep 10; done
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh g1d bin/nt_cs4.exe 27,7,17,10 1,4 15.5 -132
until grep -q "^exit" /tmp/fx/build_cs4f.log 2>/dev/null; do sleep 10; done
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_cs4f.exe ] && W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh F4 bin/nt_cs4f.exe 27,7,17,10 1 15.5,23 -132
EXE=bin/nt_cs4.exe P=cs4_ts7b N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_cs4_ts7b.txt
EXE=bin/nt_m0.exe P=m0_ts7b N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_m0_ts7b.txt
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh d1 bin/nt_cs4f.exe 7,17 1 23 -132 --debugsplit 1
echo done > /tmp/fx/chain_p2.done
