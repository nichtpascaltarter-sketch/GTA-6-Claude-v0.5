#!/bin/sh
# Lead check of the live cockpit on the current working tree: driver's hands on the vehicle's own rim, rim turning with
# them, gauge needles. First-person drive by day (also shows whether --firstperson now holds in vehicles).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
cd $REPO || exit 1
rm -f $SP/lead_index; cp .git/index $SP/lead_index
GIT_INDEX_FILE=$SP/lead_index git add -A src tools build.sh >/dev/null 2>&1
T=$(GIT_INDEX_FILE=$SP/lead_index git write-tree); echo "tree $T"
D=/tmp/leadtree7; rm -rf $D; mkdir -p $D; git archive $T | tar -x -C $D
cd $D && mkdir -p build/gen bin
sh $SP/memgate.sh 3000 >/dev/null 2>&1
QUICK=1 OUT=bin/lead_v7.exe ./build.sh > $SP/lead_v7_build.log 2>&1 || { echo "build failed"; grep -E "error|undefined" $SP/lead_v7_build.log | head; exit 1; }
cp bin/lead_v7.exe $SP/lead_v7_run.exe
cd $REPO
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
O=/tmp/v7_fpdrive; rm -rf $O; mkdir -p $O
sh $SP/memgate.sh 2400 >/dev/null 2>&1
EXE=$SP/lead_v7_run.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4000 nice -n 6 tools/run.sh --width 960 --height 540 --play --firstperson --autoplay drive --autoduration 32 --autoevery 8 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\v7_fpdrive\\" > $O/run.txt 2>&1
echo "fpdrive exit $?"
cp $LOGF $O/log.txt 2>/dev/null
grep -E "autoplay t=|Unhandled" $O/log.txt | tail -4 | cut -c1-200
