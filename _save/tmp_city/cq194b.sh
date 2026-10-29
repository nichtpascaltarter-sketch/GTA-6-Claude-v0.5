#!/bin/bash
# 3o the park edge (b026: dress.py's new region on Calle Nueve's north side) on R4d's tree = $S/b32: $S/b33. The builds, at
# nice 19: worldc (sections vs R4d's bin), worldv, CI x3, MinGW main.cpp, dress.py's py_compile, the census vs R4d's, the
# exe nt_b33, the harness, and the scratch exe nt_s94 (the park + evtime_patch.py). The games wait for his "quiet".
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
st "--- cq194b (3o park: worldc, worldv, CI, MinGW, census, exe, harness, the scratch exe; nice 19)"
cd $S/b33
wmem 2800
nice -n 19 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/c194_worldc > /tmp/city/c194_worldc_build.log 2>&1; b=$?
/tmp/city/c194_worldc --world=world --out=/tmp/city/c194_world_b33.bin > /tmp/city/c194_worldc.log 2>&1; r=$?
$S/b2w/binsec_b4 /tmp/city/c194_world_b33.bin > /tmp/city/c194_sections_b33.txt 2>&1
$S/b2w/binsec_b4 /tmp/city/c193_world_b32.bin > /tmp/city/c194_sections_b32.txt 2>&1
st "b33 worldc build rc=$b ($(grep -c warning /tmp/city/c194_worldc_build.log) warnings) run rc=$r: $(grep -E 'library|buildings|file' /tmp/city/c194_worldc.log | tr '\n' ' ' | cut -c1-300); sections vs R4d: $(diff <(grep kind /tmp/city/c194_sections_b32.txt) <(grep kind /tmp/city/c194_sections_b33.txt) | grep '^>' | awk '{print $2}' | tr '\n' ' ')"
rm -f /tmp/city/c194_worldc
wmem 2800
nice -n 19 g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/city/c194_worldv > /tmp/city/c194_worldv_build.log 2>&1; b=$?
/tmp/city/c194_worldv --world=world --no-cache > /tmp/city/c194_worldv.log 2>&1; r=$?
st "b33 worldv build rc=$b run rc=$r: $(tail -1 /tmp/city/c194_worldv.log | cut -c1-160)"
rm -f /tmp/city/c194_worldv
for T in world_format_test worldv_test worldmodel_test; do
  wmem 2800
  nice -n 19 g++ $CI tests/world/$T.cpp -o /tmp/city/c194_$T > /tmp/city/c194_${T}_build.log 2>&1; b=$?
  /tmp/city/c194_$T > /tmp/city/c194_${T}_run.log 2>&1; r=$?
  st "b33 CI $T build rc=$b run rc=$r: $(tail -1 /tmp/city/c194_${T}_run.log | cut -c1-100)"
  rm -f /tmp/city/c194_$T
done
mkdir -p build/gen
./build/embed_shaders src/shaders build/gen/shaders_embedded.h > /tmp/city/c194_embed.log 2>&1
wmem 2800
nice -n 19 x86_64-w64-mingw32-g++-posix $W -Ibuild/gen src/main.cpp > /tmp/city/c194_mingw_main.log 2>&1; r=$?
st "b33 MinGW -Wall main.cpp rc=$r errors $(grep -c 'error' /tmp/city/c194_mingw_main.log) warnings $(grep -c 'warning' /tmp/city/c194_mingw_main.log)"
python3 -I -m py_compile tools/author/dress.py && st "b33 dress.py py_compile OK" || st "b33 dress.py py_compile FAILED"
wmem 2800
nice -n 19 g++ -O2 -std=c++17 -DPS_SIDES -I. -I src $S/wm2/meshcensus4.cpp -o /tmp/city/c194_mc4_b33 -lpthread > /tmp/city/c194_mc4_b33_build.log 2>&1; b=$?
wmem 2000
nice -n 19 /tmp/city/c194_mc4_b33 > /tmp/city/c194_mc4_b33.txt 2> /tmp/city/c194_mc4_b33_err.txt; r=$?
st "procedural census R4d vs b33: build rc=$b run rc=$r: $(diff <(grep '^[CQTF] ' /tmp/city/c193_mc4_b32.txt) <(grep '^[CQTF] ' /tmp/city/c194_mc4_b33.txt) | grep -c '^[<>]') of $(grep -c '^[CQF] ' /tmp/city/c194_mc4_b33.txt) C/Q/F lines differ"
rm -f /tmp/city/c194_mc4_b33
wmem 2800
rm -f /tmp/city/nt_b33.exe build/gen/world_res.o build/gen/world.bin
OUT=/tmp/city/nt_b33.exe nice -n 19 ./build.sh > /tmp/city/build_b33.log 2>&1; rc=$?
st "nt_b33.exe build rc=$rc: $(ls -la /tmp/city/nt_b33.exe 2>/dev/null | awk '{print $5}') bytes; $(grep -cE 'warning|error' /tmp/city/build_b33.log) warning/error lines"
mkdir -p /tmp/city/hs33
wmem 2800
nice -n 19 $S/hs17/hs17 /tmp/city/c194_world_b33.bin /tmp/city/hs33 > /tmp/city/hs33/all.txt 2>&1; r=$?
find /tmp/city/hs33 -type f ! -name 'all.txt' -delete
st "b33 harness run rc=$r: $(grep -E '^[0-9]+ interiors' /tmp/city/hs33/all.txt | cut -c1-120); rooms $(grep -oE 'reaches [0-9]+ of [0-9]+ rooms' /tmp/city/hs33/all.txt | awk '{a+=$2; b+=$4} END {print a"/"b}'); tour legs off the walk $(grep -c 'leaves the walk' /tmp/city/hs33/all.txt); not all rooms: $(grep -E 'reaches' /tmp/city/hs33/all.txt | grep -vE 'reaches ([0-9]+) of \1 rooms' | head -3 | tr '\n' ' ')"
wmem 2800
cd $S/sc94/tree
rm -f /tmp/city/nt_s94.exe build/gen/world_res.o build/gen/world.bin
OUT=/tmp/city/nt_s94.exe nice -n 19 ./build.sh > /tmp/city/build_s94.log 2>&1; rc=$?
st "nt_s94.exe (scratch: the park + evtime_patch.py) build rc=$rc: $(ls -la /tmp/city/nt_s94.exe 2>/dev/null | awk '{print $5}') bytes; $(grep -cE 'warning|error' /tmp/city/build_s94.log) warning/error lines"
touch /tmp/city/cq194a_done
st "cq194b done"
