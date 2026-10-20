@echo off
rem Builds bin\NeonTide.exe from source with one command.
rem Uses MSVC (cl.exe) if available (auto-detected via vswhere), otherwise MinGW-w64 g++.
setlocal
cd /d "%~dp0"
if not exist build\gen mkdir build\gen
if not exist bin mkdir bin
rem set VERSION=0.02.7 first to name the version the exe shows (src\core\version.h)
set "NTVER="
if defined VERSION set "NTVER=-DNT_VERSION=\"%VERSION%\""
rem The commit the exe is built from, which the updater compares with GitHub's main (src\core\version.h): NT_COMMIT
rem when set (the updater sets it for the builds it makes), else git's HEAD when this folder is a git checkout itself
rem (not inside another one), else unknown
set "NTCOMMIT=%NT_COMMIT%"
if not defined NTCOMMIT if exist .git for /f "usebackq delims=" %%c in (`git rev-parse HEAD 2^>nul`) do set "NTCOMMIT=%%c"
if not defined NTCOMMIT set "NTCOMMIT=unknown"
for /f "delims=0123456789abcdef" %%x in ("%NTCOMMIT%") do set "NTCOMMIT=unknown"
set "NTVER=%NTVER% -DNT_COMMIT=\"%NTCOMMIT%\""

set "VSINSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if defined VSINSTALL if not exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL="
if defined VSINSTALL (
  call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
  if errorlevel 1 exit /b 1
  goto msvc
)

where cl >nul 2>nul
if %errorlevel%==0 goto msvc

where g++ >nul 2>nul
if %errorlevel%==0 goto mingw
echo ERROR: No C++ compiler found. Install Visual Studio (Desktop C++) or MinGW-w64.
exit /b 1

:msvc
echo Building with MSVC...
cl /nologo /std:c++17 /utf-8 /O2 /EHsc tools\embed_shaders.cpp /Fe:build\embed_shaders.exe /Fo:build\embed_shaders.obj || exit /b 1
build\embed_shaders.exe src\shaders build\gen\shaders_embedded.h || exit /b 1
rem The hand-built world (docs\HANDBUILT_WORLD.md): tools\worldc compiles world\ into build\gen\world.bin and world.rc,
rem and rc turns them into build\gen\world.res for the link (src\world\handworld.cpp reads it with --world=hand)
cl /nologo /std:c++17 /utf-8 /O2 /EHsc /I. tools\worldc.cpp /Fe:build\worldc.exe /Fo:build\worldc.obj || exit /b 1
build\worldc.exe --world=world --out=build\gen\world.bin || exit /b 1
pushd build\gen || exit /b 1
rc /nologo /fo world.res world.rc || exit /b 1
popd
set "WORLD_RES=build\gen\world.res"
cl /nologo /std:c++17 /utf-8 /O2 /Oi /GS- /EHsc /MT /DNDEBUG /bigobj /W3 /wd4244 /wd4267 /wd4305 /wd4018 /wd4146 /wd4996 %NTVER% /Ibuild\gen src\main.cpp /Fe:bin\NeonTide.exe /Fo:build\main.obj /link /SUBSYSTEM:WINDOWS dxgi.lib uuid.lib ole32.lib oleaut32.lib winmm.lib shell32.lib user32.lib gdi32.lib kernel32.lib avrt.lib winhttp.lib bcrypt.lib %WORLD_RES% || exit /b 1
echo Built bin\NeonTide.exe
exit /b 0

:mingw
echo Building with MinGW-w64 g++...
g++ -O2 -std=c++17 tools\embed_shaders.cpp -o build\embed_shaders.exe || exit /b 1
build\embed_shaders.exe src\shaders build\gen\shaders_embedded.h || exit /b 1
rem The hand-built world (docs\HANDBUILT_WORLD.md): tools\worldc and windres, as in the MSVC branch
g++ -O2 -std=c++17 -I. tools\worldc.cpp -o build\worldc.exe || exit /b 1
build\worldc.exe --world=world --out=build\gen\world.bin || exit /b 1
pushd build\gen || exit /b 1
windres world.rc -O coff -o world_res.o || exit /b 1
popd
set "WORLD_OBJ=build\gen\world_res.o"
g++ -std=c++17 -O2 -DNDEBUG -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing %NTVER% -Ibuild\gen src\main.cpp -o bin\NeonTide.exe -s -static -static-libgcc -static-libstdc++ -mwindows -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt -lwinhttp -lbcrypt %WORLD_OBJ% || exit /b 1
echo Built bin\NeonTide.exe
exit /b 0
