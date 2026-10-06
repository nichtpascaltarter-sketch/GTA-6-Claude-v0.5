#!/bin/sh
# after the baseline A/B: gfx self-test with nt_bl1, then the same A/B runs with nt_bl1
until [ -f /tmp/fx/b0_bl.done ]; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 1500
WINEPREFIX=/tmp/wine_fx EXE=bin/nt_bl1.exe TIMEOUT=900 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_bl1_selftest.log 2>&1
echo "selftest exit $?" >> /tmp/fx/run_bl1_selftest.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_bl1_selftest.txt
TAG=b1 EXE1=nt_bl1.exe sh /tmp/fx/runlist_bl.sh > /tmp/fx/runlist_b1.log 2>&1
