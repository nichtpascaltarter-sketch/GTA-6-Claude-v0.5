#!/bin/sh
# wait for the after2 exe, then its locotour run (after the instrumented run is done), then a summary
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
until [ -f $T/after2/bin/loco_after2.exe ] || grep -q "build after2 rc [1-9]" /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/b69s1jj2j.output 2>/dev/null; do sleep 20; done
[ -f $T/after2/bin/loco_after2.exe ] || { echo "no after2 exe"; exit 1; }
sleep 30
sh $T/run.sh after2
rm -f $T/shots_after2/*.bmp
grep "locotour steps\|locotour feet\|close passes\|body language\|slides over\|peds cpu" $T/log_after2.txt
echo "after2 run done"
