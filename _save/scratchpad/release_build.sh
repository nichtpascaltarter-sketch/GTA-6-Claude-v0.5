#!/bin/sh
# Release build of main (af8a066) as NeonTide-0.02.6.exe: build.sh's static -O2 build, then the snapshot drive smoke on it.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
D=/tmp/rel026
rm -rf "${D:?}" && mkdir -p $D
cd $REPO && git archive af8a06651dcc21a17694f10e98b1c782ea563786 | tar -x -C $D
cd $D && mkdir -p build/gen bin
if ! OUT=bin/NeonTide-0.02.6.exe ./build.sh > $SP/rel_build.log 2>&1; then echo "RELEASE BUILD FAILED"; tail -5 $SP/rel_build.log; exit 1; fi
ls -la bin/NeonTide-0.02.6.exe
x86_64-w64-mingw32-objdump -p bin/NeonTide-0.02.6.exe | grep "DLL Name" | sort -u
mkdir -p /tmp/rel026_smoke
EXE=$D/bin/NeonTide-0.02.6.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=3000 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay drive --autoduration 24 --autoevery 8 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\rel026_smoke\\" > $SP/rel_smoke.txt 2>&1
rc=$?
L=/tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt
grep -E "autoplay t=" $L | tail -2 | cut -c1-140
if [ $rc -ne 0 ] || grep -q "Unhandled" $L; then echo "RELEASE SMOKE FAILED rc=$rc"; exit 1; fi
sha256sum bin/NeonTide-0.02.6.exe
echo "RELEASE READY"
