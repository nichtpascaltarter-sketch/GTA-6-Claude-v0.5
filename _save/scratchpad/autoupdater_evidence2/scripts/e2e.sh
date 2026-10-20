#!/bin/sh
# End-to-end updater test under Wine: an exe built with commit A (the updater harness, or the game: e2e.sh EXE_A),
# a local server standing in for GitHub (server.py: GitHub's real compare answers, a commit list, test archives),
# Windows' tar.exe and the toolchains as stand-ins in a private prefix. Results: /tmp/upd2/results/e2e.txt
set -u
T=/tmp/upd2; P=$T/wine; S=$T/srv; PORT=8766; U=http://127.0.0.1:$PORT
A=a0c1e2f3a4b5c6d7e8f90123456789abcdef0a1b; B=b5ad2c3e0f1e2d3c4b5a69788796a5b4c3d2e1f0
LA=$P/drive_c/users/root/AppData/Local/NeonTide; L=$LA/log.txt; UPD=$LA/updates
G=$T/g; OUT=$T/results/e2e${E2E_TAG:-}.txt
EXE_A=${1:-$T/chk/harness_a.exe}; EXE_B=$T/chk/harness_b.exe; EXE_U=$T/chk/harness_u.exe
mkdir -p $T/results $S
: > $OUT
pass=0; fail=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  ok   $1" >> $OUT; else fail=$((fail+1)); echo "  FAIL $1" >> $OUT; fi; }
run() {   # run ARGS...: one start of $G/NeonTide.exe (and of what it starts); RC is its exit code
  sh /tmp/tools/gate.sh ${GATE:-800} > /dev/null   # (the harness is small; the game exe gets GATE=3300)
  : > $S/requests.log
  rm -f $L
  T0=$(date +%s)
  (cd $G && WINEDEBUG=-all WINEPREFIX=$P VERSION=9.9.9 timeout 900 xvfb-run -a -s "-screen 0 640x480x24" /usr/lib/wine/wine64 NeonTide.exe "$@" > $T/run_stdout.txt 2>&1)
  RC=$?
  WINEPREFIX=$P timeout 300 /usr/lib/wine/wineserver64 -w
  SECS=$(( $(date +%s) - T0 ))
  echo "--- run $* -> exit $RC (${SECS} s)" >> $OUT
  sed 's/^/    log| /' $L >> $OUT 2>/dev/null
  sed 's/^/    srv| /' $S/requests.log >> $OUT
}
scenario() { printf '%s\n' "$1" > $S/scenario.json; }
fresh() { rm -rf $G $UPD; mkdir -p $G; cp "$1" $G/NeonTide.exe; }
tools_none() { rm -rf $P/drive_c/w64devkit "$P/drive_c/Program Files (x86)/Microsoft Visual Studio" $P/drive_c/FakeVS; }
tools_mingw() {
  tools_none; mkdir -p $P/drive_c/w64devkit/bin
  cp $T/stubs/stub_tool.exe $P/drive_c/w64devkit/bin/g++.exe; cp $T/stubs/stub_tool.exe $P/drive_c/w64devkit/bin/windres.exe
}
tools_msvc() {
  VS="$P/drive_c/Program Files (x86)/Microsoft Visual Studio/Installer"
  mkdir -p "$VS" $P/drive_c/FakeVS/BuildTools/VC/Auxiliary/Build
  cp $T/stubs/fake_vswhere.exe "$VS/vswhere.exe"; printf '@echo off\r\n' > $P/drive_c/FakeVS/BuildTools/VC/Auxiliary/Build/vcvars64.bat
}
tar_on() { cp $T/stubs/stub_tar.exe $P/drive_c/windows/system32/tar.exe; }
tar_off() { rm -f $P/drive_c/windows/system32/tar.exe; }
record() { cat $G/NeonTide.exe.pending.json 2>/dev/null; }
blog() { cat $UPD/$B/build.log 2>/dev/null | tr -d '\r'; }

