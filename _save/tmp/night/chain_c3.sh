#!/bin/sh
# shop interiors (falloff, ceiling panels, muted rails) + beacon rhythm: nt_c1 with hc's facade.hlsli, 720p Q2
until [ -f /tmp/night/chain_c2.done ]; do sleep 30; done
W=1280 H=720 Q=2 SHOTS="-1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening 3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh e3c --exposurelog --shaderdir 'Z:\tmp\night\shd_c3'
echo done > /tmp/night/chain_c3.done
