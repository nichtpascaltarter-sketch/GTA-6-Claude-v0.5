@echo off
rem Builds bin\NeonTide.exe from source with one command.
rem Uses MSVC (cl.exe) if available (auto-detected via vswhere), otherwise MinGW-w64 g++.
rem "build.bat bundle" also gives the exe the bundled compiler for the updater (README: The bundled compiler; :bundle).
setlocal
cd /d "%~dp0"
set "NTBUNDLE="
if /i "%~1"=="bundle" set "NTBUNDLE=1"
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

rem NT_MINGW, a folder with g++ and windres (the updater sets it for the bundled compiler): that MinGW-w64, nothing else
set "NTCXX="
if not defined NT_MINGW goto find_compiler
if not exist "%NT_MINGW%\g++.exe" echo ERROR: NT_MINGW=%NT_MINGW% has no g++.exe.
if not exist "%NT_MINGW%\g++.exe" exit /b 1
set "PATH=%NT_MINGW%;%PATH%"
goto mingw

:find_compiler
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
rem The hand-built world (docs\HANDBUILT_WORLD.md): tools\worldc compiles world\ into build\gen\world.bin and world.rc
rem (:worldc), and rc turns them into build\gen\world.res for the link (src\world\handworld.cpp reads it with
rem --world=hand); without them the exe is built without it
cl /nologo /std:c++17 /utf-8 /O2 /EHsc /I. tools\worldc.cpp /Fe:build\worldc.exe /Fo:build\worldc.obj || exit /b 1
call :worldc
set "WORLD_RES="
if not defined WORLDOK goto msvc_link
pushd build\gen || exit /b 1
rc /nologo /fo world.res world.rc || exit /b 1
popd
set "WORLD_RES=build\gen\world.res"
:msvc_link
cl /nologo /std:c++17 /utf-8 /O2 /Oi /GS- /EHsc /MT /DNDEBUG /bigobj /W3 /wd4244 /wd4267 /wd4305 /wd4018 /wd4146 /wd4996 %NTVER% /Ibuild\gen src\main.cpp /Fe:bin\NeonTide.exe /Fo:build\main.obj /link /SUBSYSTEM:WINDOWS dxgi.lib uuid.lib ole32.lib oleaut32.lib winmm.lib shell32.lib user32.lib gdi32.lib kernel32.lib avrt.lib winhttp.lib bcrypt.lib %WORLD_RES% || exit /b 1
echo Built bin\NeonTide.exe
set "NTCXX=msvc"
goto built

:mingw
echo Building with MinGW-w64 g++...
g++ -O2 -std=c++17 tools\embed_shaders.cpp -o build\embed_shaders.exe || exit /b 1
build\embed_shaders.exe src\shaders build\gen\shaders_embedded.h || exit /b 1
rem The hand-built world (docs\HANDBUILT_WORLD.md): tools\worldc and windres, as in the MSVC branch
g++ -O2 -std=c++17 -I. tools\worldc.cpp -o build\worldc.exe || exit /b 1
call :worldc
set "WORLD_OBJ="
if not defined WORLDOK goto mingw_link
pushd build\gen || exit /b 1
windres world.rc -O coff -o world_res.o || exit /b 1
popd
set "WORLD_OBJ=build\gen\world_res.o"
:mingw_link
g++ -std=c++17 -O2 -DNDEBUG -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing %NTVER% -Ibuild\gen src\main.cpp -o bin\NeonTide.exe -s -static -static-libgcc -static-libstdc++ -mwindows -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt -lwinhttp -lbcrypt %WORLD_OBJ% || exit /b 1
echo Built bin\NeonTide.exe
set "NTCXX=mingw"
goto built

:built
if defined NTBUNDLE call :bundle
exit /b 0