python3 -I $T/scripts/mkarchives.py $S $A $B $EXE_B $EXE_A >> $OUT
python3 -I $T/scripts/server.py $S $PORT $T/tree/tests/updater/fixtures > $T/server.log 2>&1 &
SRVPID=$!
sleep 1
AHEAD='"'$A'": "ahead"'
IDENT='"'$B'": "identical"'

echo "=== 1. no compiler: the notice says what building needs, nothing is downloaded" >> $OUT
fresh $EXE_A; tools_none; tar_on; scenario '{"compare": {'"$AHEAD"'}, "archive": "ok.tar"}'
run --update-test --update-url $U
check "exit 2 (not built)" '[ $RC = 2 ]'
check "the notice names the compilers" 'grep -q "need a C++ compiler: Visual Studio Build Tools (free) or w64devkit" $L'
check "main is 2 ahead, #55 and #54 listed" 'grep -q "main has 2 new commits" $L && grep -q "2 new updates: #55 Night lighting realism, #54 Taxi stands at the airport" $L'
check "two requests: compare (page 100) and 3 commits" '[ "$(grep -c . $S/requests.log)" = 2 ] && grep -q "/compare/$A...main?per_page=100&page=100" $S/requests.log && grep -q "/commits?sha=main&per_page=3" $S/requests.log'

echo "=== 2. no tar.exe: the notice, nothing downloaded" >> $OUT
fresh $EXE_A; tools_mingw; tar_off
run --update-test --update-url $U
check "exit 2" '[ $RC = 2 ]'
check "the notice names tar.exe" 'grep -q "unpacked with tar.exe, which comes with Windows 10 version 1803" $L'
check "no archive request" '! grep -q "archive" $S/requests.log'
tar_on

echo "=== 3. builds with a MinGW-w64 folder (w64devkit)" >> $OUT
fresh $EXE_A; tools_mingw
run --update-test --update-url $U
check "exit 0 (built and verified)" '[ $RC = 0 ]'
check "the toolchain" 'grep -q "building with MinGW-w64 (C:\\\\w64devkit\\\\bin)" $L'
check "archive through the redirect" 'grep -q "GET /nichtpascaltarter-sketch/GTA-6-Claude-v0.5/archive/$B.tar.gz" $S/requests.log && grep -q "GET /codeload/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/tar.gz/$B" $S/requests.log'
check "pending exe is the build of B" '[ "$(sha256sum < $G/NeonTide.exe.pending)" = "$(sha256sum < $EXE_B)" ]'
check "record: commit B from A, #55 #54, toolchain" 'record | grep -q "\"commit\": \"$B\", \"from\": \"$A\"" && record | grep -q "\"number\": 55" && record | grep -q "\"number\": 54" && record | grep -q "w64devkit"'
check "build env: NT_COMMIT=B, VERSION removed, PATH from w64devkit" 'blog | grep -q "^NT_COMMIT=$B$" && blog | grep -q "^VERSION=\[\]$" && blog | grep -qi "^PATH=C:\\\\w64devkit\\\\bin;"'
check "the labels worked (CRLF)" 'blog | grep -q "^built bin\\\\NeonTide.exe$" && ! blog | grep -q "never here"'
check "only build.log stays" '[ "$(ls $UPD/$B)" = "build.log" ]'
check "log: built, installs at the next start" 'grep -q "built b5ad2c3 in .* it installs at the next start" $L'

echo "=== 4. the next start installs it and starts it; the new build is up to date" >> $OUT
scenario '{"compare": {'"$AHEAD"', '"$IDENT"'}, "archive": "ok.tar"}'
run --update-test --update-url $U
check "exit 0" '[ $RC = 0 ]'
check "installed B over A, started it" 'grep -q "installed b5ad2c3 over a0c1e2f" $L && grep -q "Updater test: done, installed b5ad2c3 and started it" $L'
check "the new process: now running B, updated from A" 'grep -q "now running b5ad2c3 (version 0.02.7-dev), updated from a0c1e2f" $L'
check "it compares B with main: up to date" 'grep -q "/compare/$B...main" $S/requests.log && grep -q "up to date (main is at b5ad2c3)" $L'
check "NeonTide.exe is now the build of B" '[ "$(sha256sum < $G/NeonTide.exe)" = "$(sha256sum < $EXE_B)" ]'
check "pending files and .old gone" '[ ! -e $G/NeonTide.exe.pending ] && [ ! -e $G/NeonTide.exe.pending.json ] && [ ! -e $G/NeonTide.exe.old ]'
check "the update folder of B (its log) stays" '[ -f $UPD/$B/build.log ]'

