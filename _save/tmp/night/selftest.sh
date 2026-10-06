#!/bin/sh
# selftest.sh TAG EXE -> /tmp/night/log_TAG_selftest.txt
cd /home/user/GTA-6-Claude-v0.5
sh /tmp/night/gate.sh 3000
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$2 TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/night/run_$1_selftest.log 2>&1
echo "exit $?" >> /tmp/night/run_$1_selftest.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/log_$1_selftest.txt
grep "self-test:" /tmp/night/log_$1_selftest.txt
