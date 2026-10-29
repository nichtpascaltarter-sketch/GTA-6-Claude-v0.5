# NEON TIDE

An original open-world action game set in the fictional sub-tropical state of **Palmera**: the metropolis
**Porto Sol**, the barrier island **Sol Beach**, the **Coral Keys**, **The Sawgrass** wetlands, **Cypress Ridge** and
more, on a 20.5 km seamless map. Two protagonists, **Mari** and **Dex**, a three-act story, strangers and side jobs, a
living city with traffic, pedestrians, police, wildlife and public transit.

Everything is written from scratch in C++17 with no engine, no third-party code and no external assets. The world,
buildings, characters, vehicles, animation, textures, sound effects, music, radio shows and voices are all generated
by the game's own code at load time. The only dependencies are Windows itself (Win32, Direct3D 12, DXGI, XInput,
WASAPI, and WinHTTP and BCrypt for updates) and the compiler's standard library.

## Running

- Windows 10/11, 64-bit, a Direct3D 12 GPU and driver (target: 60 fps at 2560x1440 on the High preset with an RTX
  4070-class card), 16 GB RAM recommended. Without Direct3D 12 the game says so in a message box and closes.
- Double-click `NeonTide.exe`. There is nothing to install. The first launch compiles and caches the shaders
  (about half a minute), and every launch generates the world (a few seconds on a modern CPU).
