#!/bin/sh
# usage: tour.sh EXE OUTDIR START COUNT [extra args...]   (one game at a time, slot 4)
EXE=$1; OUTD=$2; START=$3; COUNT=$4; shift 4
mkdir -p "$OUTD"
WIN=$(echo "$OUTD" | sed 's#/#\\#g')
cd /home/user/GTA-6-Claude-v0.5
rm -f /tmp/wine_wl/drive_c/users/root/AppData/Local/NeonTide/log.txt
EXE=$EXE WINEPREFIX=/tmp/wine_wl NT_D3D12_SLOT=1 TIMEOUT=${TIMEOUT:-3000} nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart $START --tourcount $COUNT --quality 1 --renderevery 6 --popdebug --shotdir "Z:${WIN}\\" "$@" > "$OUTD/run.out" 2>&1
echo "exit $?" >> "$OUTD/run.out"
cp /tmp/wine_wl/drive_c/users/root/AppData/Local/NeonTide/log.txt "$OUTD/log.txt" 2>/dev/null
for f in "$OUTD"/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
tail -1 "$OUTD/run.out"
grep -E "autoplay tour shot|autoplay tour done" "$OUTD/log.txt"
