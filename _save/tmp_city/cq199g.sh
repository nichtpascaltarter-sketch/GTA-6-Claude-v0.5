#!/bin/bash
# 3t V3's games (on the lead's "quiet"; each still waits for no showcase.exe and load under 2.0): the before/after shots
# with row 3's cameras (R3a's and R3b's: the two aerials and two Calle Siete views at 15:30; before: nt_b37 = V1, V3's
# base; after: nt_b38), the frame-log pair of hand drives back to back (nt_b37, the base's exe, then nt_b38), and the
# procedural drive on nt_b38.
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
st "--- cq199g (3t V3: before/after shots, the hand drives nt_b37/nt_b38, the procedural drive on nt_b38)"
cd /home/user/GTA-6-Claude-v0.5
if [ -f /tmp/city/nt_b38.exe ] && [ -f /tmp/city/nt_b37.exe ]; then
  D=/tmp/city/b38smoke; mkdir -p $D/before $D/after $D/drive_hand $D/drive_proc; rm -f $D/before/*.bmp $D/after/*.bmp
  SH="--shot 1399,600,95,180,-40,15.5,r3a_aerial_1530 --shot 1455,451,7.2,70,-7,15.5,r3a_b015_calle_siete_1530 --shot 1740,600,95,180,-40,15.5,r3b_aerial_1530 --shot 1738,451,7.2,70,-7,15.5,r3b_b017_calle_siete_1530"
  for E in b37 b38; do
    O=after; [ $E = b37 ] && O=before
    quiet
    game ${E}_vshots /tmp/city/nt_$E.exe 2700 --world=hand --width 1280 --height 720 $SH --shotdir "Z:\\tmp\\city\\b38smoke\\$O\\"
    st "$E V1 shots ($O) rc=$?: $(ls $D/$O/*.bmp 2>/dev/null | wc -l) shots; $(fatal /tmp/city/${E}_vshots_log.txt) fatal"
    for f in $D/$O/*.bmp; do [ -f "$f" ] && convert "$f" -resize 1280x720 "${f%.bmp}.png" && rm -f "$f"; done
  done
  for E in b37 b38; do
    quiet; l0=$(cut -d' ' -f1 /proc/loadavg)
    game ${E}_drive_hand27 /tmp/city/nt_$E.exe 1500 --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 30 --autoevery 10 \
      --renderevery 4 --quality 1 --shotdir "Z:\\tmp\\city\\b38smoke\\drive_hand\\"
    st "$E hand drive (V3 pair) rc=$?, load at start $l0, at end $(cut -d' ' -f1-3 /proc/loadavg), showcase $(ps -eo comm= | grep -cx 'showcase.exe'): $(fatal /tmp/city/${E}_drive_hand27_log.txt) fatal; $(pts ${E}_drive_hand27)"; nums ${E}_drive_hand27
    st "$E slowest streaming: $(grep -oE 'slowest streaming [0-9]: .*' /tmp/city/${E}_drive_hand27_log.txt | tr '\n' ' ' | cut -c1-900)"
    find $D/drive_hand -name '*.bmp' -delete 2>/dev/null
  done
  quiet
  game b38_drive_proc /tmp/city/nt_b38.exe 1500 --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 \
    --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\city\\b38smoke\\drive_proc\\"
  st "b38 procedural drive rc=$?: $(fatal /tmp/city/b38_drive_proc_log.txt) fatal; $(pts b38_drive_proc)"
  find $D/drive_proc -name '*.bmp' -delete 2>/dev/null
fi
touch /tmp/city/cq199_done
st "cq199g done"
