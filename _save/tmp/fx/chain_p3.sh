#!/bin/sh
# 1920x1080 proof on Wine slot 4, one game at a time, after g1 (nt_cs4 night, feature shadows):
#  g2  nt_cs4 + shd_v2 (feature shadows, eyes and teeth SM_EYE), night 1 m + 4 m
#  M0  main 2de35c5, day + night  (the "before")
#  (hold point: /tmp/fx/hold_after_m0; SHD names the final shader folder, default shd_v2)
#  G   nt_cs4 + final shaders, day (night = g2 unless the shaders changed)
#  F4  batch-1 meshes (nt_cs4f) + final shaders, 1 m day + night
#  the reversed timing pair at tour stop 7 (final first, then m0), the albedo split with the batch-1 meshes
export NT_D3D12_SLOT=1
cd /tmp/fx
until [ -f /tmp/fx/fc/g1.done ]; do sleep 10; done
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh g2 bin/nt_cs4.exe 27,7,17,10 1,4 23 -132 --shaderdir 'Z:\tmp\fx\shd_v2'
W=1920 H=1080 SETTLE=12 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh M0 bin/nt_m0.exe 27,7,17,10 1,4 15.5,23 -132
while [ -f /tmp/fx/hold_after_m0 ]; do sleep 10; done
SHD=$(cat /tmp/fx/final_shd 2>/dev/null || echo shd_v2)
HOURS=$(cat /tmp/fx/final_hours 2>/dev/null || echo 15.5)
W=1920 H=1080 SETTLE=12 TIMEOUT=5400 sh /tmp/fx/facecam_run.sh G bin/nt_cs4.exe 27,7,17,10 1,4 $HOURS -132 --shaderdir "Z:\\tmp\\fx\\$SHD"
until grep -q "^exit" /tmp/fx/build_cs4f.log 2>/dev/null; do sleep 10; done
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_cs4f.exe ] && W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh F4 bin/nt_cs4f.exe 27,7,17,10 1 15.5,23 -132 --shaderdir "Z:\\tmp\\fx\\$SHD"
EXE=bin/nt_cs4.exe P=cs4_ts7b N=7 EXTRA="--synctimers --gfxstats --shaderdir Z:\\tmp\\fx\\$SHD" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_cs4_ts7b.txt
EXE=bin/nt_m0.exe P=m0_ts7b N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_m0_ts7b.txt
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh d1 bin/nt_cs4f.exe 7,17 1 23 -132 --debugsplit 1 --shaderdir "Z:\\tmp\\fx\\$SHD"
echo done > /tmp/fx/chain_p3.done
