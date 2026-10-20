#!/bin/sh
# Takes over from chain_v2k while td3 (top-down, stoop-step rings) runs: the character-shading facecams before / after
# (the faces agent's layout: c27 / c38 at night 0.5 and 1 m, by day 0.5 m, c17 brows 0.3 m), close-ups of a Grove palm
# crown and hedge with phase 2 and 3, and the phase 3 timing run. One game at a time on slot 4.
cd /tmp/fx
until grep -q "^exit" /tmp/fx/td3.out 2>/dev/null; do sleep 15; done
echo done > /tmp/fx/td3.done
SD='--shaderdir Z:\tmp\fx\shd_p2'
SC='--shaderdir Z:\tmp\fx\shd_c1'
export NT_D3D12_SLOT=1
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_args.sh c1a bin/nt_p3c.exe /tmp/fx/fc_c1.args $SD
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_args.sh c1b bin/nt_p3c.exe /tmp/fx/fc_c1.args $SC
CL="1954.3,-3496.3,10,90,-12,15,palm_crown 1985,-3501.5,6.5,0,-25,15,hedge 1962,-3497,6.5,90,-30,15,palm_trunk"
SHOTS="$CL" W=1920 H=1080 EXE=bin/nt_p2c.exe TIMEOUT=2400 sh /tmp/fx/shoot.sh ps2 --settle 12 $SD > /tmp/fx/ps2.out 2>&1
SHOTS="$CL" W=1920 H=1080 EXE=bin/nt_p3c.exe TIMEOUT=2400 sh /tmp/fx/shoot.sh ps3 --settle 12 $SD > /tmp/fx/ps3.out 2>&1
echo done > /tmp/fx/ps.done
[ -f /tmp/fx/hold_p3t ] && until [ ! -f /tmp/fx/hold_p3t ]; do sleep 15; done
EXE=bin/nt_p3c.exe P=p3t START=4 COUNT=10 EXTRA="--synctimers --gfxstats $SD" sh /tmp/fx/tour_multi.sh
echo done > /tmp/fx/chain_v2l.done
