# Neon Tide save point: how to resume

This branch (`claude/neon-tide-save`) was written when the session paused at about 21:20 UTC on 2026-10-01, because
the account's usage limit was nearly reached. It is a save point, not for merging. Verified work is on `main`.

## What is in it
- **Root of the tree:** the shared working tree at the time of saving. That is `claude/neon-tide` HEAD (equal to
  `main` after PR #24) plus every agent's uncommitted, unverified work in progress.
- **`_save/scratchpad/`:** the lead's scratchpad, byte for byte. It holds:
  - snapshot scripts (`snap_smoke.sh`, `commit_snap.sh`, `memgate.sh`, `wip_backup.sh`, `save_all.sh`);
  - the running log `overnight.md`, blob lists, snapshot messages;
  - the agents' work dirs (`aiwork/`, `loco/`, `locotour/`, `b3/`, `base4/`, `dev4/`, `citylab/`, `wc/`, `city/`,
    `faces/`, `shc_d3d12/` ...);
  - **`resume/*.md`**, one note per agent: its state, exact paths, the next steps and the test commands.
- **`_save/tmp/<dir>/`:** the agents' private trees from `/tmp`: `fx/` (renderer: `gd` phase 1, `gd2` phase 2,
  `gd3` phase 3, `cs2`...), `faces/` (`batch2/` ready to send, `dev3/` batch 3), `city/`, and older finished ones
  (`story2`, `sp`, `int`, `world`, `ui`, `wl`, `d3d12bug`).
- **`_save/bundles/*.bundle`:** full git history of the nested repos under `/tmp/fx`. Their working files are in
  `_save/tmp/fx/...`.
- **`_save/claude_tasks/`:** the lead's task list.
- **`_save/MANIFEST_skipped.tsv`:** every file left out, with size and sha256. These are regenerable binaries: exes,
  object files, screenshots, audio renders. Rebuild exes with `build.sh` and re-render shots with the scripts. Wine
  prefixes were not saved; they are recreated on first run.
- **`_save/SYMLINKS.tsv`:** the symlinks in those dirs.

Not saved here: the conversation transcripts. This repository is public and they contain personal details. The
lead's conversation continues in the Claude Code session itself.

## Restore in a fresh container
```sh
cd /home/user/GTA-6-Claude-v0.5
git fetch origin claude/neon-tide-save main
git checkout -B claude/neon-tide origin/main                                 # the verified state
git restore --source=origin/claude/neon-tide-save --worktree -- . ':!_save'  # agents' unverified edits, as modifications
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad  # (same session = same path)
mkdir -p $S && git archive origin/claude/neon-tide-save _save/scratchpad | tar -x --strip-components=2 -C $S
git archive origin/claude/neon-tide-save _save/tmp | tar -x --strip-components=2 -C /tmp
# nested repo history (example; one bundle per repo, named after its path):
git archive origin/claude/neon-tide-save _save/bundles | tar -x -C /tmp/save_bundles_parent 2>/dev/null || true
# git clone --no-checkout /tmp/save_bundles_parent/_save/bundles/tmp_fx_gd2.bundle /tmp/x && mv /tmp/x/.git /tmp/fx/gd2/
```
Files that did not exist at save time are not deleted by `git restore`. Check `git status` afterwards.

## Queue on resume (details in `_save/scratchpad/resume/*.md`)
1. **Faces batch 2:** fully verified on main and ready.
   - Copy `/tmp/faces/batch2/*`: four files into `src/anim/`, two into `tests/anim/`.
   - Send `/tmp/faces/msg/batch2.txt` as the sign-off.
   - Snapshot it (`snap_smoke.sh`), then PR, merge and fast-forward.
   - It brings the goggles fix, brows, waterline, satin lips and the hair stripes fix.
2. **Animation locomotion:** the 4 files are on disk in the shared tree. They pass the harness, anim_test, the O2 build
   and 39 s in Wine.
   - Still to do: redo the in-game after run (`sh $S/locotour/run.sh after && sh $S/locotour/montage.sh after`).
   - Then snapshot it.
   - The graft text is in `resume/animation.md` §2.
3. **AI:**
   - Sets 3+4: built as `nt_ai62`, tests not run yet.
   - Set 5: untested.
   - The couple and reach blocks for peds.cpp are in `aiwork/couple_block.txt` and `aiwork/reach_block.txt`.
   - See `resume/ai.md`.
4. **City:**
   - Batch 3: worldcheck and shots OK; the story regression (27 missions) and the 20 district views are pending.
     `$S/base4` is ready to build.
   - Batch 4: work in progress in `$S/dev4`.
   - See `resume/city.md`.
5. **Renderer:**
   - Phase 2, decor plants (`/tmp/fx/gd2`): the garden-stop verification is incomplete.
   - Phase 3, palms and hedge canopy (`/tmp/fx/gd3`): built, not run.
   - See `resume/renderer.md`.

## Rules that still apply
- Snapshots are built inclusion-based from HEAD plus only the signed-off blobs. Never `git add -A`.
- Merge only after `snap_smoke.sh` passes: syntax check, O2 build, shaders, drive smoke and melee smoke.
- The container's memory cap is 14.3 GB, and D3D12 games use about 3 GB each. Every run goes through
  `tools/run.sh` / `build.sh`, which gate on memory.
- The user asked for auto-merge of every verified snapshot: open a PR, merge it (merge commit), fast-forward
  `claude/neon-tide`.
