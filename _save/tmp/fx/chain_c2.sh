#!/bin/sh
# character-shading before / after on main 111efa9 (latest face meshes): facecams in the faces agent's layout, the
# exe's own shaders vs lighting.hlsl + dynamic.hlsl of /tmp/fx/gd4 (--shaderdir); after the phase 3 self-test
cd /tmp/fx
until [ -f /tmp/fx/p3_selftest.done ]; do sleep 20; done
until grep -q "^exit" /tmp/fx/build_m111.log 2>/dev/null; do sleep 20; done
grep -q "^exit 0" /tmp/fx/build_m111.log || exit 1
export NT_D3D12_SLOT=1
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_args.sh c2a bin/nt_m111.exe /tmp/fx/fc_c1.args
W=1920 H=1080 SETTLE=12 TIMEOUT=3600 sh /tmp/fx/facecam_args.sh c2b bin/nt_m111.exe /tmp/fx/fc_c1.args --shaderdir 'Z:\tmp\fx\shd_c2'
echo done > /tmp/fx/chain_c2.done
