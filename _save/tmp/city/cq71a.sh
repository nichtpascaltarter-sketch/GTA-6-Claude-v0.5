#!/bin/bash
# After cq70: part 2a's first compile ($S/p2 = p62f + 2a), syntax only (CI's flags), then p62f's fixture checks
# (CI's world_format_test, worldv_test, worldmodel_test; worldv on world/). One build at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/p2.status; }
sf() { echo "$1 $(date)" >> /tmp/city/p62f.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
CI="-std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined"
until [ -f /tmp/city/cq70_done.marker ]; do sleep 20; done
st "cq71a start (syntax only)"
cd $S/p2
wmem 2000
nice -n 5 g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only tests/world/worldmodel_test.cpp > /tmp/city/p2s_wmt.log 2>&1; st "syntax worldmodel_test rc=$? ($(grep -c 'error' /tmp/city/p2s_wmt.log) error lines)"
wmem 2000
nice -n 5 g++ -std=c++17 -Wall -Wextra -fsyntax-only -I. tools/worldc.cpp > /tmp/city/p2s_worldc.log 2>&1; st "syntax worldc rc=$? ($(grep -c 'error' /tmp/city/p2s_worldc.log) error lines, $(grep -c 'warning' /tmp/city/p2s_worldc.log) warning lines)"
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/p2s_mingw.log 2>&1; st "syntax MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/p2s_mingw.log) warnings $(grep -c 'warning' /tmp/city/p2s_mingw.log)"
touch /tmp/city/cq71a_syntax.marker
sf "cq71a p62f start"
cd $S/p62f
for T in world/world_format_test world/worldv_test world/worldmodel_test; do
  n=$(basename $T)
  wmem 2000
  nice -n 5 g++ $CI tests/$T.cpp -o /tmp/city/p62f_$n > /tmp/city/p62f_${n}_build.log 2>&1; b=$?
  /tmp/city/p62f_$n > /tmp/city/p62f_${n}_run.log 2>&1; sf "CI $n build rc=$b ($(grep -c 'error\|warning' /tmp/city/p62f_${n}_build.log) error/warning lines) run rc=$?: $(tail -1 /tmp/city/p62f_${n}_run.log | cut -c1-100)"
done
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/city/p62f_worldv > /tmp/city/p62f_worldv_build.log 2>&1; b=$?
/tmp/city/p62f_worldv --world=world --no-cache > /tmp/city/p62f_worldv_run.log 2>&1; sf "CI worldv on world/ build rc=$b run rc=$?: $(tail -1 /tmp/city/p62f_worldv_run.log)"
/tmp/city/p62f_worldv --world=tests/world/fixtures/model --no-cache > /tmp/city/p62f_worldv_model.log 2>&1; sf "worldv on the model fixture rc=$?: $(tail -1 /tmp/city/p62f_worldv_model.log)"
sf "cq71a done"
touch /tmp/city/cq71a_done.marker
