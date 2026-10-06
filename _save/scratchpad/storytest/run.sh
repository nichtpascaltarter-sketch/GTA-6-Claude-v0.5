#!/bin/sh
# Runs the fast mission-test runner: run.sh <missiontest ids> [extra game args]
ST=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/storytest
LOGF=/tmp/wine_story/drive_c/users/root/AppData/Local/NeonTide/log.txt
IDS=$1; shift
mkdir -p /tmp/story
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_story EXE=$ST/nt_storytest.exe TIMEOUT=${TIMEOUT:-2400} nice -n 10 tools/run.sh --width ${W:-640} --height ${H:-360} --play --missiontest $IDS --renderevery ${RE:-400} --shotdir 'Z:\tmp\story\' $EXTRA "$@" > /tmp/story/run.txt 2>&1
cp $LOGF /tmp/story/log_$(echo $IDS | tr ',' '_').txt
grep -a "missiontest\|storytest\|FATAL\|rror" $LOGF | grep -v "Compiled" | tail -${TAILN:-80}
