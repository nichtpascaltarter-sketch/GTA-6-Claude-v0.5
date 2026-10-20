# PC result 2026-10-20-03

- **id:** 2026-10-20-03
- **kind:** run (a build of the ref, then starts of the game with the updater on)
- **ref:** 7b9146dffc92b67853291c38c699ae0978e5797d (P: snapshot 62, the head of PR #55)
- **started:** 2026-10-20T16:40:16Z (the package was seen at 16:38:56Z, 60 s after it was queued; `started` reached
  GitHub at 16:40:20Z)
- **finished:** 2026-10-20T17:01:15Z
- **status:** pass

The user was not at the PC, so the runs went ahead without waiting for a yes (rule 5). The package ran twice: pass 1
passed every stage, but the package's `SendKeys` and `Shot` recipes did not do their job on this PC (note 1). Pass 2
ran it again with those fixed, and the results below are pass 2's unless they say otherwise. Each pass had the game
open for about 2.5 minutes.

## Commands

In the lasting clone `%USERPROFILE%\NeonTide-pc`:

1. `git ls-remote origin refs/heads/main`, `git log -1 --format=%s <M>`
2. `git worktree add --detach ../neon-tide-pc-2026-10-20-03 7b9146dffc92b67853291c38c699ae0978e5797d`
3. Pass 1 (data folder `pcdata`), from 16:43:46Z, the package's steps 1 to 7 in PowerShell:
   - `cmd /c .\build.bat`
   - `settings.ini` with `fullscreen=0`, `auto_update=1` and `master_volume=0`
   - `Start-Process bin\NeonTide.exe -ArgumentList '--width 1280 --height 720 --datadir="<worktree>\pcdata"'`
   - the log polled every 10 s
   - the `Shot` function, then `pending.json` and `build.log` copied and the `.pending` file hashed
   - `SendKeys('u')` into the game's window once it had the focus
   - after no restart in 20 s: `CloseMainWindow()`, `log_1.txt`, and a second start with the same arguments
   - the new process shot and closed, `log_2.txt`, and the step 7 checks
4. Pass 2 (data folder `pcdata2`, its own results folder), from 16:54:02Z: a fresh `cmd /c .\build.bat` of P, then the
   same steps with three changes:
   - U pressed with `keybd_event` and held for 200 ms;
   - `SetProcessDPIAware()` before the screenshots;
   - each screenshot taken once the log said `Init done`.
5. Scrubbed every copied text file (rule 3), then
   `git worktree remove --force ../neon-tide-pc-2026-10-20-03`

## Stages

| Stage | Result |
|---|---|
| 0, main | pass |
| 1, build of P | pass |
| 2, data folder | pass |
| 3, check, download, build | pass |
| 4, the notice | pass |
| 5, restart | pass, with U (pass 1: close and start again, also pass) |
| 6, the new exe | pass |
| **overall** | **pass** |

### 0, main

- **P:** `7b9146dffc92b67853291c38c699ae0978e5797d`
- **M:** `0bdeab3f4de31a6d5962b85b2779e84f6874bfb3` (from `git ls-remote`; its parents are the previous main `c7a63ae`
  and P)
- **M's subject:** `Merge pull request #55: the game updates itself from main's source code, and test runs keep to their own data folder`

Main did not move: both passes' logs name 0bdeab3.

### 1, build of P

build.bat returned 0 after 119.8 s (pass 1: 121 s). `bin\NeonTide.exe` is 15921664 bytes and carries P (True). The
build had no warning or error lines.

### 2, data folder

```
[   0.000] Neon Tide starting (version 0.02.7-dev, commit 7b9146dffc92b67853291c38c699ae0978e5797d). Data dir: %USERPROFILE%\neon-tide-pc-2026-10-20-03\pcdata2\
```

Pass 1 had the same line, with `pcdata\`.

### 3, check, download, build

```
[   0.697] Updater: comparing with main (running 0.02.7-dev, commit 7b9146d)
[   1.458] Updater: main has 1 new commit
[   1.746] Updater: main is at 0bdeab3 (2026-10-20T16:37:32Z): 1 new update: #55 the game updates itself from main's source code, and test runs keep to their own data folder
[   1.746] Updater:   #55 the game updates itself from main's source code, and test runs keep to their own data folder
[   1.810] Updater: building with MSVC (C:\Program Files\Microsoft Visual Studio\2022\Community)
[   1.810] Updater: downloading https://github.com/<account>/GTA-6-Claude-v0.5/archive/0bdeab3f4de31a6d5962b85b2779e84f6874bfb3.tar.gz
[   2.779] Updater: downloaded the source of 0bdeab3 (6166297 bytes)
[   3.270] Updater: building 0bdeab3 (%USERPROFILE%\neon-tide-pc-2026-10-20-03\pcdata2\updates\0bdeab3f4de31a6d5962b85b2779e84f6874bfb3\build.log)
[  83.134] Updater: built 0bdeab3 in 80 s (15912448 bytes, SHA-256 6f44b76fb170a92d1911e4bb84ba9097974e1fb2332ce38f9bc7d89647d1d76f): it installs at the next start
```

From the start line to `built` took 83.1 s (pass 1: 85.2 s, built in 82 s). The game's own start-up ran alongside:
`Init done in 33.82 s (shaders: 129 compiled in 28.42 s)`.

### 4, the notice

`1_update_ready.png` shows the main menu's corner panel with all three parts:

- **UPDATE READY**
- the PR line, the same text as the log's: "1 new update: #55 the game updates itself from main's source code, and
  test runs keep to their own data folder", then "It installs the next time you start Neon Tide."
- **RESTART NOW**, with its U key

The version line above the panel reads `Version 0.02.7-dev • 7b9146d`.

`pending.json`:

```json
{"commit": "0bdeab3f4de31a6d5962b85b2779e84f6874bfb3", "from": "7b9146dffc92b67853291c38c699ae0978e5797d", "size": 15912448, "sha256": "6f44b76fb170a92d1911e4bb84ba9097974e1fb2332ce38f9bc7d89647d1d76f", "toolchain": "MSVC (C:\\Program Files\\Microsoft Visual Studio\\2022\\Community)", "commits": 1, "pulls": [{"number": 55, "title": "the game updates itself from main's source code, and test runs keep to their own data folder"}]}
```

The `.pending` file is 15912448 bytes, with SHA-256 `6f44b76fb170a92d1911e4bb84ba9097974e1fb2332ce38f9bc7d89647d1d76f`.

`build.log` has both of build.bat's lines, `Building with MSVC...` and `Built bin\NeonTide.exe`, and no cl or link
warning or error lines. Its only other output is vcvars64's
`'vswhere.exe' is not recognized as an internal or external command,`, which is harmless and comes from the PC
session's `NoDefaultCurrentDirectoryInExePath`, as before.

### 5, restart

Pass 2 restarted with **U**:

```
[  95.367] Updater: restarting into 0bdeab3
[  95.440] Updater: installed 0bdeab3 over 7b9146d (%USERPROFILE%\neon-tide-pc-2026-10-20-03\bin\NeonTide.exe)
[  96.090] Updater: started %USERPROFILE%\neon-tide-pc-2026-10-20-03\bin\NeonTide.exe
[   0.001] Updater: now running 0bdeab3 (version 0.02.7-dev), updated from 7b9146d
[   0.515] Updater: removed the previous version (%USERPROFILE%\neon-tide-pc-2026-10-20-03\bin\NeonTide.exe.old)
[   1.206] Updater: up to date (main is at 0bdeab3)
```

The installing process exited with 0.

Pass 1 **closed the game and started it again**. The second start logged `installed 0bdeab3 over 7b9146d` at 0.015 s
and `started` at 0.645 s, then the same `now running`, `removed the previous version` and `up to date` lines followed
(`pass1_log_2.txt`).

### 6, the new exe

`2_after.png` shows the menu with `Version 0.02.7-dev • 0bdeab3` and no notice.

The checks of step 7:

| Check | Value |
|---|---|
| exe carries M | True |
| exe's SHA-256 = pending.json's sha256 | True (`6f44b76f…`) |
| `NeonTide.exe.old`, `.pending`, `.pending.json` exist | False, False, False |
| `pcdata2\updates` | only `0bdeab3f4de31a6d5962b85b2779e84f6874bfb3\build.log` |

Pass 1 gave the same four results, with its own build's hash.

### Anything else

- No line with `not built`, `failed`, `refused`, `rate limited`, `without a job object`, `Unhandled` or `FATAL`, in
  either pass.
- No window other than the game's. During the runs the PC session watched for processes that started with a window
  and found none. The update's tar.exe, cmd.exe and build run with `CREATE_NO_WINDOW`.
- The player's own data folder was not touched.

## Notes for the lead

1. **Two of the package's recipes need changes.**
   - **`SendKeys('u')` never registers.** The game counts a key as pressed when it is down at the start of a frame
     (`keys[k] && !prevKeys[k]`, set from WM_KEYDOWN and WM_KEYUP). SendKeys' down and up arrive in the same pass of
     the message loop, before the next frame. Holding the key works: `keybd_event` down, 200 ms, up. A player's press
     lasts several frames, so only taps shorter than a frame are lost (macros, remote tools). If that matters, the
     window procedure could latch a press until the next frame has read it.
   - **`Shot` cut the window off.** Windows PowerShell is not DPI-aware, so under display scaling `GetWindowRect`
     gives a scaled-down size, and the bitmap held only the top-left 868x517 of a 1302x776 window. Calling user32's
     `SetProcessDPIAware()` before `GetWindowRect` fixes it.
   - **The "after" shot needs to wait for the menu.** It came 3 s after `Updater: up to date`, while the new exe was
     still loading (its `Init done` came at 31 s). Wait for `Init done` before taking it.
2. **The two builds of M are not byte-identical.** Both are 15912448 bytes, but their SHA-256 differ: pass 1
   `3385a0b7…`, pass 2 `6f44b76f…`. MSVC builds are not reproducible byte for byte. Each install was checked against
   its own build's hash, which is what matters.
3. **The first rebuild of P between the passes failed** with
   `LINK : fatal error LNK1104: Datei "bin\NeonTide.exe" kann nicht geöffnet werden.` The exe that pass 1 had just
   installed was held for a short while, with no game process running (most likely a scan). The retry two minutes
   later passed.
4. **`master_volume=0`** was added to `settings.ini` so that the menu stayed silent during the unattended runs.
5. **The files write the repository's owner as `<account>`** (rule 3), for example in the download URL.

## Files

- `results/2026-10-20-03/started`
- `results/2026-10-20-03/summary.md`
- `results/2026-10-20-03/1_update_ready.png`, `2_after.png`: pass 2, 1302x776, the game's window only
- `results/2026-10-20-03/log_2.txt`: pass 2, the whole run in one file. U keeps the same log going, so pass 2 has no
  `log_1.txt`.
- `results/2026-10-20-03/build.log`, `pending.json`: pass 2
- `results/2026-10-20-03/pass1_log_1.txt`, `pass1_log_2.txt`: pass 1, closed and started again

All text files are scrubbed per rule 3:

- paths as `%USERPROFILE%`;
- `<user>` for the user's name and `<pc>` for the machine's;
- `<account>` for the repository's owner;
- the graphics adapter's name and VRAM redacted.
