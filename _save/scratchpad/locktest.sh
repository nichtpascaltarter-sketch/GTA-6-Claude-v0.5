#!/bin/sh
# mimic run.sh's launch lock with a 3 s hold
exec 8>>/tmp/locktest_slot.$1
flock 8
exec 7>>/tmp/locktest_launch.lock; flock 7
echo "$1 passed gate at $(date +%s.%N | cut -c1-14)"
sleep 3 </dev/null >/dev/null 2>&1 8>&- & exec 7>&-
exec sh -c "sleep 1; echo $1 game done at \$(date +%s.%N | cut -c1-14)"
