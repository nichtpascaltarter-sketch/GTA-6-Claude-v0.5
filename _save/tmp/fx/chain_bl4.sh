#!/bin/sh
# after chain3 (b1 single day shot): bl3 (bl2 without the SkyLine lights) on the three street shots, vs b1
until [ -f /tmp/fx/b1_streets2.done ]; do sleep 20; done
until grep -q "^exit" /tmp/fx/bl_build3.log 2>/dev/null; do sleep 20; done
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_bl3.exe ] || exit 1
cd /tmp/fx
EXE=bin/nt_bl3.exe TIMEOUT=2400 SHOTS="3000,-652,3.2,-90,-3,23.0,downtown_night 1600,1552,5.2,-90,-3,23.0,residential_night 3000,-652,3.2,-90,-3,14.0,downtown_day" sh /tmp/fx/shoot.sh b3_streets --settle 16 --quality 1 --gfxstats
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_b3_streets.txt
echo done > /tmp/fx/b3_streets.done
