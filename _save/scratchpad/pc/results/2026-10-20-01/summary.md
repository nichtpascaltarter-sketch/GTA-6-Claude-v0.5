# PC result 2026-10-20-01

- **id:** 2026-10-20-01
- **kind:** build
- **ref:** bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e (main)
- **started:** 2026-10-20T14:12:33Z (the package was seen at 14:11:47Z; `started` reached GitHub at 14:16:41Z, see note 5)
- **finished:** 2026-10-20T14:20:47Z
- **status:** pass

## Commands

In a clone of the repository, in the worktree `../neon-tide-pc` beside it:

1. `git worktree add --detach ../neon-tide-pc bcb8a71a0a5f8eea6d057663b06d30bb5d4fe37e`
2. `cmd /c .\build.bat`, stdout and stderr to a log, from 14:15:48Z to 14:17:45Z (`cmd /c build.bat` did not start, see note 1)
3. For the compiler lines: `vcvars64.bat`, then `cl` and `link /?` on their own
4. `git worktree remove ../neon-tide-pc`

The game was not run.

## Numbers

| | |
|---|---|
| build.bat's return code | 0 |
| build.bat's wall time | 116.7 s |
| `bin\NeonTide.exe` | 15770112 bytes (SHA-256 `b9945153cde551f4757e8ff86c14bd26b268e3daf91bd7bfaaf89e870e0cbd00`) |
| cl | `Microsoft (R) C/C++-Optimierungscompiler Version 19.44.35229 für x64` (the German-language MSVC of Visual Studio 2022 17.14) |
| link | `Microsoft (R) Incremental Linker Version 14.44.35229.0` |
| warning and error lines from cl and link | none: 0 warnings, 0 errors (with build.bat's `/W3` and its `/wd` list) |
| shader files embedded | 40: `Embedded 40 shader files.` (`src\shaders` holds 40 files) |
| worldc | `worldc: 1 file(s) -> build\gen\world.bin (2669552 bytes, 6 sections) and build\gen\world.rc` |

## Log

build.bat's whole output, stdout and stderr:

```
'vswhere.exe' is not recognized as an internal or external command,
operable program or batch file.
Building with MSVC...
embed_shaders.cpp
Embedded 40 shader files.
worldc.cpp
worldc: terrain 2560x2560 at 8 m, 2 water levels, 93 outlines, 7 channels
worldc: 1 file(s) -> build\gen\world.bin (2669552 bytes, 6 sections) and build\gen\world.rc
main.cpp
Built bin\NeonTide.exe
```

## Notes for the lead

1. **`cmd /c build.bat` does not start in the PC session, `cmd /c .\build.bat` does.** The session's environment sets
   `NoDefaultCurrentDirectoryInExePath` (it is not a user or machine variable), so cmd does not look in the current
   folder for a bare command name. Two tries ended at once with `'build.bat' is not recognized as an internal or
   external command`, before anything was built. The `build` row of PC_WORKER.md could say `cmd /c .\build.bat`.
2. **The `'vswhere.exe' is not recognized` line comes from vcvars64.bat, for the same reason.** It goes away when the
   variable is removed. It is harmless: build.bat finds Visual Studio through vswhere's full path, and vcvars64 still
   sets up cl, rc and link.
3. **`sh` cannot run in the PC session.** Git for Windows' sh fails to fork (`dofork: child -1 - forked process ...
   died unexpectedly, exit code 0xC0000142`), so `sh tools/pc/next_package.sh --wait` cannot wait there. The PC
   session waits with a PowerShell loop that does what the script does, and skips `queue/README.md` (note 4).
4. **next_package.sh would return `README`.** `git ls-tree --name-only origin/claude/pc-queue queue/` lists
   `queue/README.md`, `grep '\.md$'` keeps it, and nothing starts or cancels a package "README", so with 2026-10-20-01
   started the script prints `README`. Leaving it out, for example with `grep -v '^queue/README\.md$'`, fixes that.
5. **`started` was late.** It was committed at once (holding 14:12:33Z), but git on the PC had no GitHub credentials
   until the user signed in, so the push went through at 14:16:41Z, five minutes after the package was seen.
6. **No message to the lead.** The PC session cannot reach the cloud session `gta-6-claude-v0-5-8f` (it is not among
   the sessions it can message), so this branch is the whole report.
7. The PC had no clone of the repository, so the PC session made one in a temporary folder and worked in a worktree
   beside it, as rule 1 says. The user's own files were not touched.

## Files

- `results/2026-10-20-01/started`
- `results/2026-10-20-01/summary.md`
