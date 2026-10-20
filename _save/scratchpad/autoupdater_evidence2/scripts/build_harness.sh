#!/bin/sh
# The updater harness (platform + updater, no game) as commit A, commit B and without a commit, and the test stubs
cd /tmp/upd2/tree || exit 1
A=a0c1e2f3a4b5c6d7e8f90123456789abcdef0a1b; B=b5ad2c3e0f1e2d3c4b5a69788796a5b4c3d2e1f0
CXX=x86_64-w64-mingw32-g++
LIBS="-static -static-libgcc -static-libstdc++ -mwindows -lwinhttp -lbcrypt -lshell32 -lole32 -lwinmm -luser32"
for v in a b u; do
  case $v in a) def="-DNT_COMMIT=\"$A\"";; b) def="-DNT_COMMIT=\"$B\"";; u) def=;; esac
  sh /tmp/tools/gate.sh 2500 >/dev/null
  $CXX -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter $def -Isrc /tmp/upd2/chk/harness.cpp -o /tmp/upd2/chk/harness_$v.exe $LIBS 2>&1 \
    | grep -E "error|warning" | grep -v "src/platform/win32.cpp\|src/core/math.cpp" | head -20
  ls -la /tmp/upd2/chk/harness_$v.exe
done
for s in stub_tar stub_tool fake_vswhere; do
  sh /tmp/tools/gate.sh 2500 >/dev/null
  $CXX -std=c++17 -O1 -Wall /tmp/upd2/stubs/$s.cpp -o /tmp/upd2/stubs/$s.exe -static -static-libgcc -static-libstdc++ 2>&1 | head -10
  ls -la /tmp/upd2/stubs/$s.exe
done
