#!/bin/sh
# buildb.sh TAG TREE : builds TREE into bin/nt_<TAG>.exe (QUICK=1), log /tmp/night/build_<TAG>.log, marker .done
T=$1; TREE=${2:-/tmp/night/wb}
sh /tmp/night/gate.sh 2500
cd $TREE && QUICK=1 OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_$T.exe nice -n 10 sh build.sh > /tmp/night/build_$T.log 2>&1
echo "rc $?" > /tmp/night/build_$T.done
