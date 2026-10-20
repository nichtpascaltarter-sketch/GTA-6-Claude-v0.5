#!/bin/sh
# A private Wine prefix for the updater tests (/tmp/upd2/wine), with VERSION visible to Windows programs checked
sh /tmp/tools/gate.sh 3300 >/dev/null
export WINEDEBUG=-all WINEPREFIX=/tmp/upd2/wine
mkdir -p $WINEPREFIX
/usr/lib/wine/wine64 wineboot -i > /tmp/upd2/wineboot.log 2>&1
/usr/lib/wine/wineserver64 -w
ls $WINEPREFIX/drive_c/windows/system32 | grep -ci "cmd.exe\|ping.exe\|tar.exe"
ls $WINEPREFIX/drive_c/users/
VERSION=9.9.9 /usr/lib/wine/wine64 cmd /c "echo VERSION=[%VERSION%] PATH=[%PATH%] SystemDrive=[%SystemDrive%] USERPROFILE=[%USERPROFILE%] LOCALAPPDATA=[%LOCALAPPDATA%]" 2>&1 | tail -2
/usr/lib/wine/wineserver64 -w
