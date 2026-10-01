#!/bin/sh
# takes over from chain_p3 during the cs4 timing run: m0 timing (reversed order: cs4 first), then the batch-1
# meshes at 4 m night with the final shaders (does the goggles look go with batch 1?), then releases the self-test
export NT_D3D12_SLOT=1
cd /tmp/fx
until [ -f /tmp/fx/cs4_ts7b.done ]; do sleep 5; done
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_cs4_ts7b.txt
EXE=bin/nt_m0.exe P=m0_ts7b N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_m0_ts7b.txt
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh F44 bin/nt_cs4f.exe 27,7,17,10 4 23 -132 --shaderdir 'Z:\tmp\fx\shd_v4'
echo done > /tmp/fx/chain_p3.done
