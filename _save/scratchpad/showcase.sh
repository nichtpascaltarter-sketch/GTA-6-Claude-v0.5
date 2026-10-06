#!/bin/sh
# Showcase screenshots for the user at each check-in: 5 free-camera views (no player, no HUD) at 1920x1080 on the ultra
# preset, taken with the latest build-verified exe (bin/nt_snap.exe, copied first so a snapshot finishing meanwhile
# cannot swap it). The views are the city agent's district framings (aerial and street level, some moved to sunset or
# night), in 5 sets that rotate (showcase_sets.txt). Output: PNGs in
# /tmp/showcase/<stamp>/. It waits behind the lead's snapshot checks and for room under the memory cap, and takes the
# launch lock like tools/run.sh, without using a test slot. usage: showcase.sh [set 0-4]
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
N=${1:-$(cat $SP/showcase_next.txt 2>/dev/null || echo 0)}
case $N in 1|2|3|4) ;; *) N=0;; esac
SHOTS=$(awk -v n=$N '$1 == n {$1 = ""; print}' $SP/showcase_sets.txt)
echo $(( (N + 1) % 5 )) > $SP/showcase_next.txt
STAMP=$(date -u +%m%d_%H%M)
OUT=/tmp/showcase/$STAMP
mkdir -p $OUT
cp $REPO/bin/nt_snap.exe $SP/showcase.exe || { echo "SHOWCASE FAILED: no nt_snap.exe"; exit 1; }
echo "build $(git -C $REPO rev-parse --short HEAD), set $N" > $OUT/info.txt
# the lead's snapshot checks go first, then wait for room (a plain wait: memgate raises no flag for this script)
while [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; do sleep 30; done
sh $SP/memgate.sh 3800
export WINEDEBUG=-all WINEPREFIX=/tmp/wine_tour WINEDLLOVERRIDES="d3dcompiler_47=n"
L=$WINEPREFIX/drive_c/users/root/AppData/Local/NeonTide/log.txt
rm -f $L
exec 7>>/tmp/neontide_launch.lock
flock 7
n=0; while [ "$(sh $REPO/tools/memfree.sh)" -lt 4000 ] && [ $n -lt 80 ]; do sleep 15; n=$((n + 1)); done
sleep 30 </dev/null >/dev/null 2>&1 &
exec 7>&-
echo 500 > /proc/self/oom_score_adj 2>/dev/null
cd $REPO
timeout 5400 xvfb-run -a -s "-screen 0 1920x1080x24" /usr/lib/wine/wine64 $SP/showcase.exe --autotest --width 1920 --height 1080 \
  --quality ${Q:-3} --settle 16 $SHOTS --shotdir "Z:\\tmp\\showcase\\$STAMP\\" > $OUT/run.txt 2>&1
rc=$?
cp $L $OUT/log.txt 2>/dev/null
for f in $OUT/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
grep -E "[Ss]hot|Unhandled|FATAL" $OUT/log.txt 2>/dev/null | cut -c1-120
echo "SHOWCASE DONE rc=$rc $OUT $(ls $OUT/*.png 2>/dev/null | wc -l) png"
