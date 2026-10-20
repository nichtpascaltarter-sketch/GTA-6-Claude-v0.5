#!/bin/sh
# The game as commit A (QUICK=1: -O1, for the tests): bin/nt_upd2.exe in the private tree
cd /tmp/upd2/tree || exit 1
sh /tmp/tools/gate.sh 2500
echo "build start $(date +%T)"
NT_COMMIT=a0c1e2f3a4b5c6d7e8f90123456789abcdef0a1b QUICK=1 OUT=bin/nt_upd2.exe ./build.sh > /tmp/upd2/build_game.log 2>&1
echo "build rc=$? $(date +%T)"
grep -E "error|warning" /tmp/upd2/build_game.log | head -20
ls -la bin/nt_upd2.exe
