#!/bin/bash
# Step 4.1 ($S/s41 = bcb8a71 + 4.1): the CI's world steps with its flags (world_format_test, worldv_test, worldv on the
# world), ntwfmt --check on the samples and on two relative-floor stairs, worldv on those as files.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s41.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq62 start"
cd $S/s41
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined tests/world/world_format_test.cpp -o /tmp/city/s41_wft > /tmp/city/s41_wft_build.log 2>&1; st "wft build rc=$? ($(tail -1 /tmp/city/s41_wft_build.log))"
/tmp/city/s41_wft > /tmp/city/s41_wft_run.log 2>&1; st "wft run rc=$?: $(tail -1 /tmp/city/s41_wft_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined tests/world/worldv_test.cpp -o /tmp/city/s41_wvt > /tmp/city/s41_wvt_build.log 2>&1; st "worldv_test build rc=$? ($(tail -1 /tmp/city/s41_wvt_build.log))"
/tmp/city/s41_wvt > /tmp/city/s41_wvt_run.log 2>&1; st "worldv_test run rc=$?: $(tail -1 /tmp/city/s41_wvt_run.log)"
wmem 2000
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/city/s41_worldv > /tmp/city/s41_worldv_build.log 2>&1; st "worldv build rc=$?"
/tmp/city/s41_worldv --world=world --no-cache > /tmp/city/s41_worldv_world.log 2>&1; st "worldv --world=world rc=$?: $(tail -1 /tmp/city/s41_worldv_world.log)"
/tmp/city/s41_worldv --no-cache $S/s41test/rel/core_rel.ntw $S/s41test/rel/core_rel_down.ntw > /tmp/city/s41_worldv_rel.log 2>&1; st "worldv on the relative-floor stairs rc=$?: $(tail -1 /tmp/city/s41_worldv_rel.log)"
wmem 1500
nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/ntwfmt.cpp -o /tmp/city/s41_ntwfmt > /tmp/city/s41_ntwfmt_build.log 2>&1; st "ntwfmt build rc=$?"
/tmp/city/s41_ntwfmt --check tests/world/fixtures/samples > /tmp/city/s41_ntwfmt_samples.log 2>&1; st "ntwfmt --check samples rc=$?: $(tail -1 /tmp/city/s41_ntwfmt_samples.log)"
/tmp/city/s41_ntwfmt --check $S/s41test/rel > /tmp/city/s41_ntwfmt_rel.log 2>&1; st "ntwfmt --check relative-floor stairs rc=$?: $(tail -2 /tmp/city/s41_ntwfmt_rel.log | tr '\n' ' ')"
st "cq62 done"
touch /tmp/city/cq62.done
