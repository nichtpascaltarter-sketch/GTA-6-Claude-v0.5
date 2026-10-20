#!/bin/sh
# phase 1 final: build the main-equivalent tree (4b6357f + phase 1, /tmp/fx/gd) after the phase-2 test build, then once
# the baseline tour run is done: the self-test with it on slot 4
export NT_D3D12_SLOT=1
until grep -q "^exit" /tmp/fx/build_gd2.log 2>/dev/null; do sleep 15; done
rm -rf /tmp/fx/gd1m && mkdir -p /tmp/fx/gd1m && cd /tmp/fx/gd && tar --exclude=./build --exclude=./bin --exclude=./.git -cf - . | (cd /tmp/fx/gd1m && tar xf -)
cd /tmp/fx/gd1m && QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_gd1m.exe nice -n 10 sh build.sh > /tmp/fx/build_gd1m.log 2>&1; echo "exit $?" >> /tmp/fx/build_gd1m.log
until [ -f /tmp/fx/chain_gd.done ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5
if grep -q "^exit 0" /tmp/fx/build_gd1m.log; then
  WINEPREFIX=/tmp/wine_fx EXE=bin/nt_gd1m.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_gd1m_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_gd1m_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_gd1m_selftest.txt
fi
echo done > /tmp/fx/chain_p1final.done
