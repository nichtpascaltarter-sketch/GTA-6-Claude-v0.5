#!/bin/sh
# beacon check: the aerial with the beacons held on (test override of facade.hlsli), 1280x720 Q2
until [ -f /tmp/night/chain_c1c.done ]; do sleep 30; done
W=1280 H=720 Q=2 SHOTS="2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night" EXE=bin/nt_c1.exe sh /tmp/night/shoot.sh bcn --shaderdir 'Z:\tmp\night\shd_c2'
echo done > /tmp/night/chain_c2.done
