#!/bin/sh
# 10-19 bloom + low-beam sign-off: build nt_e1 (HEAD 731a731 + post.hlsl + lights.hlsli), self-test, 1080p Q3 night A/B
# per view (before = nt_e1 + shd_head: HEAD's two shaders; after = nt_e1), day A/B (3 views), timing tours A/B (4-8)
. /tmp/night/views.sh
sh /tmp/night/buildb.sh e1 /tmp/night/e
grep -q "rc 0" /tmp/night/build_e1.done || { echo build-failed > /tmp/night/chain_e.done; exit 1; }
sh /tmp/night/selftest.sh e1 bin/nt_e1.exe
BEACH="5078.76,1502.16,3.48,-7.26,8.00,21.50,beach_street_night"
for v in $NIGHT $BEACH; do
  GATE=4000 sh /tmp/night/shoot1.sh eb bin/nt_e1.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_head'
  GATE=4000 sh /tmp/night/shoot1.sh ea bin/nt_e1.exe "$v" --exposurelog
done
echo done > /tmp/night/chain_e_night.done
for v in $(echo "$DAY" | tr ' ' '\n' | grep -E "downtown_street_day|calle_luna_street_day|mimo_strip_mall_day"); do
  GATE=4000 sh /tmp/night/shoot1.sh edb bin/nt_e1.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_head'
  GATE=4000 sh /tmp/night/shoot1.sh eda bin/nt_e1.exe "$v" --exposurelog
done
echo done > /tmp/night/chain_e_day.done
for t in teb tea; do
  k=0
  while [ $k -lt 3 ]; do
    if [ $t = teb ]; then sh /tmp/night/tour.sh $t bin/nt_e1.exe 4 5 --shaderdir 'Z:\tmp\night\shd_head'; else sh /tmp/night/tour.sh $t bin/nt_e1.exe 4 5; fi
    n=$(ls /tmp/night/tour/${t}_auto_tour_*.png 2>/dev/null | wc -l)
    [ "$n" -ge 5 ] && break
    k=$((k+1))
  done
done
echo done > /tmp/night/chain_e.done
