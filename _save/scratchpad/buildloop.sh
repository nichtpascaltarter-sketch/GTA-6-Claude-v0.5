#!/bin/sh
# retry the build until it succeeds (OOM kills and other agents' in-progress breakages); stops on errors in my files
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
LOG=$S/buildl.log
cd /home/user/GTA-6-Claude-v0.5
: > $LOG
for i in $(seq 1 15); do
  /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 3200
  echo "attempt $i $(date +%H:%M:%S)" >> $LOG
  OUT=bin/nt_int.exe nice -n 10 ./build.sh > $S/buildl_last.log 2>&1
  if [ $? -eq 0 ]; then echo "exit=0" >> $LOG; exit 0; fi
  if grep -q "error" $S/buildl_last.log; then
    grep -n "error" $S/buildl_last.log | head -3 >> $LOG
    if grep "error" $S/buildl_last.log | grep -q "interior\|holdup\|landmarks"; then echo "exit=2 (mine)" >> $LOG; exit 2; fi
  else
    echo "killed" >> $LOG
  fi
  sleep 150
done
echo "exit=99" >> $LOG
