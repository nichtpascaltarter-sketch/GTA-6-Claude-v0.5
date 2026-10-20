#!/bin/sh
# 10-19: bloom variant (softer Karis at night) on nt_d1 + a night traffic look (vehicle lights), 720p Q2
until [ -f /tmp/night/chain_d1.done ]; do sleep 30; done
grep -q "rc 0" /tmp/night/build_d1.done || { echo no-d1 > /tmp/night/chain_bl1.done; exit 1; }
BEACH="5078.76,1502.16,3.48,-7.26,8.00,21.50,beach_street_night"
GATE=3200 W=1280 H=720 Q=2 sh /tmp/night/shoot1.sh d1 bin/nt_d1.exe "$BEACH" --exposurelog
GATE=3200 W=1280 H=720 Q=2 sh /tmp/night/shoot1.sh bl1 bin/nt_d1.exe "$BEACH 1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening 3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" --exposurelog --shaderdir 'Z:\tmp\night\shd_bl1'
# night traffic (vehicle lights): the AI's busy downtown corner at 22:00, a screenshot every 5 s of game time
mkdir -p /tmp/night/shots/veh1
cd /home/user/GTA-6-Claude-v0.5
sh /tmp/night/gate.sh 3200
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_d1.exe TIMEOUT=2400 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay traffic --time 22 --autoduration 36 --autoevery 6 --shotdir 'Z:\tmp\night\shots\veh1\' > /tmp/night/shots/run_veh1.log 2>&1
echo "exit $?" >> /tmp/night/shots/run_veh1.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_veh1.txt
for f in /tmp/night/shots/veh1/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
echo done > /tmp/night/chain_bl1.done
