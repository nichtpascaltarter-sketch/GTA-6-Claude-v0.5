# NEON TIDE

An original open-world action game set in the fictional sub-tropical state of **Palmera**: the metropolis
**Porto Sol**, the barrier island **Sol Beach**, the **Coral Keys**, **The Sawgrass** wetlands, **Cypress Ridge** and
more, on a 20.5 km seamless map. Two protagonists, **Mari** and **Dex**, a three-act story, strangers and side jobs, a
living city with traffic, pedestrians, police, wildlife and public transit.

Everything is written from scratch in C++17 with no engine, no third-party code and no external assets. The world,
buildings, characters, vehicles, animation, textures, sound effects, music, radio shows and voices are all generated
by the game's own code at load time. The only dependencies are Windows itself (Win32, Direct3D 11, DXGI, XInput,
WASAPI) and the compiler's standard library.

## Running

- Windows 10/11, 64-bit, a Direct3D 11 GPU (target: 60 fps at 2560x1440 on the High preset with an RTX 4070-class
  card), 16 GB RAM recommended.
- Double-click `NeonTide.exe`. There is nothing to install. The first launch compiles and caches the shaders
  (about half a minute), and every launch generates the world (a few seconds on a modern CPU).
- Settings, saves, photos and benchmark results live in `%LOCALAPPDATA%\NeonTide\`
  (`settings.ini`, `saves\`, `Photos\`, `benchmark.txt`).

## Building

- **Windows:** run `build.bat`. It uses MSVC when Visual Studio (Build Tools) is installed and falls back to MinGW-w64
  `g++`. Output: `bin\NeonTide.exe`, statically linked with no DLL dependencies.
- **Linux cross-compile:** `./build.sh` (MinGW-w64 g++ 13, static). `./build.sh debug` builds with symbols.

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

- `--play`: skip the menu into a new game. `--firstperson`: start in first person (`--autoplay fpguns` then
  screenshots every gun at the hip, aiming, sprinting, reloading and through scopes).
- `--autoplay walk|drive|bike|fly|boat|shoot|melee|tour|panic|chase|soak|metro|bus|ferry` runs scripted play-tests
  with periodic screenshots and telemetry in the log. Related flags: `--autoduration S`, `--autoevery S`,
  `--renderevery N`, and `--tourstart N` / `--tourcount M` for tour slices. `--autoplay camfade` holds a
  pedestrian across the camera's line of sight and in front of the first-person eyes to check the camera fade.
- `--missiontest ...`: automated campaign and side-activity runs.
- `--shot x,y,z,yaw,pitch,hour,name`: free-camera screenshots. Related flags: `--viewer vehicles|characters`,
  `--weaponshowcase x,y,z`, `--wildlifetest`.
- Native test harnesses live in `tests/` (vehicle physics, animation, audio, wildlife, transit), plus
  `tools/worldcheck.cpp` for world data.

`PROGRESS.md` holds the architecture notes, the status of every system, the known issues, and the milestone
scorecards against GTA 6.
