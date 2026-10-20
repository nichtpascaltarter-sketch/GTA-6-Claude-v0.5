#!/bin/sh
# Final builds (door / path clearance; phase 3 with the material generator fix), one game at a time on slot 4:
# phase 2: 1080p stops 11-13, stops 4-13 timing at 960x540, self-test, and the base timing for stops 11-13 (p1t was
# killed by the memory cap at stop 11); phase 3: 1080p stops 11-13, 1080p stop 3 for phase 2 and 3 (tower storefront
# glass), stops 4-13 timing
cd /tmp/fx
until grep -q "^exit" /tmp/fx/build_p2c.log 2>/dev/null; do sleep 15; done
if grep -q "^exit 0" /tmp/fx/build_p2c.log; then
  W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2h3 START=11 COUNT=3 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
  EXE=bin/nt_p2c.exe P=p2t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
  cd /home/user/GTA-6-Claude-v0.5
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p2c.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p2_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_p2_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p2_selftest.txt
  echo done > /tmp/fx/p2_selftest.done
  cd /tmp/fx
fi
EXE=bin/nt_p1.exe P=p1t2 START=11 COUNT=3 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
until grep -q "^exit" /tmp/fx/build_p3c.log 2>/dev/null; do sleep 15; done
if grep -q "^exit 0" /tmp/fx/build_p3c.log; then
  W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3h2 START=11 COUNT=3 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
  W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2s3 START=3 COUNT=1 sh /tmp/fx/tour_multi.sh
  W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 sh /tmp/fx/tour_multi.sh
  [ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
  EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
fi
echo done > /tmp/fx/chain_v2e.done
