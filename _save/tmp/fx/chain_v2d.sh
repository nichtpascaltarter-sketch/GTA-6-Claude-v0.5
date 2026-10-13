#!/bin/sh
# after p3h (1080p stops 11-13, phase 3 with the reach-only clearance), one game at a time on slot 4, with the final
# builds (door / path clearance): phase 2 stop 13 at 1080p, stop 3 at 1080p for phase 2 and phase 3 (tower storefront
# glass), the stops 4-13 timing run at 960x540 for phase 2 and its self-test, then the timing run for phase 3
cd /tmp/fx
until [ -f /tmp/fx/p3h.done ]; do sleep 15; done
until grep -q "^exit" /tmp/fx/build_p3c.log 2>/dev/null; do sleep 15; done
if grep -q "^exit 0" /tmp/fx/build_p2c.log; then
  W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2h3 START=13 COUNT=1 EXTRA="--gfxstats" sh /tmp/fx/tour_multi.sh
  W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2s3 START=3 COUNT=1 sh /tmp/fx/tour_multi.sh
fi
grep -q "^exit 0" /tmp/fx/build_p3c.log && W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 sh /tmp/fx/tour_multi.sh
if grep -q "^exit 0" /tmp/fx/build_p2c.log; then
  EXE=bin/nt_p2c.exe P=p2t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
  cd /home/user/GTA-6-Claude-v0.5
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p2c.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p2_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_p2_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p2_selftest.txt
  echo done > /tmp/fx/p2_selftest.done
  cd /tmp/fx
fi
# the base run p1t was killed (memory cap) at stop 11: the base for stops 11-13
EXE=bin/nt_p1.exe P=p1t2 START=11 COUNT=3 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
grep -q "^exit 0" /tmp/fx/build_p3c.log && EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2d.done
