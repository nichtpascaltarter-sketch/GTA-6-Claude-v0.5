#!/bin/sh
# 10-20 combined sign-off (v2 exes: + local-only --reversetest drive tweak): bloom + low beam + vehicle lights. before = nt_e1 + shd_head (HEAD's post.hlsl and
# lights.hlsli; HEAD's gameworld.cpp, dynamic.hlsl), after = nt_v1 (tree v2). Both trees carry the LOCAL-ONLY app.cpp
# parking --time tweak (never signed off). One game at a time; gate.sh honours HOLD and the lead flag.
. /tmp/night/views.sh
sh /tmp/night/buildb.sh e2 /tmp/night/e
grep -q "rc 0" /tmp/night/build_e2.done || { echo build-failed > /tmp/night/chain_v.done; exit 1; }
sh /tmp/night/buildb.sh v2 /tmp/night/v2
grep -q "rc 0" /tmp/night/build_v2.done || { echo build-failed > /tmp/night/chain_v.done; exit 1; }
sh /tmp/night/selftest.sh v2 bin/nt_v2.exe
cd /home/user/GTA-6-Claude-v0.5
SI=/tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/settings.ini
run_auto() {  # TAG EXE AUTOPLAY DURATION EVERY [extra...]
  T=$1; X=$2; AP=$3; DU=$4; EV=$5; shift 5
  mkdir -p /tmp/night/shots/$T
  sh /tmp/night/gate.sh 3200
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$X TIMEOUT=5400 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay $AP --time 22 --autoduration $DU --autoevery $EV --synctimers --gfxstats --shotdir "Z:\\tmp\\night\\shots\\$T\\" "$@" > /tmp/night/shots/run_$T.log 2>&1
  echo "exit $?" >> /tmp/night/shots/run_$T.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_$T.txt
  for f in /tmp/night/shots/$T/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
  echo done > /tmp/night/shots/$T.done
}
rm -f $SI
run_auto vdA bin/nt_e2.exe drive 14 1 --reversetest --shaderdir 'Z:\tmp\night\shd_head'
run_auto vdB bin/nt_v2.exe drive 14 1 --reversetest
run_auto vwB0 bin/nt_v2.exe wanted 16 2
echo "reduce_flashing=1" > $SI
run_auto vwB1 bin/nt_v2.exe wanted 16 2
rm -f $SI
echo done > /tmp/night/chain_v_veh.done
BEACH="5078.76,1502.16,3.48,-7.26,8.00,21.50,beach_street_night"
for v in $NIGHT $BEACH; do
  GATE=4000 sh /tmp/night/shoot1.sh eb bin/nt_e2.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_head'
  GATE=4000 sh /tmp/night/shoot1.sh va bin/nt_v2.exe "$v" --exposurelog
done
echo done > /tmp/night/chain_v_night.done
for v in $(echo "$DAY" | tr ' ' '\n' | grep -E "downtown_street_day|calle_luna_street_day|mimo_strip_mall_day"); do
  GATE=4000 sh /tmp/night/shoot1.sh edb bin/nt_e2.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_head'
  GATE=4000 sh /tmp/night/shoot1.sh vda bin/nt_v2.exe "$v" --exposurelog
done
echo done > /tmp/night/chain_v_day.done
for t in teb tva; do
  k=0
  while [ $k -lt 3 ]; do
    if [ $t = teb ]; then sh /tmp/night/tour.sh $t bin/nt_e2.exe 4 5 --shaderdir 'Z:\tmp\night\shd_head'; else sh /tmp/night/tour.sh $t bin/nt_v2.exe 4 5; fi
    n=$(ls /tmp/night/tour/${t}_auto_tour_*.png 2>/dev/null | wc -l)
    [ "$n" -ge 5 ] && break
    k=$((k+1))
  done
done
echo done > /tmp/night/chain_v.done
