#!/bin/sh
# 10-20 roof speckles diagnosis (key_solano_pickets_morning, 1080p Q3, as the PC showcase): as is (256-texel autotest
# materials), without the metal roof's rust (shd_norust matgen.hlsl), and with 1024-texel materials (--texsize 1024)
until [ -f /tmp/night/chain_r.done ] && [ -f /tmp/night/chain_rl.done ]; do sleep 60; done
V="-8799.21,-9371.35,2.63,-68.42,-1.79,9.00,key_solano_pickets_morning"
GATE=4000 sh /tmp/night/shoot1.sh spA bin/nt_r1.exe "$V" --exposurelog
GATE=4000 sh /tmp/night/shoot1.sh spN bin/nt_r1.exe "$V" --exposurelog --shaderdir 'Z:\tmp\night\shd_norust'
GATE=4000 sh /tmp/night/shoot1.sh spT bin/nt_r1.exe "$V" --exposurelog --texsize 1024
echo done > /tmp/night/chain_sp.done