echo "=== 5. a failed build: its log, failed.txt, not built again with the same compiler" >> $OUT
fresh $EXE_A; tools_mingw; scenario '{"compare": {'"$AHEAD"'}, "archive": "fail.tar"}'
run --update-test --update-url $U
check "exit 2" '[ $RC = 2 ]'
check "build failed, exit code" 'grep -q "the update build failed (build.bat exited with 1)" $L'
check "the log tail in the game log" 'grep -q "build.log| src.main.cpp(12): error C2065" $L'
check "failed.txt names the toolchain" 'grep -q "toolchain=MinGW-w64" $UPD/$B/failed.txt'
check "no pending update" '[ ! -e $G/NeonTide.exe.pending ]'
run --update-test --update-url $U
check "again: exit 2, not built again, no download" '[ $RC = 2 ] && grep -q "failed before with MinGW-w64" $L && ! grep -q archive $S/requests.log'

echo "=== 6. another compiler tries again: Visual Studio found by vswhere" >> $OUT
check "after the failure: the archive and the log stay, the source is gone" '[ -f $UPD/$B/source.tar.gz ] && [ -f $UPD/$B/build.log ] && [ ! -e $UPD/$B/src ] && [ ! -e $UPD/$B/x ]'
# (a commit's archive never changes; here the server's other archive stands for a source that MSVC builds)
rm -f $UPD/$B/source.tar.gz
tools_msvc; scenario '{"compare": {'"$AHEAD"'}, "archive": "ok.tar"}'
run --update-test --update-url $U
check "exit 0" '[ $RC = 0 ]'
check "MSVC from vswhere" 'grep -q "building with MSVC (C:\\\\FakeVS\\\\BuildTools)" $L'
check "the build has no PATH prefix, failed.txt gone" 'blog | grep -q "^NT_COMMIT=$B$" && ! blog | grep -qi "^PATH=C:\\\\w64devkit" && [ ! -e $UPD/$B/failed.txt ]'

echo "=== 7. rejected builds" >> $OUT
for c in nocommit notpe noexe; do
  fresh $EXE_A; tools_mingw; scenario '{"compare": {'"$AHEAD"'}, "archive": "'$c'.tar"}'
  run --update-test --update-url $U
  case $c in
    nocommit) m="the build does not carry its commit";;
    notpe) m="the build is not a Windows program";;
    noexe) m="build.bat made no bin.NeonTide.exe";;
  esac
  check "$c: exit 2, '$m', no pending files" '[ $RC = 2 ] && grep -q "$m" $L && [ ! -e $G/NeonTide.exe.pending ] && [ ! -e $G/NeonTide.exe.download ]'
done

echo "=== 8. rejected archives and downloads" >> $OUT
for c in wrongfolder twotop; do
  fresh $EXE_A; tools_mingw; scenario '{"compare": {'"$AHEAD"'}, "archive": "'$c'.tar"}'
  run --update-test --update-url $U
  case $c in wrongfolder) m="does not name commit b5ad2c3";; twotop) m="holds 2 entries at the top";; esac
  check "$c: exit 2, '$m', the archive is deleted" '[ $RC = 2 ] && grep -q "$m" $L && [ ! -e $UPD/$B/source.tar.gz ]'
done
fresh $EXE_A; tools_mingw; scenario '{"compare": {'"$AHEAD"'}, "archive": "ok.tar", "redirect": "evil"}'
run --update-test --update-url $U
check "redirect to another host: refused before connecting" '[ $RC = 2 ] && grep -q "redirected to a refused URL https://example.com/" $L && [ ! -e $UPD/$B/source.tar.gz ]'
scenario '{"compare": {'"$AHEAD"'}, "archive": "missing.tar"}'
run --update-test --update-url $U
check "archive 404: not downloaded, tried again next start" '[ $RC = 2 ] && grep -q "was not downloaded (HTTP 404): the next start tries again" $L'

