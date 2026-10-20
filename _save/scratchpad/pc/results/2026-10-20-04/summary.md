# PC result 2026-10-20-04

- **id:** 2026-10-20-04
- **kind:** shots
- **ref:** 7b9146dffc92b67853291c38c699ae0978e5797d (snapshot 62 on claude/neon-tide, PR #55)
- **started:** 2026-10-20T16:29:22Z (the package was seen at 16:28:01Z, 54 s after it was queued; `started` reached
  GitHub at 16:29:26Z)
- **finished:** 2026-10-20T16:38:05Z
- **status:** pass

The user was not at the PC, so the run went ahead without waiting for a yes (rule 5). The game window was open for
36 s.

## Commands

In the lasting clone `%USERPROFILE%\NeonTide-pc`:

1. `git worktree add --detach ../neon-tide-pc 7b9146dffc92b67853291c38c699ae0978e5797d`
2. In the worktree: `cmd /c .\build.bat`, from 16:32:04Z to 16:34:10Z
3. Made `pcdata\` and `shots\`, and wrote `pcdata\settings.ini`: `fullscreen=0` and `master_volume=0` (see note 2)
4. The run, from 16:34:32Z, with the package's arguments and the two paths unquoted (see note 1), and
   `NEONTIDE_DATADIR=<worktree>\pcdata\` in its environment as a fallback:
   ```
   bin\NeonTide.exe --autotest --width 1920 --height 1080 --quality 3 --settle 16 --shot -1380.43,-971.07,97.11,-20.00,-38.00,18.60,westbrook_pools_aerial_sunset --shot -1283.07,-946.51,54.28,20.00,-45.00,21.50,westbrook_pools_night --shot 998.88,-892.81,6.79,98.41,-1.78,16.50,calle_luna_rejas --shot -8799.21,-9371.35,2.63,-68.42,-1.79,9.00,key_solano_pickets_morning --shot 912.57,-3572.62,5.51,-98.20,2.30,17.00,grove_coral_wall --shotdir <worktree>\shots\ --datadir=<worktree>\pcdata
   ```
   The log's `Data dir:` line named pcdata 0.9 s after the start (rule 4); the run would have been stopped at once if
   it had not, and after 30 minutes at the latest.
5. Each `shots\<name>.bmp` to `<name>.jpg` at quality 95 with System.Drawing (a JPEG encoder with an
   `Encoder.Quality` parameter)
6. `git worktree remove --force ../neon-tide-pc` (`pcdata\` and `shots\` are not in .gitignore, so a plain remove
   refuses)

## Report

| | |
|---|---|
| build.bat's return code | 0 (126.1 s; `bin\NeonTide.exe` 15921664 bytes; no warning or error lines) |
| the run's return code | 0 |
| the run's wall time | 35.9 s, start to exit: `Init done in 32.63 s (shaders: 129 compiled in 26.95 s)`, then the five shots in 1.4 s |
| `Unhandled`, `FATAL`, `error`, `debug layer` lines | none |

The log's `Data dir:` line:

```
[   0.001] Neon Tide starting (version 0.02.7-dev, commit 7b9146dffc92b67853291c38c699ae0978e5797d). Data dir: <worktree>\pcdata\
```

The `Saved ...` lines:

```
[  33.918] Saved <worktree>\shots\westbrook_pools_aerial_sunset.bmp
[  33.969] Saved <worktree>\shots\westbrook_pools_night.bmp
[  34.276] Saved <worktree>\shots\calle_luna_rejas.bmp
[  34.452] Saved <worktree>\shots\key_solano_pickets_morning.bmp
[  34.711] Saved <worktree>\shots\grove_coral_wall.bmp
```

The updater stayed off, as it should in automated runs: `Updater: no update check (automated run)`. The player's own
data folder was not touched.

## Notes for the lead

1. **The package's command line swallows `--datadir` on Windows.** `--shotdir "<worktree>\shots\"` ends in `\"`,
   which `CommandLineToArgvW` reads as an escaped quote, so that argument would run on to the end of the line, take
   `--datadir=...` with it, and the game would write to the player's own `%LOCALAPPDATA%\NeonTide\`. The PC passed both
   paths unquoted (the worktree's path has no spaces). Packages could write `--shotdir <worktree>\shots\` unquoted,
   or double the last backslash inside quotes (`"<worktree>\shots\\"`).
2. **`master_volume=0`** was added to `settings.ini` so that the unattended run made no sound. It does not change the
   pictures.
3. **Two things to look at in the shots:** ring-shaped light spots in the leaf shadows on the facade in
   `grove_coral_wall`, and small bright speckles on the metal roof in `key_solano_pickets_morning`.

## Files

- `results/2026-10-20-04/started`
- `results/2026-10-20-04/summary.md`
- `results/2026-10-20-04/log.txt`: the run's log.txt, 284 lines, scrubbed per rule 3: paths as `<worktree>` and
  `%USERPROFILE%`, and the graphics adapter's name and VRAM redacted on the `Direct3D 12 device created` line
- `results/2026-10-20-04/westbrook_pools_aerial_sunset.jpg` (1920x1080, 742240 bytes)
- `results/2026-10-20-04/westbrook_pools_night.jpg` (1920x1080, 396586 bytes)
- `results/2026-10-20-04/calle_luna_rejas.jpg` (1920x1080, 424250 bytes)
- `results/2026-10-20-04/key_solano_pickets_morning.jpg` (1920x1080, 370056 bytes)
- `results/2026-10-20-04/grove_coral_wall.jpg` (1920x1080, 544535 bytes)
