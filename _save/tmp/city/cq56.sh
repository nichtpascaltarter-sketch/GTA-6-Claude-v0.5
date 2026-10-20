#!/bin/bash
# Road-overlap fix, round 5b: two more districts with changed spots - Downtown/Financial (11th Avenue, Bougainvillea
# Boulevard, S 2nd Street along Bayshore Boulevard) and Old Cutler Road (Caribe Drive, Bayshore Way) - seeds 1-3, base and fix5.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
while [ ! -f /tmp/city/cq55.done ]; do sleep 10; done
st "cq56 start"
cd $S/rf/t2
for SEED in 1 2 3; do
  for V in base fix5; do
    for D in "down 3650 -900" "cutler 2900 -2950"; do
      set -- $D
      O=/tmp/city/sim_$1_${V}_$SEED.txt
      if [ ! -s $O ] || ! grep -q 'deadlocks' $O; then
        wmem 2500
        nice -n 5 /tmp/city/ts_$V --sim $2 $3 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > $O 2>&1
      fi
      st "$1 $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O); by node: $(grep '^DEADLOCK' $O | grep -o 'gateNode [0-9]*' | sort | uniq -c | sort -rn | head -4 | awk '{print $3"x"$1}' | tr '\n' ' ')"
    done
  done
done
st "cq56 done"
touch /tmp/city/cq56.done
