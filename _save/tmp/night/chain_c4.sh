#!/bin/sh
# beacon check 2 (each wall's start), beacons held on, 720p Q2 aerial
until [ -f /tmp/night/chain_c3.done ]; do sleep 30; done
W=1280 H=720 Q=2 SHOTS="2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh bcn2 --shaderdir 'Z:\tmp\night\shd_c2'
echo done > /tmp/night/chain_c4.done
