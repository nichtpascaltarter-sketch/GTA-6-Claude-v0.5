#!/bin/bash
# 3o the park edge's games (on the lead's "quiet"; each still waits for no showcase.exe and load under 2.0): 3 shots (a
# day view north from Calle Nueve, the path at night, an aerial); the park-cost game, before (nt_b32, R4d) and after
# (nt_b33): one view across the park held for 3 shots of 30 frames each at 960x540, timed by the log between saves; the
# hand drives on nt_b32 and nt_b33 back to back, the procedural drive; and if the park's drive has a streaming frame over
# 3 ms that uploaded nothing, the scratch exe nt_s94's hand drive (evictions timed).
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
st "--- cq194g (3o park: shots, the park-cost pair, the hand drives R4d/park, the procedural drive, and maybe the scratch eviction drive)"
cd /home/user/GTA-6-Claude-v0.5
if [ -f /tmp/city/nt_b33.exe ] && [ -f /tmp/city/nt_b32.exe ]; then
  D=/tmp/city/b33smoke; mkdir -p $D/shots $D/cost $D/drive_hand $D/drive_proc; rm -f $D/shots/*.bmp
  SH="--shot 1406,653,5.5,0,-4,17.0,park_porch_north_1700 --shot 1380,679.6,5.4,270,-3,21.0,park_path_night_2100 --shot 1575,800,110,180,-40,17.0,park_aerial_1700"
  quiet
  game b33_shots /tmp/city/nt_b33.exe 2700 --world=hand --width 1280 --height 720 $SH --shotdir "Z:\\tmp\\city\\b33smoke\\shots\\"
  st "b33 shots rc=$?: $(ls $D/shots/*.bmp 2>/dev/null | wc -l) shots; $(fatal /tmp/city/b33_shots_log.txt) fatal; $(grep -E 'Hand interiors' /tmp/city/b33_shots_log.txt | head -1 | cut -c1-200)"; nums b33_shots
  for f in $D/shots/*.bmp; do [ -f "$f" ] && convert "$f" -resize 1280x720 "${f%.bmp}.png" && rm -f "$f"; done
  CV="1400,653,5.5,300,-3,17.0"
  for E in b32 b33; do
    quiet; l0=$(cut -d' ' -f1 /proc/loadavg)
    game ${E}_parkcost /tmp/city/nt_$E.exe 2700 --world=hand --width 960 --height 540 --settle 30 --shot $CV,pc_warm --shot $CV,pc_m1 --shot $CV,pc_m2 --shotdir "Z:\\tmp\\city\\b33smoke\\cost\\"
    st "$E park-cost game rc=$?, load at start $l0, at end $(cut -d' ' -f1-3 /proc/loadavg), showcase $(ps -eo comm= | grep -cx 'showcase.exe'): $(fatal /tmp/city/${E}_parkcost_log.txt) fatal; saves $(grep -oE '^\[ *[0-9.]+\] Saved .*pc_[a-z0-9]+' /tmp/city/${E}_parkcost_log.txt | sed -E 's/^\[ *([0-9.]+)\] Saved .*(pc_[a-z0-9]+)/\2:\1/' | tr '\n' ' '); ms per frame $(grep -oE '^\[ *[0-9.]+\] Saved .*pc_[a-z0-9]+' /tmp/city/${E}_parkcost_log.txt | sed -E 's/^\[ *([0-9.]+)\].*/\1/' | awk 'NR>1 {printf "%.1f ", ($1-p)*1000/30} {p=$1}')"
    rm -f $D/cost/*.bmp
  done
  for E in b32 b33; do
    quiet; l0=$(cut -d' ' -f1 /proc/loadavg)
    game ${E}_drive_hand22 /tmp/city/nt_$E.exe 1500 --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 30 --autoevery 10 \
      --renderevery 4 --quality 1 --shotdir "Z:\\tmp\\city\\b33smoke\\drive_hand\\"
    st "$E hand drive (park) rc=$?, load at start $l0, at end $(cut -d' ' -f1-3 /proc/loadavg), showcase $(ps -eo comm= | grep -cx 'showcase.exe'): $(fatal /tmp/city/${E}_drive_hand22_log.txt) fatal; $(pts ${E}_drive_hand22)"; nums ${E}_drive_hand22
    st "$E slowest streaming: $(grep -oE 'slowest streaming [0-9]: .*' /tmp/city/${E}_drive_hand22_log.txt | tr '\n' ' ' | cut -c1-900)"
    find $D/drive_hand -name '*.bmp' -delete 2>/dev/null
  done
  quiet
  game b33_drive_proc /tmp/city/nt_b33.exe 1500 --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 \
    --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\city\\b33smoke\\drive_proc\\"
  st "b33 procedural drive rc=$?: $(fatal /tmp/city/b33_drive_proc_log.txt) fatal; $(pts b33_drive_proc)"
  find $D/drive_proc -name '*.bmp' -delete 2>/dev/null
  if grep -oE "slowest streaming [0-9]: [0-9.]+ ms at t=[0-9.]+, streaming [0-9.]+ ms, uploaded nothing" /tmp/city/b33_drive_hand22_log.txt | awk -F'streaming ' '{split($3, a, " "); if (a[1] + 0 > 3.0) f = 1} END {exit !f}' && [ -f /tmp/city/nt_s94.exe ]; then
    st "b33's drive has a streaming frame over 3 ms that uploaded nothing: the scratch eviction drive"
    quiet; l0=$(cut -d' ' -f1 /proc/loadavg)
    game s94_drive_hand /tmp/city/nt_s94.exe 1500 --world=hand --width 960 --height 540 --play --autoplay drive --autoduration 30 --autoevery 10 \
      --renderevery 4 --quality 1 --shotdir "Z:\\tmp\\city\\b33smoke\\drive_hand\\"
    st "s94 hand drive rc=$?, load at start $l0, at end $(cut -d' ' -f1-3 /proc/loadavg): $(fatal /tmp/city/s94_drive_hand_log.txt) fatal; $(grep -c 'slowupdate2' /tmp/city/s94_drive_hand_log.txt) slow updates, $(grep -c 'evict ' /tmp/city/s94_drive_hand_log.txt) evictions over 0.3 ms"
    find $D/drive_hand -name '*.bmp' -delete 2>/dev/null
  else
    st "b33's drive has no streaming frame over 3 ms without an upload: no scratch eviction drive"
  fi
fi
touch /tmp/city/cq194_done
st "cq194g done"
