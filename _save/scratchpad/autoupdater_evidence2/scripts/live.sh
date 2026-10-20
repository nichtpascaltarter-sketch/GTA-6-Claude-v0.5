#!/bin/sh
# Live check against GitHub itself (no --update-url): a harness built as commit 837b39c (#52's merge, 2 behind main
# when written), stand-in tools and tar.exe in the private prefix. Expected: the comparison and the commit list from
# api.github.com, #53 listed, the archive downloaded from github.com through codeload.github.com, and the stand-in
# tar.exe refusing gzip (the real one unpacks it). Rate limits (60/h per address) may stop it earlier.
T=/tmp/upd2; P=$T/wine
LA=$P/drive_c/users/root/AppData/Local/NeonTide; L=$LA/log.txt
OUT=$T/results/live.txt
sh /tmp/tools/gate.sh 3300 >/dev/null
rm -rf $T/glive $LA/updates; mkdir -p $T/glive $T/results; cp $T/chk/harness_live.exe $T/glive/NeonTide.exe
mkdir -p $P/drive_c/w64devkit/bin; cp $T/stubs/stub_tool.exe $P/drive_c/w64devkit/bin/g++.exe; cp $T/stubs/stub_tool.exe $P/drive_c/w64devkit/bin/windres.exe
cp $T/stubs/stub_tar.exe $P/drive_c/windows/system32/tar.exe
rm -f $L
(cd $T/glive && WINEDEBUG=-all WINEPREFIX=$P timeout 600 xvfb-run -a -s "-screen 0 640x480x24" /usr/lib/wine/wine64 NeonTide.exe --update-test > $T/live_stdout.txt 2>&1)
echo "exit=$?" > $OUT
WINEPREFIX=$P timeout 120 /usr/lib/wine/wineserver64 -w
cat $L >> $OUT
echo "--- update folder:" >> $OUT
ls -la $LA/updates/* >> $OUT 2>&1
tr -d '\r' < $LA/updates/*/build.log >> $OUT 2>/dev/null
cat $OUT
