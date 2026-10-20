#!/bin/sh
# 10-19 single queue (one game at a time; gate.sh honours HOLD): after chain_d1b -> HEAD (nt_c99) befores, shop pass 2
# (nt_d1 + shd_d2 facade.hlsli), bloom variant (shd_bl1), night traffic veh1 (as is) / veh2 (shd_hl1 low beam)
until [ -f /tmp/night/chain_d1.done ]; do sleep 30; done
MALL="-1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening"
CALLE="1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening"
STREET="3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night"
BEACH="5078.76,1502.16,3.48,-7.26,8.00,21.50,beach_street_night"
q() { P=$1; X=$2; shift 2; V=$1; shift; for n in $(echo "$V" | tr ' ' '\n' | awk -F, '{print $7}'); do :; done
      GATE=3200 W=1280 H=720 Q=2 SHOTS="$V" EXE=$X sh /tmp/night/shoot.sh $P "$@"; }
q h0a bin/nt_c99.exe "$MALL $CALLE" --exposurelog
q h0b bin/nt_c99.exe "$STREET $BEACH" --exposurelog
q d2 bin/nt_d1.exe "$MALL $CALLE" --exposurelog --shaderdir 'Z:\tmp\night\shd_d2'
q bl1 bin/nt_d1.exe "$STREET $BEACH" --exposurelog --shaderdir 'Z:\tmp\night\shd_bl1'
cd /home/user/GTA-6-Claude-v0.5
for v in veh1 veh2; do
  mkdir -p /tmp/night/shots/$v
  SD=""; [ $v = veh2 ] && SD="--shaderdir Z:\\tmp\\night\\shd_hl1"
  sh /tmp/night/gate.sh 3200
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_d1.exe TIMEOUT=2400 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay traffic --time 22 --autoduration 36 --autoevery 6 --shotdir "Z:\\tmp\\night\\shots\\$v\\" $SD > /tmp/night/shots/run_$v.log 2>&1
  echo "exit $?" >> /tmp/night/shots/run_$v.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_$v.txt
  for f in /tmp/night/shots/$v/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
done
echo done > /tmp/night/chain_q.done
