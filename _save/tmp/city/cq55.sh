#!/bin/bash
# Road-overlap fix, round 5b simulations: the AI agent's Midtown and Sol Beach runs, seeds 1-3, base (when missing) and fix5.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
while [ ! -f /tmp/city/cq54.done ]; do sleep 10; done
while kill -0 19709 2>/dev/null; do sleep 5; done
st "cq55 start"
cd $S/rf/t2
for SEED in 1 2 3; do
  for V in base fix5; do
    O=/tmp/city/sim_mid_${V}_$SEED.txt
    if [ ! -s $O ] || ! grep -q 'deadlocks' $O; then
      wmem 2500
      nice -n 5 /tmp/city/ts_$V --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > $O 2>&1
    fi
    st "mid $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O); by node: $(grep '^DEADLOCK' $O | grep -o 'gateNode [0-9]*' | sort | uniq -c | sort -rn | head -4 | awk '{print $3"x"$1}' | tr '\n' ' ')"
    O=/tmp/city/sim_beach_${V}_$SEED.txt
    if [ ! -s $O ] || ! grep -q 'deadlocks' $O; then
      wmem 2500
      nice -n 5 /tmp/city/ts_$V --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > $O 2>&1
    fi
    st "beach $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O); by node: $(grep '^DEADLOCK' $O | grep -o 'gateNode [0-9]*' | sort | uniq -c | sort -rn | head -4 | awk '{print $3"x"$1}' | tr '\n' ' ')"
  done
done
st "cq55 done"
touch /tmp/city/cq55.done
