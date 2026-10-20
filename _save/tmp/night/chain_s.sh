#!/bin/sh
# 10-20 siren evidence: build nt_v2s (tree v2 + LOCAL-ONLY sirentest autoplay), then sirentest at 22:00 with Reduce
# Flashing off (vsB0) and on (vsB1), 16 shots 0.1 s apart. Games take /tmp/night/game.lock (one game of mine at a time).
[ -f /tmp/night/build_v2s.done ] || sh /tmp/night/buildb.sh v2s /tmp/night/v2
grep -q "rc 0" /tmp/night/build_v2s.done || { echo build-failed > /tmp/night/chain_s.done; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
SI=/tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/settings.ini
srun() {   # TAG RF
  T=$1
  [ -f /tmp/night/shots/$T.done ] && return 0
  exec 8>>/tmp/night/game.lock; flock 8
  rm -rf /tmp/night/shots/$T; mkdir -p /tmp/night/shots/$T
  sh /tmp/night/gate.sh 3200
  if [ "$2" = 1 ]; then echo "reduce_flashing=1" > $SI; else rm -f $SI; fi
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_v2s.exe TIMEOUT=3600 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay sirentest --time 22 --autoduration 5 --autoevery 100 --shotdir "Z:\\tmp\\night\\shots\\$T\\" > /tmp/night/shots/run_$T.log 2>&1 8>&-
  echo "exit $?" >> /tmp/night/shots/run_$T.log
  rm -f $SI
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_$T.txt
  for f in /tmp/night/shots/$T/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
  echo done > /tmp/night/shots/$T.done
  flock -u 8; exec 8>&-
}
srun vsB0 0
srun vsB1 1
echo done > /tmp/night/chain_s.done
