#!/bin/sh
# after the b1 A/B: the b0 day street shot that the OOM killer cut, then build bl2 (no game of mine running),
# its self-test and its A/B runs
until [ -f /tmp/fx/b1_bl.done ]; do sleep 20; done
cd /tmp/fx
EXE=bin/nt_bl0.exe TIMEOUT=2400 SHOTS="3000,-652,3.2,-90,-3,14.0,downtown_day" sh /tmp/fx/shoot.sh b0_streets2 --settle 16 --quality 1 --gfxstats
QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_bl2.exe nice -n 10 sh /tmp/fx/bl2/build.sh > /tmp/fx/bl_build2.log 2>&1; echo "exit $?" >> /tmp/fx/bl_build2.log
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_bl2.exe ] || exit 1
cd /home/user/GTA-6-Claude-v0.5
/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 1500
WINEPREFIX=/tmp/wine_fx EXE=bin/nt_bl2.exe TIMEOUT=900 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_bl2_selftest.log 2>&1
echo "selftest exit $?" >> /tmp/fx/run_bl2_selftest.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_bl2_selftest.txt
TAG=b2 EXE1=nt_bl2.exe sh /tmp/fx/runlist_bl.sh > /tmp/fx/runlist_b2.log 2>&1
