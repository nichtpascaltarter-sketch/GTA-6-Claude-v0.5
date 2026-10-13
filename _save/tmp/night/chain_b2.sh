#!/bin/sh
# phase B first look: nt_b1 + shaderdir shd_b2 (wb post.hlsl with the a2 local exposure), the 6 night views
until [ -f /tmp/night/chain_a2.done ]; do sleep 20; done
N1="3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night 2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night -8201.45,-9279.96,4.22,3.24,0.32,21.00,mimo_motel_night"
N2="-1283.07,-946.51,54.28,20.00,-45.00,21.50,westbrook_pools_night 1771.00,357.10,5.47,-65.00,3.00,20.50,calle_luna_street_evening -1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening"
SHOTS="$N1 $N2" EXE=bin/nt_b1.exe sh /tmp/night/shoot.sh b2 --exposurelog --shaderdir 'Z:\tmp\night\shd_b2'
echo done > /tmp/night/chain_b2.done
