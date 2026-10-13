#!/bin/sh
# shop interiors, second pass (darker floor, stronger falloff), 720p Q2: nt_c1 + shd_c3 (after the hold)
until [ -f /tmp/night/chain_c4.done ]; do sleep 30; done
W=1280 H=720 Q=2 SHOTS="-1584.69,-591.72,8.14,-96.93,-1.88,20.50,mimo_strip_mall_evening" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh e4c --exposurelog --shaderdir 'Z:\tmp\night\shd_c3'
echo done > /tmp/night/chain_c5.done
