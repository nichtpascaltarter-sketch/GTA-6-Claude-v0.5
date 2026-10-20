#!/bin/sh
# sequential Wine test runs (one at a time, each behind the memory gate)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
cp bin/nt_int.exe $S/nt_int_run.exe
run() {
  name=$1; shift
  $S/memgate.sh 2600
  echo "start $name $(date +%H:%M:%S)" >> $S/tests.log
  TIMEOUT=3000 WINEPREFIX=/tmp/wine_int EXE=$S/nt_int_run.exe nice -n 10 tools/run.sh --width 960 --height 540 --quality 1 --renderevery 20 --play --autoplay interior --autoevery 1000 --shotdir 'Z:\tmp\int\' "$@" > $S/$name.log 2>&1
  echo "done $name $? $(date +%H:%M:%S)" >> $S/tests.log
}
: > $S/tests.log
rm -f /tmp/int/holdup_* /tmp/int/elev_* /tmp/int/tide_*
run hold4 --holduptest "TideStop Mart" --autoduration 45
run elev1 --elevatortest --autoduration 40
run tide4 --tidetest "Tide Customs Calle Luna" --autoduration 60
echo "all done" >> $S/tests.log
