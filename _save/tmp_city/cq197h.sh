#!/bin/bash
# 3s S2's extra shots: the side-yard row aerial again at noon (the 17:00 one has the side yards in the houses' shadow),
# over b021's houses from Calle Nueve; before nt_b34 (S), after nt_b36 (S2).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/cq80.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
wload() { local end=$((SECONDS + 1200)); while [ "$(awk '{print ($1 < 2.0) ? 0 : 1}' /proc/loadavg)" = 1 ] && [ $SECONDS -lt $end ]; do sleep 20; done; }
fatal() { local n; n=$(grep -cE 'Unhandled|FATAL' "$1" 2>/dev/null); echo "${n:-0}"; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
W="-std=c++17 -fsyntax-only -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess"
game() {   # name exe timeout args...
  local N=$1 E=$2 T=$3; shift 3
  rm -f $L
  wmem 3300
  TIMEOUT=$T EXE=$E WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" > /tmp/city/$N.out 2>&1
  local rc=$?
  cp $L /tmp/city/${N}_log.txt 2>/dev/null
  rm -f $L
  return $rc
}
nums() {   # name
  st "$1: load $(cut -d' ' -f1-3 /proc/loadavg); cells $(grep -hoE 'Hand buildings: cell [0-9,]+ \((near|far)\): [0-9]+ placements, [0-9]+ triangles, [0-9.]+ ms' /tmp/city/${1}_log.txt | sed -E 's/Hand buildings: cell ([0-9,]+) \((near|far)\): ([0-9]+) placements, ([0-9]+) triangles, ([0-9.]+) ms/\1\2:\3p:\4t:\5/' | tr '\n' ' '); resident at most $(grep -hoE 'resident [0-9]+ \([0-9]+ hand-built\), [0-9]+ triangles' /tmp/city/${1}_log.txt | sed -E 's/resident ([0-9]+) \(([0-9]+) hand-built\), ([0-9]+) triangles/\2 \1 \3/' | sort -g | tail -1); mem by t $(grep -oE 'autoplay t=[0-9.]+ .*mem [0-9]+ MB' /tmp/city/${1}_log.txt | sed -E 's/autoplay t=([0-9.]+) .*mem ([0-9]+) MB/\1:\2/' | tr '\n' ' '); $(grep -oE 'autoplay frames .*' /tmp/city/${1}_log.txt | head -1); $(fatal /tmp/city/${1}_log.txt) fatal"
}
pts() {   # name: the checkpoints
  grep -oE 'autoplay t=[0-9.]+ pos [-0-9.]+ [-0-9.]+ [-0-9.]+ state [0-9]+ health [0-9]+ veh [-0-9]+ speed [-0-9.]+ \| peds [0-9]+ vehicles [0-9]+ wanted [0-9]+' /tmp/city/${1}_log.txt | sed -E 's/autoplay //; s/ state [0-9]+ health [0-9]+ veh [-0-9]+//; s/\| //' | tr '\n' ';'
}
quiet() { local end=$((SECONDS + 3600)); while { ps -eo comm= | grep -qx 'showcase.exe' || [ "$(awk '{print ($1 < 2.0) ? 0 : 1}' /proc/loadavg)" = 1 ]; } && [ $SECONDS -lt $end ]; do sleep 20; done; }
st "--- cq197h (3s S2: the side-yard aerial at noon, before/after)"
cd /home/user/GTA-6-Claude-v0.5
D=/tmp/city/b36smoke
SH="--shot 1400,668,22,180,-40,12.0,s2_sideyard_aerial_1200"
for E in b34 b36; do
  O=after; [ $E = b34 ] && O=before
  quiet
  game ${E}_s2noon /tmp/city/nt_$E.exe 2700 --world=hand --width 1280 --height 720 $SH --shotdir "Z:\\tmp\\city\\b36smoke\\$O\\"
  st "$E S2 noon shot ($O) rc=$?: $(ls $D/$O/*.bmp 2>/dev/null | wc -l) shots; $(fatal /tmp/city/${E}_s2noon_log.txt) fatal"
  for f in $D/$O/*.bmp; do [ -f "$f" ] && convert "$f" -resize 1280x720 "${f%.bmp}.png" && rm -f "$f"; done
done
touch /tmp/city/cq197h_done
st "cq197h done"
