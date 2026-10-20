#!/bin/sh
# bld2021.sh: st20 (c14d) again with locoplayer block v3 (the test spiral), then st21 (= st20 + c15f), each gated
# (memfree >= 2500, no fresh lead flag). If st20's v3 build fails, its v2 exe comes back (bin/loco_st20_v2.exe).
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
gate
echo "build st20 (c14d, v3) $(date -u +%T)"
rm -f $T/st20/bin/loco_st20.exe
sh $T/build.sh st20
if [ ! -f $T/st20/bin/loco_st20.exe ]; then cp $T/st20/bin/loco_st20_v2.exe $T/st20/bin/loco_st20.exe; echo "st20 v3 FAILED: v2 exe back"; exit 1; fi
gate
echo "build st21 (c15f, v3) $(date -u +%T)"
sh $T/build.sh st21
echo "bld2021 done $(date -u +%T)"