rem ------------------------------------------------------------------------------------------------------------------
rem tools\worldc, just built, compiles world\ into build\gen\world.bin and world.rc: WORLDOK when it did. Some machines
rem will not start a program that was just built (an antivirus, or Windows' Smart App Control, may block a new one), so
rem it gets one more try 5 seconds later; then the game is built without the hand-built world. The game is complete
rem without it: only --world=hand has nothing to load (it says so), and updates keep working on such a machine.
:worldc
set "WORLDOK="
build\worldc.exe --world=world --out=build\gen\world.bin && set "WORLDOK=1"
if defined WORLDOK exit /b 0
echo NOTE: build\worldc.exe did not run. Trying again in 5 seconds...
ping -n 6 127.0.0.1 >nul
build\worldc.exe --world=world --out=build\gen\world.bin && set "WORLDOK=1"
if defined WORLDOK exit /b 0
echo NOTE: build\worldc.exe did not run again, so the exe is built without the hand-built world: the game is complete, only --world=hand has nothing to load.
exit /b 0

rem ------------------------------------------------------------------------------------------------------------------
rem build.bat bundle (README: The bundled compiler): bin\NeonTide.exe also carries a MinGW-w64 GCC, which the updater
rem builds updates with on a machine without a compiler and passes on to every exe it builds. Once per machine, the
rem pinned w64devkit is downloaded with curl.exe, checked against its SHA-256, unpacked by its own self-extractor and cut
rem down by tar.exe to what the MinGW branch above needs, with the texts in tools\toolchain\, into
rem %LOCALAPPDATA%\NeonTide\toolchain\payload.tgz (payload.txt says what it is); tools\payload.cpp appends it to the exe.
rem Optional: on any problem the exe stays as built, without it, and the build still succeeds.
:bundle
setlocal
set "TCVER=2.9.1"
set "TCSHA=9208c19755cd4964b7915b9afcf02c66d493a4c870c4b3e83f6c538d9c1237a5"
set "TCLABEL=w64devkit %TCVER%"
set "TCGCC=libexec/gcc/x86_64-w64-mingw32/16.2.0"
set "TCHOME="
set "WHY=LOCALAPPDATA is not set"
if not defined LOCALAPPDATA goto bundle_none
set "TCHOME=%LOCALAPPDATA%\NeonTide\toolchain"
set "TCX=%LOCALAPPDATA%\NeonTide\toolchain\x"
set "TCDL=%LOCALAPPDATA%\NeonTide\toolchain\w64devkit-x64-%TCVER%.7z.exe"
if not exist "%TCHOME%\" mkdir "%TCHOME%"
set "WHY=tools\payload.cpp did not build"
if "%NTCXX%"=="msvc" cl /nologo /std:c++17 /utf-8 /O2 /EHsc tools\payload.cpp /Fe:build\payload.exe /Fo:build\payload.obj
if "%NTCXX%"=="mingw" g++ -O2 -std=c++17 tools\payload.cpp -o build\payload.exe -static
if errorlevel 1 goto bundle_none
if not exist build\payload.exe goto bundle_none
rem the payload this machine keeps, else a new one from the pinned download
build\payload.exe check "%TCHOME%\payload.tgz" "%TCHOME%\payload.txt" "%TCLABEL%" 2>nul
if not errorlevel 1 goto bundle_attach
echo Making the bundled compiler from %TCLABEL% (once per machine, in %TCHOME%)...
if exist "%TCDL%" goto bundle_check
set "WHY=curl.exe is missing (Windows 10 version 1803 and later have it)"
if not exist "%SystemRoot%\System32\curl.exe" goto bundle_none
set "WHY=the download failed"
"%SystemRoot%\System32\curl.exe" -fL --retry 2 -o "%TCDL%.part" "https://github.com/skeeto/w64devkit/releases/download/v%TCVER%/w64devkit-x64-%TCVER%.7z.exe"
if errorlevel 1 goto bundle_none
move /y "%TCDL%.part" "%TCDL%" >nul
if errorlevel 1 goto bundle_none
:bundle_check
set "WHY=the download does not have the pinned SHA-256 (it is deleted; the next build.bat bundle downloads it again)"
build\payload.exe sha256 "%TCDL%" %TCSHA%
if not errorlevel 1 goto bundle_unpack
del "%TCDL%"
goto bundle_none
:bundle_unpack
set "WHY=its self-extractor did not unpack it"
if exist "%TCX%\" rd /s /q "%TCX%"
start "" /wait "%TCDL%" -y -o"%TCX%"
if not exist "%TCX%\w64devkit\bin\g++.exe" goto bundle_none
set "WHY=the texts in tools\toolchain\ could not be added"
mkdir "%TCX%\w64devkit\licenses"
copy /y tools\toolchain\*.txt "%TCX%\w64devkit\licenses\" >nul
if errorlevel 1 goto bundle_none
copy /y "%TCX%\w64devkit\COPYING.MinGW-w64-runtime.txt" "%TCX%\w64devkit\licenses\" >nul
if errorlevel 1 goto bundle_none
rem what the MinGW branch needs: the drivers, cc1plus (and cc1, which windres preprocesses with), as, ld, windres,
rem the headers and the libraries (all import libraries: a new -l in this file still builds), not what it never uses
set "WHY=tar.exe could not pack it"
"%SystemRoot%\System32\tar.exe" -czf "%TCHOME%\payload.tgz.part" -C "%TCX%\w64devkit" --exclude "*.idl" --exclude "*.la" --exclude "lib/ccache" --exclude "lib/bfd-plugins" --exclude "lib/pkgconfig" --exclude "lib/libbfd.a" --exclude "lib/libopcodes.a" --exclude "lib/libctf*.a" --exclude "lib/libsframe.a" --exclude "*gfortran*" --exclude "*caf_*" --exclude "*finclude*" --exclude "*gomp*" --exclude "*quadmath*" --exclude "*stdc++exp*" --exclude "*-gdb.py" --exclude "*install-tools*" licenses bin/g++.exe bin/gcc.exe bin/as.exe bin/ld.exe bin/windres.exe %TCGCC%/cc1plus.exe %TCGCC%/cc1.exe %TCGCC%/collect2.exe include lib
if errorlevel 1 goto bundle_none
move /y "%TCHOME%\payload.tgz.part" "%TCHOME%\payload.tgz" >nul
if errorlevel 1 goto bundle_none
set "WHY=payload.txt could not be written"
build\payload.exe record "%TCHOME%\payload.tgz" "%TCHOME%\payload.txt" "%TCLABEL%"
if errorlevel 1 goto bundle_none
rd /s /q "%TCX%"
del "%TCDL%"
:bundle_attach
set "WHY=it could not be appended"
build\payload.exe attach bin\NeonTide.exe "%TCHOME%\payload.tgz" "%TCHOME%\payload.txt"
if errorlevel 1 goto bundle_none
exit /b 0
:bundle_none
echo NOTE: bin\NeonTide.exe is built, but without the bundled compiler: %WHY%.
if defined TCHOME if exist "%TCHOME%\x\" rd /s /q "%TCHOME%\x"
if defined TCHOME if exist "%TCHOME%\payload.tgz.part" del "%TCHOME%\payload.tgz.part"
exit /b 0
