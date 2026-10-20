# PC result 2026-10-20-02

- **id:** 2026-10-20-02
- **kind:** test
- **ref:** 1846b52165daf642a948643def81ad141dc3e6f6 (snapshot 61 on claude/neon-tide, PR #54)
- **started:** 2026-10-20T14:31:46Z (the package was seen at 14:30:49Z, 62 s after it was queued; `started` reached
  GitHub at 14:31:49Z)
- **finished:** 2026-10-20T14:37:41Z
- **status:** pass

## Commands

In the lasting clone `%USERPROFILE%\NeonTide-pc`:

1. `git worktree add --detach ../neon-tide-pc 1846b52165daf642a948643def81ad141dc3e6f6`
2. In the worktree, after `vcvars64.bat` (its environment captured once) and making `build\`, for each item
   `cl /nologo /std:c++17 /utf-8 /O2 /EHsc /W3 /I. <file> /Fe:build\<name>.exe`, then the exe, each through
   `cmd /c ... > log 2>&1` so that stdout and stderr stay in order. All six ran from 14:35:18Z to 14:35:43Z.
3. `git worktree remove ../neon-tide-pc`

The game was not run. In the lines below, `<worktree>` stands for the worktree's path.

## Results

| # | file | compile rc | warning and error lines | run | run rc |
|---|---|---|---|---|---|
| 1 | `tests\compiler\bit_scan_test.cpp` | 0 (0.3 s) | none | `build\bit_scan_test.exe` (0.3 s) | 0 |
| 2 | `tests\updater\update_logic_test.cpp` | 0 (1.0 s) | none | `build\update_logic_test.exe` (0.3 s) | 0 |
| 3 | `tests\world\world_format_test.cpp` | 0 (2.7 s) | none | `build\world_format_test.exe` (0.4 s) | 0 |
| 4 | `tests\world\worldv_test.cpp` | 0 (5.8 s) | 3: C4996 ×3 | `build\worldv_test.exe` (2.0 s) | 0 |
| 5 | `tests\world\worldmodel_test.cpp` | 0 (4.3 s) | 1: C4996 ×1 | `build\worldmodel_test.exe` (0.4 s) | 0 |
| 6 | `tools\worldv.cpp` | 0 (4.6 s) | 2: C4996 ×2 | `build\worldv.exe --world=world --no-cache` (0.3 s) | 0 |

No errors. All six warnings are C4996 for `fopen`: build.bat silences that number with `/wd4996`, the test line does
not.

### Each run's last lines

Every run printed a single line.

1. `Bit-scan regression tests passed (12146 masks).`
2. `Updater logic tests passed (196 checks).`
3. `world_format_test: all passed`
4. `worldv_test: all passed`
5. `worldmodel_test: all passed`
6. `worldv: 1 file (1 checked, 0 from the cache), 109 records, 0 ids, 0 references: 0 errors, 0 warnings (0.00 s)`

### Warning lines

4, `worldv_test.cpp` (3):

```
<worktree>\tests\world\../../src/world/validate.cpp(125): warning C4996: 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
<worktree>\tests\world\../../src/world/validate.cpp(1026): warning C4996: 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
tests\world\worldv_test.cpp(59): warning C4996: 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
```

5, `worldmodel_test.cpp` (1):

```
<worktree>\tests\world\../../src/world/worldmodel.cpp(27): warning C4996: 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
```

6, `worldv.cpp` (2):

```
.\src/world/validate.cpp(125): warning C4996: 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
.\src/world/validate.cpp(1026): warning C4996: 'fopen': This function or variable may be unsafe. Consider using fopen_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
```

## The PowerShell waiter

It worked.

- Package 02 was found by the PC session's PowerShell copy of the waiter, which does what `tools/pc/next_package.ps1`
  does and was already running from 14:21Z, before that file was on the queue branch. It saw 02 at 14:30:49Z, 62 s
  after the queue commit, and `started` was on GitHub a minute later.
- `tools/pc/next_package.ps1` from the queue branch (a55418b), run once without `-Wait` in the lasting clone after 02
  was started, exited 1 with no output, which is right. With `-Wait` it is the waiter from now on. `main` has no
  `tools\pc\` yet, so the PC session runs the file as it is on `origin/claude/pc-queue`.

## Notes for the lead

1. **The lasting clone** is `%USERPROFILE%\NeonTide-pc` with `main` checked out, and package worktrees go beside it at
   `%USERPROFILE%\neon-tide-pc`. Results are committed onto `origin/pc/results` through a temporary index in the
   clone, one commit per result touching only `results/<id>/`, so the clone needs no `pc/results` worktree. The
   temporary clone from package 01 is no longer used.
2. **The tests clean up after themselves:** no `worldv_test_*` or `neontide-worldmodel-test-*` folders were left in
   `%TEMP%`.

## Files

- `results/2026-10-20-02/started`
- `results/2026-10-20-02/summary.md`