echo "=== 9. no update: identical, behind, diverged, unknown to GitHub, rate limited, server error, not JSON" >> $OUT
for st in identical behind diverged 404 403 500 junk; do
  fresh $EXE_A; tools_mingw; scenario '{"compare": {"'$A'": "'$st'"}}'
  run --update-test --update-url $U
  case $st in
    identical) m="up to date (main is at a0c1e2f)";;
    behind) m="this build is 2 commits ahead of main";;
    diverged) m="commit a0c1e2f is not on main (diverged: 1 commit of its own, 2 new on main)";;
    404) m="GitHub does not know commit a0c1e2f";;
    403) m="no update (HTTP 403 (rate limited))";;
    500) m="no update (HTTP 500)";;
    junk) m="the comparison could not be read";;
  esac
  check "$st: exit 1, one request, '$m'" '[ $RC = 1 ] && [ "$(grep -c . $S/requests.log)" = 1 ] && grep -qF "$m" $L'
done

echo "=== 10. a build without a commit updates to main's newest, once" >> $OUT
fresh $EXE_U; tools_mingw; scenario '{"compare": {'"$IDENT"'}, "archive": "ok.tar"}'
run --update-test --update-url $U
check "exit 0, no compare, 10 commits listed" '[ $RC = 0 ] && ! grep -q compare $S/requests.log && grep -q "/commits?sha=main&per_page=10" $S/requests.log'
check "latest pull requests listed" 'grep -q "Latest: #55 Night lighting realism, #54 Taxi stands at the airport, #53 The hand-built world" $L'
check "record from unknown" 'record | grep -q "\"from\": \"unknown\""'
run --update-test --update-url $U
check "installed over unknown; the new build is up to date" '[ $RC = 0 ] && grep -q "installed b5ad2c3 over unknown" $L && grep -q "updated from unknown" $L && grep -q "up to date (main is at b5ad2c3)" $L'

echo "=== 11. a changed pending update is discarded" >> $OUT
fresh $EXE_A; tools_mingw; scenario '{"compare": {'"$AHEAD"'}, "archive": "ok.tar"}'
run --update-test --update-url $U
printf 'x' >> $G/NeonTide.exe.pending
run --update-test --update-url $U
check "one byte more: discarded, NeonTide.exe unchanged" 'grep -q "discarded the built update ([0-9]* bytes instead of [0-9]*)" $L && [ "$(sha256sum < $G/NeonTide.exe)" = "$(sha256sum < $EXE_A)" ]'
fresh $EXE_A; tools_mingw
run --update-test --update-url $U
sed -i "s/\"from\": \"$A\"/\"from\": \"$(echo $A | tr a-f f-k | tr g-k 0-4)\"/" $G/NeonTide.exe.pending.json
run --update-test --update-url $U
check "built for another build: discarded" 'grep -q "discarded the built update (built for .*, not for this build (a0c1e2f))" $L'

if [ -z "${E2E_GAME:-}" ]; then   # (the harness exits by itself after 10 s; the game would open its window)
echo "=== 12. the game exiting ends the build (job object)" >> $OUT
fresh $EXE_A; tools_mingw; scenario '{"compare": {'"$AHEAD"'}, "archive": "slow.tar"}'
run --update-url $U
check "the build started; all processes gone well before the 40 s build would end" 'grep -q "building b5ad2c3" $L && [ $SECS -lt 35 ]'
check "no build process left" '! ps -eo args | grep -v grep | grep -qi "ping.exe\|build.bat"'
check "no pending update" '[ ! -e $G/NeonTide.exe.pending ]'
fi

kill $SRVPID
echo "RESULT: $pass passed, $fail failed" >> $OUT
echo "RESULT: $pass passed, $fail failed"
