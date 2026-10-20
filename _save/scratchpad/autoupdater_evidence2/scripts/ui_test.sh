#!/bin/sh
# The menu regression test (tests/ui/pause_menu_test.cpp, the real menus with drawing doubles) built with MinGW and run
# under Wine, on the private tree (base and new code)
for t in tree base; do
  cd /tmp/upd2/$t || exit 1
  sh /tmp/tools/gate.sh 2500 >/dev/null
  x86_64-w64-mingw32-g++ -std=c++17 -O1 -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable \
    -Wno-class-memaccess tests/ui/pause_menu_test.cpp -o /tmp/upd2/chk/pause_menu_test_$t.exe -static -static-libgcc -static-libstdc++ -luser32 2>&1 \
    | grep -E "error|warning" | head -20
  sh /tmp/tools/gate.sh 800 >/dev/null   # (a small test program, not the game)
  WINEDEBUG=-all WINEPREFIX=/tmp/upd2/wine timeout 600 /usr/lib/wine/wine64 /tmp/upd2/chk/pause_menu_test_$t.exe 2>&1 | tail -3
  echo "$t exit=$?"
done
WINEPREFIX=/tmp/upd2/wine /usr/lib/wine/wineserver64 -w
