#!/bin/sh
# Takes over from chain_v2g while its first top-down run (td1) is going: td2, then phase 3 (1080p stops 11-13, stop 3
# for phase 2 and 3), the character-shading backlog before / after (facecam at night and by day, via --shaderdir), and
# the phase 3 timing run. One game at a time on slot 4.
cd /tmp/fx
until grep -q "^exit" /tmp/fx/td1.out 2>/dev/null; do sleep 15; done
SD='--shaderdir Z:\tmp\fx\shd_p2'
SC='--shaderdir Z:\tmp\fx\shd_c1'
SHOTS="1963,-3500,40,0,-89,17,top_n 1963,-3500,40,90,-89,17,top_w" W=1920 H=1080 EXE=bin/nt_p2c.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh td2 --settle 12 $SD > /tmp/fx/td2.out 2>&1
echo done > /tmp/fx/td.done
[ -f /tmp/fx/hold_p3 ] && until [ ! -f /tmp/fx/hold_p3 ]; do sleep 15; done
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3h2 START=11 COUNT=3 EXTRA="--gfxstats $SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
export NT_D3D12_SLOT=1
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh c1a bin/nt_p3c.exe 38,17 0.5,1 23,15.5 -132 $SD
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_run.sh c1b bin/nt_p3c.exe 38,17 0.5,1 23,15.5 -132 $SC
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats $SD" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2h.done
