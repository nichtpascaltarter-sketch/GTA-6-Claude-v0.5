#!/bin/sh
# bld42.sh: the four test trees with locoplayer block v3d (v3c's site search + the walkers' goals 0.8 m ahead:
# BRAIN_GOTO does not move for a goal under 0.6 m) - st19, st19a, st20, st21, one at a time, gated (memfree >= 2500, no
# fresh lead flag), never while a game of that tree runs. <tree>.building marks a tree being built (orch42 does not
# start its exe meanwhile); a failed build puts the tree's previous exe back (bin/loco_<t>_pre42.exe).
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
for t in st19 st19a st20 st21; do
  if grep -q "locoplayer spiral t%" "$T/$t/bin/loco_$t.exe" 2>/dev/null; then echo "have $t"; continue; fi
  gate
  while ps -eo cmd | grep -v grep | grep -q "bin/loco_$t.exe"; do sleep 30; done
  touch "$T/$t.building"
  echo "build $t $(date -u +%T)"
  [ -f "$T/$t/bin/loco_$t.exe" ] && mv "$T/$t/bin/loco_$t.exe" "$T/$t/bin/loco_${t}_pre42.exe"
  sh "$T/build.sh" "$t"
  if [ ! -f "$T/$t/bin/loco_$t.exe" ]; then echo "$t FAILED"; [ -f "$T/$t/bin/loco_${t}_pre42.exe" ] && cp "$T/$t/bin/loco_${t}_pre42.exe" "$T/$t/bin/loco_$t.exe"; fi
  mv "$T/$t.building" "$T/$t.built"
done
echo "bld42 done $(date -u +%T)"
