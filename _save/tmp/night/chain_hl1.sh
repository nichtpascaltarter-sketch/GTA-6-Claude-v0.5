#!/bin/sh
# 10-19: night traffic with the longer-throw low-beam pattern (shd_hl1 lights.hlsli) on nt_d1 (after chain_bl1)
until [ -f /tmp/night/chain_bl1.done ]; do sleep 30; done
grep -q "rc 0" /tmp/night/build_d1.done || { echo no-d1 > /tmp/night/chain_hl1.done; exit 1; }
mkdir -p /tmp/night/shots/veh2
cd /home/user/GTA-6-Claude-v0.5
sh /tmp/night/gate.sh 3200
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_d1.exe TIMEOUT=2400 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay traffic --time 22 --autoduration 36 --autoevery 6 --shotdir 'Z:\tmp\night\shots\veh2\' --shaderdir 'Z:\tmp\night\shd_hl1' > /tmp/night/shots/run_veh2.log 2>&1
echo "exit $?" >> /tmp/night/shots/run_veh2.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_veh2.txt
for f in /tmp/night/shots/veh2/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
echo done > /tmp/night/chain_hl1.done
