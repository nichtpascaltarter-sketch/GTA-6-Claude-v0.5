#!/bin/sh
# bld41.sh: the four test trees with locoplayer block v3c (site search by real collider footprints, out to 80 m, roads
# allowed with the population off; the spiral convoy): st20, st21, st19, st19a, one at a time, gated (memfree >= 2500, no
# fresh lead flag). <tree>.building marks a tree being built (orch41 does not start its exe meanwhile); a failed build
# puts the tree's previous exe back (bin/loco_<t>_pre41.exe).
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
for t in st20 st21 st19 st19a; do
  if grep -q "within 80 m" $T/$t/bin/loco_$t.exe 2>/dev/null; then echo "have $t"; continue; fi
  gate
  # (not while a game of this tree runs)
  while ps -eo cmd | grep -v grep | grep -q "bin/loco_$t.exe"; do sleep 30; done
  touch $T/$t.building
  echo "build $t $(date -u +%T)"
  [ -f $T/$t/bin/loco_$t.exe ] && mv $T/$t/bin/loco_$t.exe $T/$t/bin/loco_${t}_pre41.exe
  sh $T/build.sh $t
  if [ ! -f $T/$t/bin/loco_$t.exe ]; then echo "$t FAILED"; [ -f $T/$t/bin/loco_${t}_pre41.exe ] && cp $T/$t/bin/loco_${t}_pre41.exe $T/$t/bin/loco_$t.exe; fi
  rm -f $T/$t.building
done
echo "bld41 done $(date -u +%T)"
