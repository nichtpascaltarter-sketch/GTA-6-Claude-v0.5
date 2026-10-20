#!/bin/bash
# After cq65: the readRoads fix (the network is left empty on failure) and roomlight.h's comment change. The tests with
# CI flags, a C++ sweep of roomLight() against the Python port, worldc and rbcheck again, MinGW. One build at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rb2.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
# (no wait for cq65: its game runs wait on memory behind another agent's long run; builds may overlap a game)
st "cq66 start"
# --- roomlight.h (comment change), and the sweep
cd $S/rlt
wmem 2000
nice -n 5 g++ $CI tests/world/worldmodel_test.cpp -o /tmp/city/rlt_wmt2 > /tmp/city/rlt_wmt2_build.log 2>&1; st "rlt worldmodel_test build rc=$? ($(grep -c 'error\|warning' /tmp/city/rlt_wmt2_build.log) error/warning lines)"
/tmp/city/rlt_wmt2 > /tmp/city/rlt_wmt2_run.log 2>&1; st "rlt worldmodel_test run rc=$?: $(tail -1 /tmp/city/rlt_wmt2_run.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tests/world/worldmodel_test.cpp > /tmp/city/rlt_mingw2.log 2>&1; st "rlt MinGW worldmodel_test rc=$? errors $(grep -c 'error' /tmp/city/rlt_mingw2.log) warnings $(grep -c 'warning' /tmp/city/rlt_mingw2.log)"
wmem 1500
nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I$S/rlt $S/rl/rlsweep.cpp -o /tmp/city/rlsweep > /tmp/city/rlsweep_build.log 2>&1; st "rlsweep build rc=$?"
/tmp/city/rlsweep > /tmp/city/rlsweep.txt; python3 -I $S/rl/port.py $S/rlt/src/world/roomlight.h $S/rlt/tests/world/worldmodel_test.cpp /tmp/city/msg/roomlight_table.hlsl /tmp/city/rlsweep.txt > /tmp/city/rlsweep_port.log 2>&1
st "port vs C++ rc=$?: $(grep 'port:' /tmp/city/rlsweep_port.log | tr '\n' ';')"
touch /tmp/city/cq66_rlt.marker
# --- roads into world.bin (readRoads fix)
cd $S/rb
wmem 2000
nice -n 5 g++ $CI tests/world/worldmodel_test.cpp -o /tmp/city/rb_wmt2 > /tmp/city/rb_wmt2_build.log 2>&1; st "rb worldmodel_test build rc=$? ($(grep -c 'error\|warning' /tmp/city/rb_wmt2_build.log) error/warning lines)"
/tmp/city/rb_wmt2 > /tmp/city/rb_wmt2_run.log 2>&1; st "rb worldmodel_test run rc=$?: $(tail -1 /tmp/city/rb_wmt2_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/rb_worldc2 > /tmp/city/rb_worldc2_build.log 2>&1; st "rb worldc build rc=$? ($(grep -c warning /tmp/city/rb_worldc2_build.log) warnings)"
/tmp/city/rb_worldc2 --world=world --out=/tmp/city/rb_world2.bin > /tmp/city/rb_worldc2_world.log 2>&1; st "rb worldc on world/ rc=$?: vs 4.1's: $(cmp /tmp/city/rb_world2.bin $S/s41/build/gen/world.bin && echo identical)"
T=/tmp/city/rbworld; F=tests/world/fixtures/model
/tmp/city/rb_worldc2 --world=$T --out=/tmp/city/rbroads_world2.bin > /tmp/city/rb_worldc2_roads.log 2>&1; st "rb worldc on the roads world rc=$?: $(grep 'roads' /tmp/city/rb_worldc2_roads.log | head -1); vs cq65's: $(cmp /tmp/city/rbroads_world2.bin /tmp/city/rbroads_world.bin && echo identical)"
mkdir -p $T/districts/cl/blocks/b001; cp $F/districts/cl/blocks/b001/block.ntw $T/districts/cl/blocks/b001/
/tmp/city/rb_worldc2 --world=$T --out=/tmp/city/rbroads_world2_bad.bin > /tmp/city/rb_worldc2_block.log 2>&1; st "rb worldc with a block file rc=$? (expect 1): $(grep -c 'no compiler for block' /tmp/city/rb_worldc2_block.log) 'no compiler' errors"
rm -rf $T/districts/cl/blocks
wmem 1500
nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I. wmtools/rbcheck.cpp -o /tmp/city/rbcheck2 > /tmp/city/rbcheck2_build.log 2>&1; st "rbcheck build rc=$?"
/tmp/city/rbcheck2 /tmp/city/rbroads_world2.bin $T > /tmp/city/rbcheck2.log 2>&1; st "rbcheck rc=$?: $(tail -1 /tmp/city/rbcheck2.log)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/rb_mingw2.log 2>&1; st "rb MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/rb_mingw2.log) warnings $(grep -c 'warning' /tmp/city/rb_mingw2.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tools/worldc.cpp > /tmp/city/rb_mingw2_worldc.log 2>&1; st "rb MinGW worldc rc=$? errors $(grep -c 'error' /tmp/city/rb_mingw2_worldc.log) warnings $(grep -c 'warning' /tmp/city/rb_mingw2_worldc.log)"
st "cq66 done"
touch /tmp/city/cq66_done.marker
