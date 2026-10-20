#!/bin/sh
# 10-20 (after the 02:19 and 03:37 restarts) RESUMABLE: each step skips when its output exists. Sign-off runs for
# bloom + low beam + vehicle lights: wanted RF off/on (nt_v2), night A/B 1080p (4 views), day A/B (2 views), tours;
# then the grove leaf-shadow A/B. before = nt_e2 + shd_head, after = nt_v2.
cd /home/user/GTA-6-Claude-v0.5
SI=/tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/settings.ini
run_auto() {  # TAG EXE AUTOPLAY DURATION EVERY [extra...]
  T=$1; X=$2; AP=$3; DU=$4; EV=$5; shift 5
  [ -f /tmp/night/shots/$T.done ] && return 0
  rm -rf /tmp/night/shots/$T; mkdir -p /tmp/night/shots/$T
  sh /tmp/night/gate.sh 3200
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$X TIMEOUT=5400 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay $AP --time 22 --autoduration $DU --autoevery $EV --synctimers --gfxstats --shotdir "Z:\\tmp\\night\\shots\\$T\\" "$@" > /tmp/night/shots/run_$T.log 2>&1
  echo "exit $?" >> /tmp/night/shots/run_$T.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_$T.txt
  for f in /tmp/night/shots/$T/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
  echo done > /tmp/night/shots/$T.done
}
rm -f $SI
run_auto vwB0 bin/nt_v2.exe wanted 16 2
if [ ! -f /tmp/night/shots/vwB1.done ]; then echo "reduce_flashing=1" > $SI; run_auto vwB1 bin/nt_v2.exe wanted 16 2; rm -f $SI; fi
rm -f $SI
for v in "3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" "1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening" "5078.76,1502.16,3.48,-7.26,8.00,21.50,beach_street_night" "2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night"; do
  GATE=4000 sh /tmp/night/shoot1.sh eb bin/nt_e2.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_head'
  GATE=4000 sh /tmp/night/shoot1.sh va bin/nt_v2.exe "$v" --exposurelog
done
for v in "3168.00,-242.90,3.56,-70.00,8.00,14.00,downtown_street_day" "1771.00,357.10,5.47,-65.00,3.00,14.00,calle_luna_street_day"; do
  GATE=4000 sh /tmp/night/shoot1.sh edb bin/nt_e2.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_head'
  GATE=4000 sh /tmp/night/shoot1.sh vda bin/nt_v2.exe "$v" --exposurelog
done
for t in teb tva; do
  k=0
  while [ $k -lt 3 ]; do
    n=$(ls /tmp/night/tour/${t}_auto_tour_*.png 2>/dev/null | wc -l)
    [ "$n" -ge 5 ] && break
    if [ $t = teb ]; then sh /tmp/night/tour.sh $t bin/nt_e2.exe 4 5 --shaderdir 'Z:\tmp\night\shd_head'; else sh /tmp/night/tour.sh $t bin/nt_v2.exe 4 5; fi
    k=$((k+1))
  done
done
echo done > /tmp/night/chain_v.done
V="912.57,-3572.62,5.51,-98.20,2.30,17.00,grove_coral_wall"
GATE=4000 sh /tmp/night/shoot1.sh gA bin/nt_e2.exe "$V" --exposurelog
GATE=4000 sh /tmp/night/shoot1.sh gB bin/nt_e2.exe "$V" --exposurelog --shaderdir 'Z:\tmp\night\shd_vogel'
echo done > /tmp/night/chain_g.done
