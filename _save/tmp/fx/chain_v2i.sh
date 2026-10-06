#!/bin/sh
# Takes over from chain_v2h while p3h2 (phase 3, 1080p stops 11-13) runs: a top-down debug view of the overhead map at
# the Grove house (is the stoop in it?), then stop 3 for phase 2 and 3, the character-shading facecams before / after,
# and the phase 3 timing run. One game at a time on slot 4.
cd /tmp/fx
until [ -f /tmp/fx/p3h2.done ]; do sleep 15; done
SD='--shaderdir Z:\tmp\fx\shd_p2'
SC='--shaderdir Z:\tmp\fx\shd_c1'
SG='--shaderdir Z:\tmp\fx\shd_dbg'
SHOTS="1963,-3500,40,0,-89,17,top_n" W=1920 H=1080 EXE=bin/nt_p2c.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh tdd --settle 12 --debugview 19 --debugsplit 0 $SG > /tmp/fx/tdd.out 2>&1
echo done > /tmp/fx/tdd.done
W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
export NT_D3D12_SLOT=1
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh c1a bin/nt_p3c.exe 38,17 0.5,1 23,15.5 -132 $SD
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh c1b bin/nt_p3c.exe 38,17 0.5,1 23,15.5 -132 $SC
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats $SD" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2i.done
