@echo off
setlocal
cd /d "%~dp0\..\.."
if not exist build mkdir build
rem Run from an x64 Native Tools Command Prompt (CI sets up vcvars64 first).
cl /nologo /std:c++17 /utf-8 /O2 /EHsc /W3 /wd4100 /wd4244 /wd4267 /wd4996 tests\ui\pause_menu_test.cpp /Fe:build\pause_menu_test.exe /Fo:build\pause_menu_test.obj /link user32.lib || exit /b 1
build\pause_menu_test.exe
exit /b %errorlevel%
