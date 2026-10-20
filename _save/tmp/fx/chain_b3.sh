#!/bin/sh
# after the bl3 build and the b2 stops 4 and 7: bl3 on the three street shots and tour stops 2 and 8
until grep -q "^exit" /tmp/fx/bl_build3.log 2>/dev/null; do sleep 20; done
[ -f /home/user/GTA-6-Claude-v0.5/bin/nt_bl3.exe ] || exit 1
until [ -f /tmp/fx/b2_bl_47.done ]; do sleep 20; done
TAG=b3 EXE1=nt_bl3.exe STOPS="2 8" sh /tmp/fx/runlist_bl.sh > /tmp/fx/runlist_b3.log 2>&1
