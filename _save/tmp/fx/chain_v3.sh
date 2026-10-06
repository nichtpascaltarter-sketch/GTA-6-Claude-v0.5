#!/bin/sh
# after the phase 3 timing run (chain_v2l): the phase 3 self-test with the final exe (nt_p3d, tuned leaf canopy), the
# hedge and palm close-ups (phase 2 nt_p2c vs phase 3 nt_p3d), then the character-shading facecams on main 111efa9
# (latest face meshes): its own shaders vs lighting.hlsl + dynamic.hlsl of /tmp/fx/gd4. One game at a time, slot 4.
cd /tmp/fx
SD='--shaderdir Z:\tmp\fx\shd_p2'
until [ -f /tmp/fx/chain_v2l.done ]; do sleep 20; done
until grep -q "^exit" /tmp/fx/build_p3d.log 2>/dev/null; do sleep 20; done
if grep -q "^exit 0" /tmp/fx/build_p3d.log; then
  cd /home/user/GTA-6-Claude-v0.5
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_p3d.exe TIMEOUT=1200 nice -n 10 tools/run.sh --gfxselftest > /tmp/fx/run_p3_selftest.log 2>&1
  echo "exit $?" >> /tmp/fx/run_p3_selftest.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_p3_selftest.txt
  echo done > /tmp/fx/p3_selftest.done
  cd /tmp/fx
  CL="1985,-3501.5,6.5,0,-25,15,hedge 1951.1,-3502.2,6,45,15,15,palm_a 1945.1,-3506.2,16,0,-35,15,palm_b"
  SHOTS="$CL" W=1920 H=1080 EXE=bin/nt_p2c.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh pt2 --settle 12 $SD > /tmp/fx/pt2.out 2>&1
  SHOTS="$CL" W=1920 H=1080 EXE=bin/nt_p3d.exe TIMEOUT=2400 NT_D3D12_SLOT=1 sh /tmp/fx/shoot.sh pt3 --settle 12 $SD > /tmp/fx/pt3.out 2>&1
  echo done > /tmp/fx/pt.done
fi
until grep -q "^exit" /tmp/fx/build_m111.log 2>/dev/null; do sleep 20; done
if grep -q "^exit 0" /tmp/fx/build_m111.log; then
  export NT_D3D12_SLOT=1
  W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_args.sh c2a bin/nt_m111.exe /tmp/fx/fc_c1.args
  W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_args.sh c2b bin/nt_m111.exe /tmp/fx/fc_c1.args --shaderdir 'Z:\tmp\fx\shd_c2'
fi
echo done > /tmp/fx/chain_v3.done
