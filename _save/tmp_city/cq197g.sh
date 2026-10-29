#!/bin/bash
# 3s S2's games (on the lead's "quiet"; each still waits for no showcase.exe and load under 2.0): the before/after shots
# (before: nt_b34 = S, 478673d; after: nt_b36 = S2) of a casita front and a bungalow front at street level at 17:00, as
# S's, and an aerial of a side-yard row (b020's houses, from over Calle Nueve); the hand and procedural drives on nt_b36.
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
st "--- cq197g (3s S2: before/after shots, the hand and procedural drives on nt_b36)"
cd /home/user/GTA-6-Claude-v0.5
if [ -f /tmp/city/nt_b36.exe ] && [ -f /tmp/city/nt_b34.exe ]; then
  D=/tmp/city/b36smoke; mkdir -p $D/before $D/after $D/drive_hand $D/drive_proc; rm -f $D/before/*.bmp $D/after/*.bmp
  SH="--shot 1224.5,661,5.7,180,-6,17.0,s_casita_front_1700 --shot 1239,661,5.7,180,-6,17.0,s_bungalow_front_1700 --shot 1240,668,22,180,-40,17.0,s2_sideyard_aerial_1700"
  for E in b34 b36; do
    O=after; [ $E = b34 ] && O=before
    quiet
    game ${E}_s2shots /tmp/city/nt_$E.exe 2700 --world=hand --width 1280 --height 720 $SH --shotdir "Z:\\tmp\\city\\b36smoke\\$O\\"
    st "$E S2 shots ($O) rc=$?: $(ls $D/$O/*.bmp 2>/dev/null | wc -l) shots; $(fatal /tmp/city/${E}_s2shots_log.txt) fatal"
    for f in $D/$O/*.bmp; do [ -f "$f" ] && convert "$f" -resize 1280x720 "${f%.bmp}.png" && rm -f "$f"; done
  done
  quiet; l0=$(cut -d' ' -f1 /proc/loadavg)
  game b36_drive_hand25 /tmp/city/nt_b36.exe 1500 --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 30 --autoevery 10 \
    --renderevery 4 --quality 1 --shotdir "Z:\\tmp\\city\\b36smoke\\drive_hand\\"
  st "b36 hand drive (S2) rc=$?, load at start $l0, at end $(cut -d' ' -f1-3 /proc/loadavg), showcase $(ps -eo comm= | grep -cx 'showcase.exe'): $(fatal /tmp/city/b36_drive_hand25_log.txt) fatal; $(pts b36_drive_hand25)"; nums b36_drive_hand25
  st "b36 slowest streaming: $(grep -oE 'slowest streaming [0-9]: .*' /tmp/city/b36_drive_hand25_log.txt | tr '\n' ' ' | cut -c1-900)"
  find $D/drive_hand -name '*.bmp' -delete 2>/dev/null
  quiet
  game b36_drive_proc /tmp/city/nt_b36.exe 1500 --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 \
    --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\city\\b36smoke\\drive_proc\\"
  st "b36 procedural drive rc=$?: $(fatal /tmp/city/b36_drive_proc_log.txt) fatal; $(pts b36_drive_proc)"
  find $D/drive_proc -name '*.bmp' -delete 2>/dev/null
fi
touch /tmp/city/cq197_done
st "cq197g done"
