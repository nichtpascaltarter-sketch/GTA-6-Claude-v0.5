#!/bin/sh
# Takes over from chain_v2f once the phase 2 self-test is done (p1t2 keeps running): top-down 1080p shots of the Grove
# house with the base (nt_p1) and phase 2 (nt_p2c + final placement shader) to see where the decor plants stand
# relative to the stoop and walls, then the phase 3 runs as before.
cd /tmp/fx
until [ -f /tmp/fx/p2_selftest.done ]; do sleep 10; done
sleep 20
kill $(ps -eo pid,cmd | awk '$2=="/bin/sh" && $3=="/tmp/fx/chain_v2f.sh" {print $1}') 2>/dev/null
SD='--shaderdir Z:\tmp\fx\shd_p2'
until [ -f /tmp/fx/p1t2.done ]; do sleep 15; done
SHOTS="1963,-3500,40,0,-89,17,top_n 1963,-3500,40,90,-89,17,top_w" W=1920 H=1080 EXE=bin/nt_p1.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh td1 --settle 12 > /tmp/fx/td1.out 2>&1
SHOTS="1963,-3500,40,0,-89,17,top_n 1963,-3500,40,90,-89,17,top_w" W=1920 H=1080 EXE=bin/nt_p2c.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh td2 --settle 12 $SD > /tmp/fx/td2.out 2>&1
echo done > /tmp/fx/td.done
[ -f /tmp/fx/hold_p3 ] && until [ ! -f /tmp/fx/hold_p3 ]; do sleep 15; done
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3h2 START=11 COUNT=3 EXTRA="--gfxstats $SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats $SD" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2g.done
