# PC work 2026-10-20-setup: a setup page on the first start

- **id:** 2026-10-20-setup. The user asked for it in the PC session; it was not a queued package.
- **kind:** agent (a code change for the lead to review and land)
- **base:** 0bdeab3f4de31a6d5962b85b2779e84f6874bfb3 (main, the same as the head of claude/neon-tide)
- **branch:** `pc/work-2026-10-20-setup`, commit eceeef09768e826f95868abacf898bf9ee46142c
- **status:** pass (MSVC build and a first-start run)

## What it does

A player's very first start shows a setup page before the main menu. That is a start with no `settings.ini` in the
data folder, and not a test run. The page is a new `MENU_SETUP`, titled "WELCOME TO PALMERA", with these rows:

- **Display:** Fullscreen or Windowed
- **Graphics quality:** Low, Medium, High or Ultra
- **Master volume:** 0 to 100%, in steps of 10%
- **Subtitles:** On or Off
- **Automatic updates:** On or Off
- **Start playing**

How it behaves:

- **Changing values:** Up and Down move between rows. Left and Right change a value. Enter or a click steps it
  forward.
- **Applying:** each change applies at once (`MA_SETTINGS_CHANGED`).
- **Leaving:** Start playing, or Esc ("Skip"), saves `settings.ini` and opens the main menu, so the page shows once.
  Closing the game on the page saves nothing, and the page comes back at the next start.
- **Who skips it:** test runs (`Updater::isAutomatedRun()`: `--autotest`, `--shot`, `--benchmark` and the rest) and
  players who already have a `settings.ini` go straight to the main menu. Harnesses that write a `settings.ini` before
  the first start, such as packages 03 and 04, see no change.

## Files

| File | Base blob | New blob | What changed |
|---|---|---|---|
| `src/ui/hud.h` | f7d1bfafe1048eb9dfaa6c53960a3a609d1041a0 | 0001c663c19bad2492e40a5cae1dd15244f37415 | `MENU_SETUP` added to the end of `MenuScreen` |
| `src/ui/menus.cpp` | 36cfd1a53d9dc23f17b4ef2dbd9aa42cd46c7ea5 | f6e17a99bb93968b0332ccdffa9b6e4048cf6bc6 | see below |
| `src/game/app.cpp` | 30f8a3861e732f01bd8a24bd1cafcbda8ace8b48 | a4c4aee9a5459a6d34ccbeaf07e54b73734bf9de | see below |
| `src/platform/updater.h` | 066489f1cd2f5c720cf7ddc203dad5296907bec2 | d6fefb847634f647fceff766f05cb855840ddab0 | see below |
| `src/platform/updater.cpp` | efd8a9ace6177bc5599d2ba321c0d878b7c64210 | b7ed671e97809a63e62c9fecfedbfdb31768c2b5 | see below |

- **`src/ui/menus.cpp`:**
  - `drawSetup()`: the front-page frame, six rows with the main menu's selection bar, and the footer prompts
  - the `MENU_SETUP` case in `Menus::update`
  - `MENU_SETUP` counts as a front-end root
- **`src/game/app.cpp`:**
  - `firstStart`, set in `loadSettings()` when `settings.ini` is missing
  - `openMainMenu()` opens `MENU_SETUP` when `firstStart` is set and the start is not an automated run
- **`src/platform/updater.h`, `src/platform/updater.cpp`:** `Updater::isAutomatedRun()`, a public wrapper of
  `win_detail::automatedRun()`. Declaring `automatedRun()` itself in the header made its two calls ambiguous (C2668).

## Tests

- **Build:** `cmd /c .\build.bat` (MSVC 19.44) returned 0 in 117.5 s with no warning or error lines.
  `bin\NeonTide.exe` is 15927296 bytes.
- **A first start:** `bin\NeonTide.exe --width 1280 --height 720 --nosound --no-update --datadir=<worktree>\pcdata_first`,
  with an empty data folder.
  - The setup page opened after `Init done` (`setup_screen.jpg`).
  - The log had no `Unhandled`, `FATAL` or `error` lines.
  - Closing the window on the page left no `settings.ini`.
- **Not run:**
  - the key and mouse paths: no keys were sent, because the user was at the PC
  - the updater and world tests
  - a MinGW build

## Notes

- **The page opens in borderless fullscreen.** A first start uses the defaults (`fullscreen` is on), whatever
  `--width` and `--height` say.

## Files in this folder

- `summary.md`
- `setup_screen.jpg`: the first-start page, scaled to 1920x1080
