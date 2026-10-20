#!/bin/sh
# One game at a time on slot 4, after p2t (phase 2 timing, running). The placement shader's final rule (foundation
# anchors over 1.2 m) comes from --shaderdir Z:\tmp\fx\shd_p2 (only propcull.hlsl there; the exes embed the rule before).
# phase 2: 1080p stops 11-13, self-test, base timing for stops 11-13; phase 3: 1080p stops 11-13, stop 3 for both
# (tower storefront glass), stops 4-13 timing
cd /tmp/fx
SD='--shaderdir Z:\tmp\fx\shd_p2'
until [ -f /tmp/fx/p2t.done ]; do sleep 15; done
W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2h4 START=11 COUNT=3 EXTRA="--gfxstats $SD" sh /tmp/fx/tour_multi.sh
cd /home/user/GTA-6-Claude-v0.5
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p2c.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p2_selftest.log 2>&1
echo "exit $?" >> /tmp/fx/run_p2_selftest.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p2_selftest.txt
echo done > /tmp/fx/p2_selftest.done
cd /tmp/fx
EXE=bin/nt_p1.exe P=p1t2 START=11 COUNT=3 EXTRA="--synctimers --gfxstats" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3h2 START=11 COUNT=3 EXTRA="--gfxstats $SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p2c.exe P=p2s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
W=1920 H=1080 EXE=bin/nt_p3c.exe P=p3s3 START=3 COUNT=1 EXTRA="$SD" sh /tmp/fx/tour_multi.sh
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats $SD" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2f.done
