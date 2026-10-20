#!/bin/bash
# AI agent: one native traffic harness run behind the run memory gate (3000). Usage: ts_run.sh OUTFILE ARGS...
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
OUT=$1; shift
$SP/aiwork/ai_memgate.sh 3000
cd $SP/ts/${TS_TREE:-tree}   # (TS_TREE=tree61: HEAD 7b9146d's tree, snapshot 61)
echo "$(date +%T) start: $*" > $OUT
BIN=${TS_BIN:-$SP/ts/traffic_sim}   # (TS_BIN: another build of the harness, e.g. traffic_sim19)
echo "binary $(basename $BIN)" >> $OUT
nice -n 5 timeout 3600 $BIN "$@" >> $OUT 2>&1
echo "$(date +%T) rc=$?" >> $OUT
