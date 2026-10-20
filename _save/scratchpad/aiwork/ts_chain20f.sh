#!/bin/bash
# AI agent (set 20): the sign-off comparison on HEAD 7b9146d's world (tree61): base (traffic_sim2061b = HEAD's AI) against
# set 20 (traffic_sim20$F): the 15 runs of set 19's table, the meeting test on edge 2907, the dead-end circles (node 4870
# and the city's 177), the stair probe; first the overlap listing of this world. Restart-safe (finished runs are
# skipped); relaunch: AW/ai_detach.sh AW/ts_chain20f.out bash AW/ts_chain20f.sh 61f
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
F=${1:-61f}
export TS_TREE=tree61
rm -f $T/chain20f.done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
once $T/overlaps61.txt env TS_BIN=$T/traffic_sim2061b $R $T/overlaps61.txt --overlaps
M="Kiko,Falke 7,Sawgrass,Tekko,Palomino,Parcel P70,Lifeline,Packhorse 26,Compactor,Boulevard,Longbow 9,Guardian Pumper,Raijin 1000"
for v in $F 61b; do
  B=$T/traffic_sim20$v
  [ -x $B ] || { echo "no $B"; continue; }
  for s in 1 2 3; do
    once $T/q_ramp_${v}_$s.txt env TS_BIN=$B $R $T/q_ramp_${v}_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
    once $T/q_mid_${v}_$s.txt env TS_BIN=$B $R $T/q_mid_${v}_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  done
  for s in 1 2; do
    once $T/q_beach_${v}_$s.txt env TS_BIN=$B $R $T/q_beach_${v}_$s.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  done
  once $T/r_dt1_${v}.txt env TS_BIN=$B $R $T/r_dt1_${v}.txt --sim 2700 760 450 --cars 140 --peds 120 --minutes 8 -v
  once $T/r_luna_${v}.txt env TS_BIN=$B $R $T/r_luna_${v}.txt --sim 2250 100 450 --cars 140 --peds 120 --minutes 6 -v
  once $T/r_hwy_${v}.txt env TS_BIN=$B $R $T/r_hwy_${v}.txt --sim 2342 1143 600 --cars 160 --peds 40 --minutes 6 -v
  once $T/p_mid_${v}.txt env TS_BIN=$B $R $T/p_mid_${v}.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
  once $T/p_mid_${v}_2.txt env TS_BIN=$B $R $T/p_mid_${v}_2.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 --seed 2 -v
  once $T/p_ramp_${v}.txt env TS_BIN=$B $R $T/p_ramp_${v}.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
  once $T/p_dt1_${v}.txt env TS_BIN=$B $R $T/p_dt1_${v}.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
  once $T/mt_2907_${v}_Guar.txt env TS_BIN=$B $R $T/mt_2907_${v}_Guar.txt --meettest 2907 "Guardian Pumper" "Packhorse 26"
  once $T/mt_2907_${v}_Boul.txt env TS_BIN=$B $R $T/mt_2907_${v}_Boul.txt --meettest 2907 Boulevard Boulevard
  once $T/ut_4870_${v}.txt env TS_BIN=$B $R $T/ut_4870_${v}.txt --uturntest 2000 175 10
  once $T/ut_city_${v}.txt env TS_BIN=$B $R $T/ut_city_${v}.txt --uturntest 3000 1000 2500 "$M"
done
# three more seeds of the I-38 ramps (node 5038's signal: the junction set 20's first steering build locked up at)
for v in $F 61b; do
  for s in 4 5 6; do
    once $T/q_ramp_${v}_$s.txt env TS_BIN=$T/traffic_sim20$v $R $T/q_ramp_${v}_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  done
done
# the stair rule where the life run's walkers were held on nt_s20t (old rule) and down the Calle Luna station stair
CORNERS=$(grep "stairpace" $SP/log_life_nt_s20t.txt | grep -o "[0-9]*@[0-9]*,[0-9-]*,[0-9.]*" | sed 's/^[0-9]*@//' | awk -F, '{print $1","$2}' | sort -u | tr '\n' ' ')
STAIR=""; for x in 1818 1822 1826 1830 1834 1838 1842 1846; do STAIR="$STAIR $x,-638"; done
once $T/sp_corners61.txt env TS_BIN=$T/traffic_sim20$F $R $T/sp_corners61.txt --stairprobe 2.4 $CORNERS
once $T/sp_stair61.txt env TS_BIN=$T/traffic_sim20$F $R $T/sp_stair61.txt --stairprobe 17.6 $STAIR
echo done > $T/chain20f.done
