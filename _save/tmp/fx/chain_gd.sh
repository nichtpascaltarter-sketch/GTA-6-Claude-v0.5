#!/bin/sh
# phase 1 proof: tour stops 4-13 with the GPU-driven props (gd1) and the base (gd0), then gd1 again (ABBA timing)
cd /tmp/fx
until grep -q "^exit" /tmp/fx/build_gd1.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_gd1.log || { echo "gd1 build failed"; exit 1; }
EXE=bin/nt_gd1.exe P=gd1a START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
until grep -q "^exit" /tmp/fx/build_gd0.log 2>/dev/null; do sleep 15; done
EXE=bin/nt_gd0.exe P=gd0a START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_gd.done
