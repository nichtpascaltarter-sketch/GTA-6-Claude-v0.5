#!/bin/sh
# 10-19: build av (aviation blink sync: case 4 + reduceFlashing, city worldtypes.h) and d1 (shops, sodium share,
# corner beacons/LEDs); self-test av; 720p Q2 test shots
. /tmp/night/views.sh
sh /tmp/night/buildb.sh av /tmp/night/av
sh /tmp/night/buildb.sh d1 /tmp/night/d
grep -q "rc 0" /tmp/night/build_av.done && sh /tmp/night/selftest.sh av bin/nt_av.exe
grep -q "rc 0" /tmp/night/build_d1.done || { echo build-failed > /tmp/night/chain_d1.done; exit 1; }
GATE=3200 W=1280 H=720 Q=2 sh /tmp/night/shoot1.sh d1 bin/nt_d1.exe "-1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening 1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening 3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" --exposurelog
GATE=3200 W=1280 H=720 Q=2 sh /tmp/night/shoot1.sh bc3 bin/nt_d1.exe "2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night" --exposurelog --shaderdir 'Z:\tmp\night\shd_bcn3'
grep -q "rc 0" /tmp/night/build_av.done && GATE=3200 W=1280 H=720 Q=2 sh /tmp/night/shoot1.sh av bin/nt_av.exe "2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night" --exposurelog
echo done > /tmp/night/chain_d1.done
