#!/bin/sh
# phase 2 proof, one game at a time on slot 4: 1080p stops 11-13 with the base (nt_p1, main 06580c3) and phase 2 (nt_p2)
cd /tmp/fx
until grep -q "^exit" /tmp/fx/build_p1.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_p1.log && W=1920 H=1080 EXE=bin/nt_p1.exe P=p1h START=11 COUNT=3 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
until grep -q "^exit" /tmp/fx/build_p2.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_p2.log && W=1920 H=1080 EXE=bin/nt_p2.exe P=p2h START=11 COUNT=3 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2a.done