- Settings, saves, photos and benchmark results live in `%LOCALAPPDATA%\NeonTide\`
  (`settings.ini`, `saves\`, `Photos\`, `benchmark.txt`), with the log, the shader cache and the updater's builds.
  `--datadir=<dir>` (or the environment variable `NEONTIDE_DATADIR`) puts all of them in another folder, so test
  runs leave the player's own files alone.
- The game keeps itself up to date by building GitHub's newest source on your PC (see [Updates](#updates)). The
  update installs the next time you start the game, or right away with **Restart now** in the main menu. Settings >
  Gameplay > Automatic Updates turns this off, and so does starting the game with `--no-update`.

## Building

- **Windows:** run `build.bat`. It uses MSVC when Visual Studio (Build Tools) is installed and falls back to MinGW-w64
  `g++`. Output: `bin\NeonTide.exe`, statically linked with no DLL dependencies. `build.bat bundle` also gives the
  exe the [bundled compiler](#the-bundled-compiler) for the updater.
- **Linux cross-compile:** `./build.sh` (MinGW-w64 g++ 13, static). `./build.sh debug` builds with symbols.
- **Version and commit:** every build carries the commit it was built from: git's `HEAD` when the folder is a git
  checkout, or the `NT_COMMIT` environment variable, which takes precedence (otherwise `unknown`). The main menu shows
  the version and the commit's first 7 digits, and the updater compares the commit with `main`. `VERSION=0.02.7
  ./build.sh` (Windows: `set VERSION=0.02.7`, then `build.bat`) names the version; the source carries `0.02.7-dev`
  (`src/core/version.h`). Updates no longer need GitHub releases.

## Updates

Updates come from the `main` branch on GitHub, which holds only merged pull requests. The game builds them from
source on your PC; open or unmerged pull requests are never built.

- At startup the game asks GitHub how its commit compares with `main`. That takes one request, or two when there is
  an update; GitHub allows 60 an hour per address. Only when `main` has new commits that the running build lacks does
  it list them: the main menu and the log show the pull requests they merged, for example "3 new updates: #55 ...".
- The game then downloads `main`'s source (about 6 MB, over https from github.com only, with certificate checks on)
  and unpacks it with Windows' own `tar.exe`. It builds it with `build.bat` in the background at below-normal
  priority, in `%LOCALAPPDATA%\NeonTide\updates\<commit>\` (or under `--datadir`). The main menu shows each step
  and the build's running time. The exe it built must be a 64-bit Windows program that carries the new commit. It
  is kept next to `NeonTide.exe` until the next start, which checks it again (size, SHA-256, program header) and
  swaps it in.
- **What building needs:** Windows 10 version 1803 or later (for `tar.exe`) and a C++ compiler, either of these:
  - [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/), free, with the "Desktop
    development with C++" workload. The game finds it with `vswhere`, as `build.bat` does.
  - [w64devkit](https://github.com/skeeto/w64devkit/releases) unpacked to `C:\w64devkit` (or `%USERPROFILE%\w64devkit`),
    or MinGW-w64 GCC from MSYS2 in `C:\msys64\ucrt64` or `C:\msys64\mingw64`, or any `g++` and `windres` on `PATH`.

  - Or none: an exe made with `build.bat bundle` carries its own compiler (see
    [The bundled compiler](#the-bundled-compiler)).

  Without a compiler, the main menu says what building needs, and nothing is downloaded.
- Some PCs will not start a program that the build has just made: an antivirus, or Windows' Smart App Control, may
  block a new one. One of the tools `build.bat` makes and runs, `build\worldc.exe`, packs the hand-built world; when
  it will not start (it gets a second try 5 seconds later), the game is built without that world and `build.log` says
  so. The game is complete without it: only `--world=hand` has nothing to load. So updates keep working on such a PC.
- If a build fails, the game keeps running the version it has. The main menu offers **Open log**, which opens
  `build.log` in the update's folder. That commit is not built again with the same compiler; the next commit on
  `main` is. Old update folders are removed. A build without a commit (`unknown`) updates to `main`'s newest once. A
  build is not updated when it is already at `main`'s newest commit, ahead of `main`, on another branch, or built from
  a commit GitHub does not know.

### The bundled compiler

`build.bat bundle` builds the game as usual and then appends a C++ compiler to `bin\NeonTide.exe`: a cut-down
[w64devkit](https://github.com/skeeto/w64devkit) 2.9.1 for x64 (MinGW-w64 GCC), about 61 MB, which makes the exe
about 76 MB. It holds what `build.bat`'s MinGW-w64 branch needs (the compiler drivers, `cc1plus` and `cc1`, `as`,
`ld`, `windres`, the headers and libraries) and the license texts.

- **Making it:** once per PC, `build.bat bundle` downloads the pinned release
  (`w64devkit-x64-2.9.1.7z.exe`, 61 MB) with Windows' `curl.exe`, checks its SHA-256
  (`9208c19755cd4964b7915b9afcf02c66d493a4c870c4b3e83f6c538d9c1237a5`), unpacks it with its own self-extractor and
  packs the parts it needs with `tar.exe` into `%LOCALAPPDATA%\NeonTide\toolchain\payload.tgz` (with `payload.txt`).
  Later `build.bat bundle` runs reuse that file. `tools\payload.cpp`, built in the same run, appends it to the exe
  with a footer that holds its size, SHA-256 and name. This step is optional: when something goes wrong, the exe
  stays as built, without the compiler, and `build.bat` still succeeds and says why. The download is not kept in the
  repository, and neither is anything else of the compiler except `tools\toolchain\` (the license texts and notes).
- **Using it:** when the PC has neither Visual Studio's C++ tools nor MinGW-w64, the updater unpacks the exe's
  compiler with `tar.exe` to `%LOCALAPPDATA%\NeonTide\toolchain\<first 16 digits of its SHA-256>\` (once, after
  checking its SHA-256; it takes about 340 MB) and builds with it. `build.bat` then takes `g++` from that folder
  (`NT_MINGW`). The main menu says that the update is unpacked and built with the bundled compiler.
- **Passing it on:** every exe the updater builds gets the same compiler appended: the running exe's, or else the one
  `build.bat bundle` keeps on this PC. `<exe>.pending.json`'s size and SHA-256 cover the whole file. The compiler
  stays at the version it was packed with until `build.bat bundle` runs again with a newer pin.
- **Licenses and source:** GCC 16.2.0 and GNU Binutils 2.47 are under the GNU GPL version 3 or later (GCC's runtime
  libraries with the GCC Runtime Library Exception 3.1, so the game's own license is not affected), GMP 6.3.0, MPFR
  4.2.2 and MPC 1.4.1 under the GNU LGPL version 3 or later, Mingw-w64 14.0.0 under the licenses in its
  `COPYING.MinGW-w64-runtime.txt`, and w64devkit's recipe is public domain. The texts are in the payload's
  `licenses\` folder (and in `tools\toolchain\`). Whoever shares an exe that carries the compiler shares these
  programs, so their source code has to stay available; it is here:
  - w64devkit 2.9.1 (the build recipe, with every version, checksum and patch it uses):
    https://github.com/skeeto/w64devkit/tree/v2.9.1
  - GCC 16.2.0: https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz
  - GNU Binutils 2.47: https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz
  - GMP 6.3.0: https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz
  - MPFR 4.2.2: https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz
  - MPC 1.4.1: https://ftp.gnu.org/gnu/mpc/mpc-1.4.1.tar.xz
  - Mingw-w64 14.0.0: https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v14.0.0.tar.bz2

## Controls

The keys below are the defaults. Settings > Key Bindings rebinds every keyboard and mouse action, and the controller
has standard, alternate and southpaw layouts.

**On foot**

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Move / look | W A S D / mouse | left stick / right stick |
| Sprint / walk toggle | Shift / Alt | A / - |
| Jump, vault, climb | Space | X |
| Crouch (stealth) | Ctrl | L3 |
| Take cover | Q | RB |
| Aim (lock-on for melee) / fire or attack | right mouse / left mouse | LT / RT |
| Reload (heavy attack with melee weapons) | R | B |
| Weapon wheel / next weapon / slots | Tab / mouse wheel / 1-9 | LB / D-pad left-right |
| Enter vehicle | F | Y |
| Interact (jobs, passenger seat, vehicle ability) | G | R3 |
| Phone | Up arrow | D-pad up |
| Map / pause | M / Esc | - / Start |
| First-person / third-person view | V | Back |
| Focus ability | Caps Lock | L3 + R3 |
| Weapon flashlight on/off | H | D-pad down |

Melee: aim with a melee weapon or fists to lock on. Attack chains combos, Reload is a heavy attack, Cover blocks
(time it for a counter) and Jump dodges. Attack from behind while crouched for a stealth takedown. Aim a gun at a
shop clerk to hold up the store. In first person, aiming raises the gun to its sights (red-dot and reflex optics show
their dot, magnifying scopes a scope view); the sniper rifle always aims through its scope.

**Vehicles**

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Accelerate / brake-reverse / steer | W / S / A D | RT / LT / left stick |
| Handbrake | Space | RB |
| Horn / headlights | E / H | L3 / D-pad down |
| Look behind | C | R3 |
| Camera views (hold for cinematic camera) | V | Back |
| Radio station | Page Up / Page Down, Q, mouse wheel | D-pad right / left |
| Exit | F | Y |
| Aircraft: throttle / yaw / pitch and roll | W S / A D / arrow keys | triggers / LB RB / left stick |

## Benchmark

`NeonTide.exe --benchmark`, or Settings > Display & Graphics > Run Benchmark, plays five scripted scenes (downtown
at noon, Sol Beach at sunset, Calle Luna in night rain, a city flyover and the Sawgrass in fog) with vsync off. It
then shows and saves the average fps, 1 % and 0.1 % lows, worst frame, and CPU simulation and render-submission
times to `%LOCALAPPDATA%\NeonTide\benchmark.txt`. Options: `--quality 0..3`, `--width W --height H`,
`--benchseconds N`.

## Developer and test options

- `--datadir=<dir>` (or `NEONTIDE_DATADIR=<dir>`): the folder for the log, settings, saves, photos, benchmark results,
  caches and the updater's builds, instead of `%LOCALAPPDATA%\NeonTide\`. Test runs on a player's PC keep their files
  out of the player's own folder. The folder is made if it is missing, and the log's first line names it.
- `--play`: skip the menu into a new game. `--firstperson`: first person on foot and in vehicles (`--autoplay fpguns` then
  screenshots every gun at the hip, aiming, sprinting, reloading and through scopes; `--tourstart N` /
  `--tourcount M` pick steps; `--autoplay melee --meleeweapon knife|bat` fights with a blade or a bat).
- `--autoplay walk|drive|bike|fly|boat|shoot|melee|tour|panic|chase|soak|metro|bus|ferry` runs scripted play-tests with
  periodic screenshots and telemetry in the log; at its end it logs its frames (`autoplay frames`: median, p99, max, how
  many took over 33 ms, the streaming's share) and the slowest with what they streamed in. Related flags:
  `--autoduration S`, `--autoevery S`, `--renderevery N`, and `--tourstart N` / `--tourcount M` for tour slices.
  `--autoplay camfade` holds a pedestrian across the camera's line of sight and in front of the first-person eyes to
  check the camera fade; `--autoplay uishots` screenshots the HUD, pause menu, map, settings pages, stats, briefing,
  phone apps and the wanted HUD.
- `--missiontest ...`: automated campaign and side-activity runs.
- `--shot x,y,z,yaw,pitch,hour,name`: free-camera screenshots. Related flags: `--viewer vehicles|characters`,
  `--weaponshowcase x,y,z`, `--wildlifetest`.
- Graphics: `--d3ddebug` enables the Direct3D 12 debug layer (needs the Windows Graphics Tools feature) and logs
  its messages; `--gfxselftest` runs the graphics-layer self-test (bindless, root constants, indirect draws and
  dispatches, async compute, aliasing, mip generation) and exits with 0 when it passes; `--gputimers` and
  `--synctimers` log per-pass timings.
- Updates: `--no-update` skips the updater. `--update-url URL` asks the server at URL instead of api.github.com and
  github.com (plain http only to 127.0.0.1 or localhost), and it checks even during automated runs. `--update-test`
  installs a built update, or else checks, downloads, builds and verifies one, without opening a window, then exits
  (0 done, 1 no update, 2 not built). `--update-bundled` builds updates with the bundled compiler even when another
  one is installed. Other automated runs (`--autotest`, `--benchmark`, `--shot` ...) never update.
- Native test harnesses live in `tests/` (vehicle physics, animation, audio, wildlife, transit, graphics layer,
  updater logic), plus `tools/worldcheck.cpp` for world data.

`PROGRESS.md` holds the architecture notes, the status of every system, the known issues, and the milestone
scorecards against GTA 6.
