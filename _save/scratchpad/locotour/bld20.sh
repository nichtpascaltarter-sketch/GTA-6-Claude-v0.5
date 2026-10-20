#!/bin/sh
# bld20.sh: rebuild st20 (now c14d) gated (memfree >= 2500, no fresh lead flag); orch36 waits on orch35b, so it starts
# its runs with whichever st20 exe is there then - this has to finish first (it will: orch35b has hours of runs)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
while :; do
  if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi
  sleep 30
done
echo "build st20 (c14d) $(date -u +%T)"
sh $T/build.sh st20
echo "bld20 done $(date -u +%T)"
