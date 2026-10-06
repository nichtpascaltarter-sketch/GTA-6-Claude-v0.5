#!/bin/sh
# sharpening A/B at 1 m (faces agent's layout): nt_n0 vs nt_n0 + shd_sharp (HEAD post.hlsl + clamped sharpening)
until [ -f /tmp/night/chain_n1.done ]; do sleep 20; done
sh /tmp/night/facecam.sh s0 bin/nt_n0.exe /tmp/night/fc_sharp.args
sh /tmp/night/facecam.sh s1 bin/nt_n0.exe /tmp/night/fc_sharp.args --shaderdir 'Z:\tmp\night\shd_sharp'
echo done > /tmp/night/chain_fc.done
