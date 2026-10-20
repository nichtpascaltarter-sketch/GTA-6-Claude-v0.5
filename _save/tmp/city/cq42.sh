#!/bin/bash
# After snapshot 57 (HEAD 2b3102e): step 2 rebased ($S/s0c: app.cpp merged onto AI's 6f3ee5da, .gitattributes), step 3
# ($S/s1c: + worldc's LF manifest hash, CRLF/BOM test cases) and step 4 ($S/s2c). MinGW syntax of s0c and s1c; worldc on
# LF, CRLF and BOM+CRLF copies of world/ (world.bin must be byte-identical); ntwfmt; world_format_test with CI's GCC
# flags (timed); clang scans; the load test; then the step-3 exe and, after cq39's games, one hand shot.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$S/s0test
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/s1c.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
now() { date +%s.%N; }
dt() { awk "BEGIN{printf \"%.1f\", $2 - $1}"; }
st "cq42 start"
for TREE in s0c s1c; do
  cd $S/$TREE && mkdir -p build/gen && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h > /dev/null
  wmem 2500
  nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_$TREE.log 2>&1; st "$TREE game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_$TREE.log) warnings=$(grep -c 'warning' /tmp/city/syntax_$TREE.log)"
done
C=$S/crlftest
for TREE in s1c s2c; do
  cd $S/$TREE
  wmem 2000
  nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $T/worldc_$TREE.log 2>&1; st "$TREE worldc build rc=$? ($(grep -c 'warning:' $T/worldc_$TREE.log) warnings)"
  ./build/worldc --world=world --out=build/gen/world.bin > $T/worldc_${TREE}_run.log 2>&1; st "$TREE worldc run rc=$?: $(tail -1 $T/worldc_${TREE}_run.log)"
  ./build/worldc --world=$C/crlf --out=$C/crlf/world_$TREE.bin > /dev/null 2>&1; r1=$?
  ./build/worldc --world=$C/bomcrlf --out=$C/bomcrlf/world_$TREE.bin > /dev/null 2>&1; r2=$?
  st "$TREE worldc on CRLF rc=$r1: $(cmp -s build/gen/world.bin $C/crlf/world_$TREE.bin && echo identical || echo DIFFERS); on BOM+CRLF rc=$r2: $(cmp -s build/gen/world.bin $C/bomcrlf/world_$TREE.bin && echo identical || echo DIFFERS)"
  wmem 2000
  nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $T/ntwfmt_$TREE.log 2>&1; st "$TREE ntwfmt build rc=$? ($(grep -c 'warning:' $T/ntwfmt_$TREE.log) warnings)"
  ./build/ntwfmt --check world > $T/ntwfmt_${TREE}_world.log 2>&1; st "$TREE ntwfmt --check world rc=$?: $(tail -1 $T/ntwfmt_${TREE}_world.log)"
  wmem 2500
  t0=$(now); nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined tests/world/world_format_test.cpp -o /tmp/city/neontide-world-format-test-$TREE > $T/wft_$TREE.log 2>&1; rc=$?; t1=$(now)
  /tmp/city/neontide-world-format-test-$TREE > $T/wft_${TREE}_run.log 2>&1; rc2=$?; t2=$(now)
  st "$TREE world_format_test (CI flags) build rc=$rc in $(dt $t0 $t1) s, run rc=$rc2 in $(dt $t1 $t2) s: $(tail -1 $T/wft_${TREE}_run.log)"
  rm -f /tmp/city/neontide-world-format-test-$TREE
  nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic tests/world/world_format_test.cpp > $T/clang_wft_$TREE.log 2>&1
  nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic -I. tools/worldc.cpp > $T/clang_worldc_$TREE.log 2>&1
  nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic -I. tools/ntwfmt.cpp > $T/clang_ntwfmt_$TREE.log 2>&1
  st "$TREE clang scans: test $(grep -c 'warning:\|error:' $T/clang_wft_$TREE.log), worldc $(grep -c 'warning:\|error:' $T/clang_worldc_$TREE.log), ntwfmt $(grep -c 'warning:\|error:' $T/clang_ntwfmt_$TREE.log) diagnostics"
done
python3 $T/doc_examples.py $T/docex $S/hand0/docs/HANDBUILT_WORLD.md $S/s2c/docs/NTW_FORMAT.md > /dev/null
cd $S/s2c && ./build/ntwfmt --check $T/samples > $T/ntwfmt_samples.log 2>&1; st "s2c ntwfmt --check samples rc=$?: $(tail -1 $T/ntwfmt_samples.log)"
cd $S/s2c && ./build/ntwfmt --check $T/docex > $T/ntwfmt_docex.log 2>&1; st "s2c ntwfmt --check doc examples rc=$?: $(tail -1 $T/ntwfmt_docex.log)"
st "s2c $(python3 $T/doccheck.py $S/s2c | tail -1)"
cmp -s $S/s1c/build/gen/world.bin $S/s2c/build/gen/world.bin; st "world.bin step 3 vs step 4: $([ $? = 0 ] && echo identical || echo DIFFERS); step 3 vs s1b: $(cmp -s $S/s1c/build/gen/world.bin $S/s1b/build/gen/world.bin && echo identical || echo DIFFERS)"
wmem 2000
cd $S/s1c && nice -n 5 g++ -std=c++17 -O2 -I. $T/handload.cpp -o $T/handload_s1c -lpthread > $T/handload_s1c_build.log 2>&1; st "s1c handload build rc=$?"
wmem 1500
cd $S/s1c && $T/handload_s1c build/gen/world.bin > $T/handload_s1c_run.log 2>&1; st "s1c handload run rc=$?: $(tail -1 $T/handload_s1c_run.log); $(grep 'damaged table' $T/handload_s1c_run.log)"
rm -f $T/handload_s1c
rm -f $S/s1c/build/gen/world_res.o $S/s1c/build/gen/world.bin
wmem 2800
cd $S/s1c && OUT=/tmp/city/nt_s1c.exe nice -n 6 ./build.sh > /tmp/city/build_s1c.log 2>&1; st "nt_s1c build rc=$?: $(ls -la /tmp/city/nt_s1c.exe 2>/dev/null | awk '{print $5}') bytes; $(grep 'worldc:.*world.bin' /tmp/city/build_s1c.log)"
[ -f /tmp/city/nt_s1c.exe ] || { st "cq42: no exe"; exit 1; }
while ! grep -q "cq39 done" /tmp/city/s0b.status && pgrep -f "bash /tmp/city/cq3[9].sh" > /dev/null; do sleep 20; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/s1hand; rm -f /tmp/city/s1hand/*.bmp; rm -f $L
wmem 3300; TIMEOUT=900 EXE=/tmp/city/nt_s1c.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --world=hand --width 960 --height 540 \
  --shot 3000,-300,320,0,-30,11,s1_hand_downtown_aerial --shotdir 'Z:\tmp\city\s1hand\' > /tmp/city/s1hand.out 2>&1
rc=$?; cp $L /tmp/city/s1hand_log.txt 2>/dev/null
st "s1c hand shot rc=$rc: $(ls /tmp/city/s1hand/*.bmp 2>/dev/null | wc -l) shots; $(grep -cE 'Unhandled|FATAL' /tmp/city/s1hand_log.txt) fatal; $(grep -o 'Hand world: terrain.*' /tmp/city/s1hand_log.txt | head -1)"
st "cq42 done"
