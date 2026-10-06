#!/bin/sh
# after batch 7's proof shots: (1) once the dev14 QUICK build (PID 24570) ends, an O2 build of main b907098; (2) after the
# hats runs (chain43), crowd perf A/B: main, batch 7, main, batch 7; (3) the greet context run; (4) the buzz-cut test
cd /tmp/faces
while kill -0 24570 2>/dev/null; do sleep 15; done
cd /tmp/faces/m907 && OUT=/tmp/faces/nt_m907_o2.exe sh build.sh > /tmp/faces/build_m907_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_m907_o2.log
cd /tmp/faces
while [ ! -f /tmp/faces/chain43.done ]; do sleep 20; done
if grep -q "^exit 0" /tmp/faces/build_m907_o2.log; then
  for i in 1 2; do
    /tmp/faces/perf.sh /tmp/faces/nt_m907_o2.exe m907_$i > /tmp/faces/perf_m907_$i.out 2>&1
    /tmp/faces/perf.sh /tmp/faces/nt_b7_o2.exe b7_$i > /tmp/faces/perf_b7_$i.out 2>&1
  done
fi
echo perfdone > /tmp/faces/chain48.perf
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/faces/greet_b7
WINEPREFIX=/tmp/wine_faces EXE=/tmp/faces/nt_b7_o2.exe TIMEOUT=1800 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay greet --shotdir 'Z:\tmp\faces\greet_b7\' > /tmp/faces/run_greet_b7.log 2>&1
cp /tmp/wine_faces/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/faces/log_greet_b7.txt 2>/dev/null
for f in /tmp/faces/greet_b7/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
cd /tmp/faces
if [ -f /tmp/faces/nt_d14.exe ] && [ /tmp/faces/nt_d14.exe -nt /tmp/faces/dev14/src/anim/hair.cpp ]; then
  EXE=/tmp/faces/nt_d14.exe TIMEOUT=2400 /tmp/faces/shoot2.sh buzz_off --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzz.args) > /tmp/faces/shoot_buzz_off.out 2>&1
  FACES_BUZZ=0.007,0.011,0.011,0.011,0.6,0.4 EXE=/tmp/faces/nt_d14.exe TIMEOUT=2400 /tmp/faces/shoot2.sh buzz_on --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzz.args) > /tmp/faces/shoot_buzz_on.out 2>&1
fi
echo done > /tmp/faces/chain48.done
