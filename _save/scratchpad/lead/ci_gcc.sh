#!/bin/bash
# The lead's copy of CI's GCC job steps, run on an exported tree (sequential, memory-gated). usage: ci_gcc.sh DIR LOG
cd "$1" || exit 2
LOG="$2"; : > "$LOG"
gate() { sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 1500; }
step() { name="$1"; shift; gate; echo "== $name" >> "$LOG"; if ( "$@" ) >> "$LOG" 2>&1; then echo "PASS $name" >> "$LOG"; else echo "FAIL $name" >> "$LOG"; fi; }
F="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
O=$(mktemp -d /tmp/leadci.XXXXXX)
step bitscan bash -c "g++ $F tests/compiler/bit_scan_test.cpp -o $O/bs && $O/bs"
step updater bash -c "g++ $F tests/updater/update_logic_test.cpp -o $O/ul && $O/ul"
step payload bash -c "[ ! -f tools/payload.cpp ] || { g++ $F tools/payload.cpp -o $O/pl && sh tests/updater/payload_test.sh $O/pl; }"
step worldformat bash -c "g++ $F tests/world/world_format_test.cpp -o $O/wf && $O/wf"
step worldvtest bash -c "g++ $F tests/world/worldv_test.cpp -o $O/wvt && $O/wvt"
step worldmodel bash -c "g++ $F tests/world/worldmodel_test.cpp -o $O/wm && $O/wm"
step worldv bash -c "g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o $O/wv && $O/wv --world=world --no-cache"
rm -rf "$O"
echo "CI GCC DONE" >> "$LOG"
