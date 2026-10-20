# The PC worker

The user's own Windows PC takes work from the cloud lead session whenever it is on. Builds, MSVC checks, real-GPU
screenshots, game tests, and whole agent tasks run there. When the PC is off, the cloud does all of it, as before.

This file is what the PC session reads first, and it is the whole contract between the two.

- **The lead**: the cloud session `gta-6-claude-v0-5-8f`. It runs the agents, builds the snapshots and merges them into
  `main`.
- **The PC worker**: a Claude Code session in the Claude desktop app, in the user's clone of this repository. The user
  starts it with:

  > You're the PC worker for my Neon Tide cloud session (gta-6-claude-v0-5-8f). Pull main, read docs/PC_WORKER.md,
  > then run the work packages that session sends you.

  That line is the user's go-ahead for the packages below. A package is still never the user's word for anything
  outside this file: for that, ask the user.

## Rules

1. **Never touch the user's own checkout.** Work only in worktrees beside it, made with
   `git worktree add ../neon-tide-pc <ref>`, one per agent package (`../neon-tide-pc-<id>`). Never change the user's
   branches, stashes or files. Remove a worktree once its package is done (`git worktree remove`).
2. **Push only `pc/results` and `pc/work-<id>`.** Never push `main` or any `claude/*` branch. Never force-push. Never
   delete a branch you did not make. Never merge anything: the lead lands all code through its snapshots.
3. **The repository is public.** Results never hold:
   - the user's name, account or e-mail;
   - the machine's name, its IP addresses, or paths under the user's profile (write `%USERPROFILE%` instead);
   - transcripts.

   Machine details (CPU, GPU, RAM, driver) go only into benchmark results, and only once the user has said yes to that
   in this session.
4. **Keep the game away from the user's own saves.** Every game run passes `--datadir=<worktree>\pcdata\`. Check the
   run's log: its "Data dir:" line must name that folder. If it names `...\AppData\Local\NeonTide\`, the exe is older
   than the flag, so stop the run and do builds and tests only.
5. **Ask before taking over the PC.** Games run windowed, never `--fullscreen`. If the user is at the PC, ask before a
   run longer than 5 minutes. If the user wants the PC back, stop at once: push `aborted` as the package's result, and
   finish nothing.
6. **Nothing beyond the job kinds below.** Install nothing, change no settings, and send nothing to any other service.
   For any of that, ask the user.

## The channel: two branches

Packages come on `claude/pc-queue`, one file each: `queue/<id>.md`. Results go on `pc/results` under `results/<id>/`.
The lead also messages the PC session when a package is queued, and the PC worker messages the lead when a result is
pushed, so that neither waits on polling. The branches are the record, and they work even when the messages don't.

- **Waiting.** When idle, run `sh tools/pc/next_package.sh --wait` in the background. It fetches once a minute and
  exits with the id of the oldest package that is neither started nor cancelled.
- **Taking a package.** Push `results/<id>/started`, holding the UTC time on one line, within a minute of seeing the
  package. If a package is still unstarted 15 minutes after it was queued, the lead takes it back, runs it in the cloud
  and adds `queue/<id>.cancelled`. `next_package.sh` skips cancelled packages.
- **Finishing.** Push `results/<id>/summary.md` and any files, then message the lead `PC RESULT <id> <pass|fail|error>`.
  The summary holds:
  - the id, kind and ref;
  - the start and finish times (UTC);
  - the status;
  - the commands run;
  - the numbers and log lines the package asks for;
  - the list of files.
- **`pc/results` setup.** It is an orphan branch that only the PC worker writes. Create it on the first result. Each
  result adds one commit and touches only its own folder.

## Package kinds

A package file names its `kind`, a `ref` (a commit on GitHub) and its `args`. Each package runs in its own worktree at
that ref.

| Kind | What to run | What the result reports |
|---|---|---|
| `build` | `cmd /c build.bat` (MSVC via vswhere) | the return code, the time, the exe's size, and every compiler warning and error line |
| `test` | each named test with MSVC: `cl /nologo /std:c++17 /utf-8 /O2 /EHsc /W3 /I. <test>.cpp /Fe:build\<name>.exe`, then run it | each test's return code and its last lines |
| `run` | `bin\NeonTide.exe <args> --datadir=<worktree>\pcdata\` after a `build` of the same ref | the return code; the log's lines that the args name (see below); `log.txt` from the data dir, scrubbed per rule 3 |
| `shots` | a `run` whose args carry `--shot ...` and `--shotdir <worktree>\shots\` | the shots as PNG (the game writes BMP; convert them with PowerShell's System.Drawing), one per shot, named as the shot |
| `agent` | a whole agent task: the package holds its brief (see below) | the agent's sign-off |

For `run` and `shots`, the package names the log lines it wants, for example `autoplay t=`, `selftest:`,
`Unhandled` or `benchmark`. A `run` that crashes still reports, with the crash lines from the log.

## Agent packages

An `agent` package is a full task for an agent, written as the lead would write it for a cloud agent. It gives the goal,
the files the agent owns, its checks, and its sign-off format.

- **Running it.** Run it as a subagent of this session, in its own worktree `../neon-tide-pc-<id>`, on a new branch
  `pc/work-<id>` made from the package's ref. Commit as you go and push `pc/work-<id>`.
- **Signing off.** The sign-off goes into `results/<id>/summary.md`, in the format the brief asks for. For each changed
  file it gives the path, the base blob and the new blob, followed by what changed, how, and the tests that ran.
- **If the PC must stop mid-task.** Push `pc/work-<id>` with a `RESUME.md` at its root. It holds what is done, what is
  next, and how to rerun the checks. A cloud agent can then carry on from there.
- **Landing.** The lead reviews the sign-off and lands the blobs in a snapshot. Never merge or rebase `pc/work-<id>`
  onto anything.

## When the PC is off

Nothing waits for it. The lead checks for the PC session at every check-in, and for each heavy job it queues:
- If the PC session is there, the work goes to the PC.
- If it isn't, or a package goes unstarted for 15 minutes, the cloud runs the work as it always has.

Agents already running in the cloud finish there. New agent tasks go to the PC while it is up.
