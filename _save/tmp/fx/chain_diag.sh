#!/bin/sh
# goggles diagnosis and the 1920x1080 proof, on Wine slot 4 (one game at a time):
#  g1 feature shadows on (nt_cs4, today's meshes), night 1 m + 4 m;  g0 the same with the shipped lighting.hlsl
#  (--shaderdir, feature shadows off);  d1 albedo split with the batch-1 meshes (1 m night)
export NT_D3D12_SLOT=1
cd /tmp/fx
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh g1 bin/nt_cs4.exe 27,7,17,10 1,4 23 -132
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh g0 bin/nt_cs4.exe 27,7,17,10 1,4 23 -132 --shaderdir 'Z:\tmp\fx\shd_off'
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh d1 bin/nt_cs2f.exe 7,17 1 23 -132 --debugsplit 1
echo done > /tmp/fx/chain_diag.done
