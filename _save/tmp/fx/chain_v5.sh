#!/bin/sh
# phase 3 timing for stops 11-13 (p3t2 was killed by the memory cap after stop 10)
cd /tmp/fx
EXE=bin/nt_p3e.exe P=p3t3 START=11 COUNT=3 EXTRA="--synctimers --gfxstats --shaderdir Z:\\tmp\\fx\\shd_p2" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v5.done
