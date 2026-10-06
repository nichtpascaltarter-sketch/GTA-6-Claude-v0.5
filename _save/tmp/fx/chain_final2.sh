#!/bin/sh
# after the proof chain: self-test + a facecam sanity pair with the main-tree build (nt_cs3), then a second timing
# pair in reverse order (s2, then m0) for an ABBA comparison at tour stop 7
cd /tmp/fx
until [ -f /tmp/fx/chain_final.done ]; do sleep 20; done
until grep -q "^exit" /tmp/fx/build_cs3.log 2>/dev/null; do sleep 20; done
if [ -f /home/user/GTA-6-Claude-v0.5/bin/nt_cs3.exe ] && grep -q "^exit 0" /tmp/fx/build_cs3.log; then
  cd /home/user/GTA-6-Claude-v0.5
  WINEPREFIX=/tmp/wine_fx EXE=bin/nt_cs3.exe TIMEOUT=900 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_cs3_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_cs3_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_cs3_selftest.txt
  cd /tmp/fx
  SETTLE=16 TIMEOUT=3000 sh /tmp/fx/facecam_run.sh m3 bin/nt_cs3.exe 27,17 1 15.5,23 -132
fi
EXE=bin/nt_cs2.exe P=s2_ts7b N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_s2_ts7b.txt
EXE=bin/nt_m0.exe P=m0_ts7b N=7 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_stop.sh; cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_m0_ts7b.txt
echo done > /tmp/fx/chain_final2.done
