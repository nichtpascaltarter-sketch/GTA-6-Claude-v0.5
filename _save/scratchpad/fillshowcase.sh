#!/bin/sh
# Render the shots a showcase run lost (e.g. OOM-killed) into its existing folder, with the same exe ($SP/showcase.exe).
# Waits for room like showcase.sh (memgate raises the lead flag: this name ends in showcase.sh), and keeps the flag fresh
# while the game runs (hold_for_showcase.sh). usage: fillshowcase.sh OUTDIR "--shot ... [--shot ...]"
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
OUT=${1:?}; SHOTS=${2:?}; STAMP=$(basename "$OUT")
sh $SP/memgate.sh 3800
export WINEDEBUG=-all WINEPREFIX=/tmp/wine_tour WINEDLLOVERRIDES="d3dcompiler_47=n"
L=/tmp/wine_tour/drive_c/users/root/AppData/Local/NeonTide/log.txt
rm -f /tmp/wine_tour/drive_c/users/root/AppData/Local/NeonTide/log.txt
exec 7>>/tmp/neontide_launch.lock
flock 7
sleep 30 </dev/null >/dev/null 2>&1 &
exec 7>&-
echo 0 > /proc/self/oom_score_adj 2>/dev/null   # (a lead run: at the cap the kernel takes agent games, at 500, first)
(sleep 20; sh $SP/hold_for_showcase.sh) </dev/null >/dev/null 2>&1 &
cd $REPO
timeout 5400 xvfb-run -a -s "-screen 0 1920x1080x24" /usr/lib/wine/wine64 $SP/showcase.exe --autotest --width 1920 --height 1080 \
  --quality ${Q:-3} --settle 16 $SHOTS --shotdir "Z:\\tmp\\showcase\\$STAMP\\" > "$OUT/run_fill.txt" 2>&1
rc=$?
cp $L "$OUT/log_fill.txt" 2>/dev/null
for f in "$OUT"/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "${f:?}"; done
echo "SHOWCASE DONE rc=$rc $OUT $(ls "$OUT"/*.png 2>/dev/null | wc -l) png"
