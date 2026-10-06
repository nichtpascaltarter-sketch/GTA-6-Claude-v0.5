#!/bin/sh
# after the base timing run (p1t), one game at a time on slot 4: 1080p stops 11-13 with phase 2 (clearance test) and
# phase 3, 1080p stop 3 (the tower storefront glass) with phase 2 and phase 3, then the stops 4-13 timing runs at
# 960x540 for phase 2 (+ the self-test) and phase 3
cd /tmp/fx
until [ -f /tmp/fx/p1t.done ]; do sleep 15; done
until grep -q "^exit" /tmp/fx/build_p2.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_p2.log && W=1920 H=1080 EXE=bin/nt_p2.exe P=p2h2 START=11 COUNT=3 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
until grep -q "^exit" /tmp/fx/build_p3.log 2>/dev/null; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_p3.log && W=1920 H=1080 EXE=bin/nt_p3.exe P=p3h START=11 COUNT=3 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
grep -q "^exit 0" /tmp/fx/build_p2.log && W=1920 H=1080 EXE=bin/nt_p2.exe P=p2s3 START=3 COUNT=1 sh /tmp/fx/tour_multi.sh
grep -q "^exit 0" /tmp/fx/build_p3.log && W=1920 H=1080 EXE=bin/nt_p3.exe P=p3s3 START=3 COUNT=1 sh /tmp/fx/tour_multi.sh
if grep -q "^exit 0" /tmp/fx/build_p2.log; then
  EXE=bin/nt_p2.exe P=p2t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
  cd /home/user/GTA-6-Claude-v0.5
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p2.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p2_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_p2_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p2_selftest.txt
  echo done > /tmp/fx/p2_selftest.done
  cd /tmp/fx
fi
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_p3.log && EXE=bin/nt_p3.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2c.done
