#!/bin/sh
# debug views on nt_b1: ambient irradiance (3) and aerial perspective (8), split screen, 960x540 Q1
V="2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night -8201.45,-9279.96,4.22,3.24,0.32,21.00,mimo_motel_night 5078.76,1502.16,3.48,-127.20,25.00,20.50,beach_moon_night"
SHOTS="$V" EXE=bin/nt_b1.exe W=960 H=540 Q=1 sh /tmp/night/shoot.sh g3 --debugsplit 3 --shaderdir 'Z:\tmp\night\shd_b2'
SHOTS="$V" EXE=bin/nt_b1.exe W=960 H=540 Q=1 sh /tmp/night/shoot.sh g8 --debugsplit 8 --shaderdir 'Z:\tmp\night\shd_b2'
echo done > /tmp/night/chain_dbg1.done
