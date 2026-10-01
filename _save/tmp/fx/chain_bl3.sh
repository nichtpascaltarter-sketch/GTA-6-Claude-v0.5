#!/bin/sh
# after the b2 runs: the day street as a single-shot run with nt_bl1 (comparable with b0_streets2)
until [ -f /tmp/fx/b2_bl.done ]; do sleep 20; done
cd /tmp/fx
EXE=bin/nt_bl1.exe TIMEOUT=2400 SHOTS="3000,-652,3.2,-90,-3,14.0,downtown_day" sh /tmp/fx/shoot.sh b1_streets2 --settle 16 --quality 1 --gfxstats
echo done > /tmp/fx/b1_streets2.done
