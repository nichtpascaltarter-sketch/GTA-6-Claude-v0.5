#!/bin/sh
# Takes over from chain_v2i while p2s3 runs: p3s3, then the stoop-step rings checked at the Grove (stop 13 at 1080p
# and the top-down view), the character-shading facecams before / after, and the phase 3 timing run.
cd /tmp/fx
until [ -f /tmp/fx/p2s3.done ]; do sleep 15; done
SD='--shaderdir Z:\tmp\fx\shd_p2'
SC='--shaderdir Z:\tmp\fx\shd_c1'
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2h5 START=13 COUNT=1 EXTRA="--gfxstats $SD" sh /tmp/fx/tour_multi.sh
SHOTS="1963,-3500,40,0,-89,17,top_n" W=1920 H=1080 EXE=bin/nt_p2c.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh td3 --settle 12 $SD > /tmp/fx/td3.out 2>&1
echo done > /tmp/fx/td3.done
export NT_D3D12_SLOT=1
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh c1a bin/nt_p3c.exe 38,17 0.5,1 23,15.5 -132 $SD
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh c1b bin/nt_p3c.exe 38,17 0.5,1 23,15.5 -132 $SC
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats $SD" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2j.done
