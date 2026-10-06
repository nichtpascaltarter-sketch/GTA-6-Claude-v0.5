#!/bin/sh
# build with retries when the compiler is OOM-killed; waits for 3.5 GB of available memory first
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
LOG=$S/buildr.log
cd /home/user/GTA-6-Claude-v0.5
: > $LOG
for i in 1 2 3 4 5 6; do
  n=0
  while [ "$(free -m | awk '/Mem:/{print $7}')" -lt 3500 ] && [ $n -lt 90 ]; do sleep 10; n=$((n+1)); done
  echo "attempt $i avail $(free -m | awk '/Mem:/{print $7}')" >> $LOG
  OUT=bin/nt_int.exe nice -n 10 ./build.sh > $S/buildr_last.log 2>&1
  rc=$?
  if [ $rc -eq 0 ]; then echo "exit=0" >> $LOG; exit 0; fi
  if ! grep -q "Killed signal" $S/buildr_last.log; then grep -n "error" $S/buildr_last.log | head -20 >> $LOG; echo "exit=$rc" >> $LOG; exit $rc; fi
  echo "killed, retry" >> $LOG
  sleep 20
done
echo "exit=99" >> $LOG
