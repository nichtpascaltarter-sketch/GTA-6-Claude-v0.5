#!/bin/sh
# phase 2 timing on slot 4 after the 1080p proof: stops 4-13 at 960x540 with the base (nt_p1) then phase 2 (nt_p2),
# then the D3D12 self-test with nt_p2
cd /tmp/fx
until [ -f /tmp/fx/chain_v2a.done ]; do sleep 15; done
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_p1.exe ] && EXE=bin/nt_p1.exe P=p1t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
grep -q "^exit 0" /tmp/fx/build_p2.log && EXE=bin/nt_p2.exe P=p2t START=4 COUNT=10 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
if grep -q "^exit 0" /tmp/fx/build_p2.log; then
  cd /home/user/GTA-6-Claude-v0.5
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p2.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p2_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_p2_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p2_selftest.txt
fi
echo done > /tmp/fx/chain_v2b.done
