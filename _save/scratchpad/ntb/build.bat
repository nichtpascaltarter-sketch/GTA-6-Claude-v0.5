@echo off
rem Builds bin\NeonTide.exe from source with one command.
rem Uses MSVC (cl.exe) if available (auto-detected via vswhere), otherwise MinGW-w64 g++.
setlocal
cd /d "%~dp0"
if not exist build\gen mkdir build\gen
if not exist bin mkdir bin

where cl >nul 2>nul
if %errorlevel%==0 goto msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if defined VSINSTALL (
  call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
  goto msvc
)

where g++ >nul 2>nul
if %errorlevel%==0 goto mingw
echo ERROR: No C++ compiler found. Install Visual Studio (Desktop C++) or MinGW-w64.
exit /b 1

:msvc
echo Building with MSVC...
cl /nologo /O2 /EHsc tools\embed_shaders.cpp /Fe:build\embed_shaders.exe /Fo:build\embed_shaders.obj || exit /b 1
build\embed_shaders.exe src\shaders build\gen\shaders_embedded.h || exit /b 1
cl /nologo /std:c++17 /O2 /Oi /GS- /EHsc /MT /DNDEBUG /bigobj /W3 /wd4244 /wd4267 /wd4305 /wd4018 /wd4146 /wd4996 /Ibuild\gen src\main.cpp /Fe:bin\NeonTide.exe /Fo:build\main.obj /link /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib uuid.lib ole32.lib oleaut32.lib winmm.lib shell32.lib user32.lib gdi32.lib kernel32.lib avrt.lib || exit /b 1
echo Built bin\NeonTide.exe
exit /b 0

:mingw
echo Building with MinGW-w64 g++...
g++ -O2 -std=c++17 tools\embed_shaders.cpp -o build\embed_shaders.exe || exit /b 1
build\embed_shaders.exe src\shaders build\gen\shaders_embedded.h || exit /b 1
g++ -std=c++17 -O2 -DNDEBUG -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Ibuild\gen src\main.cpp -o bin\NeonTide.exe -s -static -static-libgcc -static-libstdc++ -mwindows -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt || exit /b 1
echo Built bin\NeonTide.exe
exit /b 0
