#!/bin/sh
# 10-20 leaf rings + held brake lamps sign-off (RESUMABLE, relaunch after a restart): build nt_r1 (tree r) ->
# selftest -> shadow A/B on nt_r1 (before = HEAD's shadow.hlsli via --shaderdir): grove (day), calle (day), downtown
# street (night), 1080p Q3 -> traffic at 22:00 720p: before nt_sf (no hold) / after nt_r1 (queue at a red)
[ -f /tmp/night/build_r1.done ] || sh /tmp/night/buildb.sh r1 /tmp/night/r
grep -q "rc 0" /tmp/night/build_r1.done || { echo build-failed > /tmp/night/chain_r.done; exit 1; }
[ -f /tmp/night/log_r1_selftest.txt ] || sh /tmp/night/selftest.sh r1 bin/nt_r1.exe
for v in "912.57,-3572.62,5.51,-98.20,2.30,17.00,grove_coral_wall" "1771.00,357.10,5.47,-65.00,3.00,14.00,calle_luna_street_day" "3168.00,-242.90,3.56,-70.00,8.00,21.50,downtown_street_night"; do
  GATE=4000 sh /tmp/night/shoot1.sh rb bin/nt_r1.exe "$v" --exposurelog --shaderdir 'Z:\tmp\night\shd_shhead'
  GATE=4000 sh /tmp/night/shoot1.sh ra bin/nt_r1.exe "$v" --exposurelog
done
cd /home/user/GTA-6-Claude-v0.5
for T in rtA rtB; do
  [ -f /tmp/night/shots/$T.done ] && continue
  X=bin/nt_sf.exe; [ $T = rtB ] && X=bin/nt_r1.exe
  exec 8>>/tmp/night/game.lock; flock 8
  rm -rf /tmp/night/shots/$T; mkdir -p /tmp/night/shots/$T
  sh /tmp/night/gate.sh 3200
  NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$X TIMEOUT=5400 nice -n 10 tools/run.sh --autotest --width 1280 --height 720 --quality 2 --play --autoplay traffic --time 22 --autoduration 16 --autoevery 1 --shotdir "Z:\\tmp\\night\\shots\\$T\\" > /tmp/night/shots/run_$T.log 2>&1 8>&-
  echo "exit $?" >> /tmp/night/shots/run_$T.log
  cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_$T.txt
  for f in /tmp/night/shots/$T/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
  echo done > /tmp/night/shots/$T.done
  flock -u 8; exec 8>&-
done
echo done > /tmp/night/chain_r.done
