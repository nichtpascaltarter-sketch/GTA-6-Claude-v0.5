#!/bin/bash
# World model v0 ($S/wm = bcb8a71 + 4.1 + road fix c5bc0aab + v0): the CI's world tests with their flags (world_format_test,
# worldmodel_test, worldv_test), worldv on the world and on the model's test world, ntwfmt --check, worldc (world.bin
# against 4.1's), the MinGW syntax checks (main.cpp, worldmodel_test.cpp), and the census of the procedural world (rfcheck)
# against the road fix's (census_fix7r.txt): roads.cpp's core moved into roadnet.cpp must change nothing.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wm.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
st "cq63 start"
cd $S/wm
for T in world_format_test worldmodel_test worldv_test; do
  wmem 2000
  nice -n 5 g++ $CI tests/world/$T.cpp -o /tmp/city/wm_$T > /tmp/city/wm_${T}_build.log 2>&1; st "$T build rc=$? ($(grep -c 'error\|warning' /tmp/city/wm_${T}_build.log) error/warning lines)"
  /tmp/city/wm_$T > /tmp/city/wm_${T}_run.log 2>&1; st "$T run rc=$?: $(tail -1 /tmp/city/wm_${T}_run.log)"
done
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/city/wm_worldv > /tmp/city/wm_worldv_build.log 2>&1; st "worldv build rc=$?"
/tmp/city/wm_worldv --world=world --no-cache > /tmp/city/wm_worldv_world.log 2>&1; st "worldv --world=world rc=$?: $(tail -1 /tmp/city/wm_worldv_world.log)"
/tmp/city/wm_worldv --world=tests/world/fixtures/model --no-cache > /tmp/city/wm_worldv_model.log 2>&1; st "worldv --world=tests/world/fixtures/model rc=$?: $(tail -1 /tmp/city/wm_worldv_model.log)"
wmem 1500
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/ntwfmt.cpp -o /tmp/city/wm_ntwfmt > /tmp/city/wm_ntwfmt_build.log 2>&1; st "ntwfmt build rc=$?"
/tmp/city/wm_ntwfmt --check tests/world/fixtures/samples tests/world/fixtures/model > /tmp/city/wm_ntwfmt.log 2>&1; st "ntwfmt --check samples + model rc=$?: $(tail -1 /tmp/city/wm_ntwfmt.log)"
wmem 1500
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o /tmp/city/wm_worldc > /tmp/city/wm_worldc_build.log 2>&1; st "worldc build rc=$? ($(grep -c warning /tmp/city/wm_worldc_build.log) warnings)"
/tmp/city/wm_worldc --world=world --out=/tmp/city/wm_world.bin > /tmp/city/wm_worldc_run.log 2>&1; st "worldc rc=$?: $(tail -1 /tmp/city/wm_worldc_run.log); world.bin vs 4.1's: $(cmp /tmp/city/wm_world.bin $S/s41/build/gen/world.bin && echo identical)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/wm_mingw_main.log 2>&1; st "MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/wm_mingw_main.log) warnings $(grep -c 'warning' /tmp/city/wm_mingw_main.log)"
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -I. tests/world/worldmodel_test.cpp > /tmp/city/wm_mingw_test.log 2>&1; st "MinGW worldmodel_test.cpp rc=$? errors $(grep -c 'error' /tmp/city/wm_mingw_test.log) warnings $(grep -c 'warning' /tmp/city/wm_mingw_test.log)"
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_wm -lpthread > /tmp/city/rfcheck_wm_build.log 2>&1; st "rfcheck_wm build rc=$?"
wmem 2000
nice -n 5 /tmp/city/rfcheck_wm > /tmp/city/census_wm.txt 2> /tmp/city/census_wm_err.txt; st "rfcheck_wm run rc=$?: census vs the road fix's: $(cmp /tmp/city/census_wm.txt /tmp/city/census_fix7r.txt && echo identical || echo "$(diff /tmp/city/census_wm.txt /tmp/city/census_fix7r.txt | grep -c '^[<>]') lines differ")"
st "cq63 done"
touch /tmp/city/cq63.done
