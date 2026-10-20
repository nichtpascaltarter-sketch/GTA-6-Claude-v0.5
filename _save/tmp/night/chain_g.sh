#!/bin/sh
# 10-20 leaf-shadow rings: grove_coral_wall (showcase set 4 framing) A/B at 1080p Q3 on nt_e2: HEAD's shadow.hlsli
# vs the full-disc tap set (shd_vogel); after chain_v3
until [ -f /tmp/night/chain_v.done ]; do sleep 60; done
V="912.57,-3572.62,5.51,-98.20,2.30,17.00,grove_coral_wall"
GATE=4000 sh /tmp/night/shoot1.sh gA bin/nt_e2.exe "$V" --exposurelog
GATE=4000 sh /tmp/night/shoot1.sh gB bin/nt_e2.exe "$V" --exposurelog --shaderdir 'Z:\tmp\night\shd_vogel'
echo done > /tmp/night/chain_g.done
