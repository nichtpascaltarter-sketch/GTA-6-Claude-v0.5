#!/bin/sh
# after chain_v2l (facecams, close-ups, phase 3 timing): the D3D12 self-test with the phase 3 exe
cd /tmp/fx
until [ -f /tmp/fx/chain_v2l.done ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p3c.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p3_selftest.log 2>&1
echo "exit $?" >> /tmp/fx/run_p3_selftest.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p3_selftest.txt
echo done > /tmp/fx/p3_selftest.done
