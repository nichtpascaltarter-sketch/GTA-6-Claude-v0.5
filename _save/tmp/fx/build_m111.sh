#!/bin/sh
# main 111efa9 (latest face meshes) for the character-shading before / after
cd /tmp/fx/m111 && QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_m111.exe nice -n 10 sh build.sh > /tmp/fx/build_m111.log 2>&1; echo "exit $?" >> /tmp/fx/build_m111.log
