# Autoupdater agent resume notes
## Setup
- Private tree: /tmp/upd/tree (from git archive HEAD 731a731). Shared tree untouched.
- origin/main = HEAD = 731a731 (local `main` branch is stale "Initial commit", ignore).
## Milestones
- [ ] M0 read code, pick hooks, send design confirmation to main
- [ ] M1 version.h + update_logic (pure) + unit test
- [ ] M2 win32 updater (WinHTTP/BCrypt) + startup hook + UI + settings
- [ ] M3 build (MinGW) ok
- [ ] M4 e2e under Wine local server (download/pending/swap/relaunch/.old, rejections)
- [ ] M5 live API check, autotest smoke, UI screenshots
- [ ] M6 sign-off to main, handback
## Log
- start: fresh, no prior notes
- 19:30 M0 done: design confirmation sent to main. Decisions:
  * /releases/latest 404s (v0.02.6 is a PRE-RELEASE) -> use /releases?per_page=10, skip drafts, max version w/ NeonTide*.exe asset
  * body has no checksum; API asset "digest":"sha256:e8b7..." (verified) -> checksum order: digest, .sha256/SHA256SUMS asset, body
  * hooks: main.cpp (beforeStart/startCheck/afterShutdown), win32.cpp initLog(append) split + log close under mutex,
    app.cpp ONE line `Updater::menuFrame(menu, act);` after Menus::update, hud.h/menus.cpp/settings.cpp/viewer.cpp UI
  * new files: src/core/version.h, src/platform/update_logic.{h,cpp}, src/platform/updater.cpp, tests/updater/*
  * fixtures fetched in /tmp/upd/fetch (latest.json=404 body, releases.json list, tag026.json); v0.02.6 exe in /tmp/upd/dl026 (do not run)
  * own WINEPREFIX /tmp/upd/wine
- Lead APPROVED design (both changes). Extra rules from lead:
  * digest: only sha256 counts; other algorithms -> fall through. First checksum source found wins; mismatch rejects.
  * app.cpp one line based on TODAY's HEAD blob; snapshot 54 (AI set 17) changes app.cpp soon -> re-sign on new HEAD when told.
  * detached thread touches only a leaked/co-owned heap state; closes its own WinHTTP handles; UI link = atomics/mutex snapshot.
    -> plan: Updater::stop() in WinMain after app.run() sets quitting under the state's log mutex (thread never logs after).
  * relaunch passes original args minus internal update flags (we use env NEONTIDE_UPDATED_FROM, no internal flags).
  * CreateProcessW fails after swap -> log + MessageBox "start the game again" (not in test modes), then exit.
  * all network/file errors silent for player except menu notice; never MessageBox from background thread.
- 19:55 M1 code written in /tmp/upd/tree: version.h, update_logic.{h,cpp}, updater.{h,cpp}, win32.cpp initLog, hud.h/settings.cpp/menus.cpp UI,
  viewer.cpp test keywords (updget/updready/updfail, keys u/pady), main.cpp hooks, app.cpp 1 line, build.sh/.bat.
  Unit test tests/updater/update_logic_test.cpp passes (194 checks, ASan/UBSan, -Wshadow -Wconversion clean).
  Harness exe: /tmp/upd/chk/harness.cpp (platform+updater only). Wine prefix /tmp/upd/wine created.
  Gate script /tmp/upd/gate.sh MB (waits for lead flag + memfree). ALWAYS gate Wine launches.
- 20:15 harness e2e under Wine PASSED (download/pending, install+relaunch child logs 9.9.9, .old removal, all rejections,
  digest/notes/none checksum sources, list pick, tampered/truncated/not-newer/no-record pending discarded, gating for
  autotest/autoplay/shot/gfxselftest/benchmark/viewer/no-update). UI pause_menu_test (MinGW+Wine) 2646 checks OK.
  Live: direct HTTPS from sandbox -> GitHub 403 rate limited (shared IP 34.72.41.21, 60/h, 0 left) = env limitation.
  Later edits (after harness runs, need re-test): beforeStart appends log for updater-started process even in automated
  runs; menuFrame stops a download on MA_RUN_BENCHMARK; README updated.
  Queued: /tmp/upd/scripts/build2.sh (bg) -> bin/nt_upd.exe (dev) + bin/nt_upd999.exe (9.9.9, QUICK=1). Output /tmp/upd/build2.out
  Scripts ready: e2e_game.sh (light), shots.sh, smoke.sh, unreach.sh, restart_flow.sh (heavy), live.sh (light; needs
  harness025.exe: build harness with -DNT_VERSION=\"0.02.5\"). Server: python http.server 8765 on /tmp/upd/srv (bg task).
  XTest tool /tmp/upd/xtool/xinject. Private prefix has d3dcompiler_47.
- 20:25 more edits (pre-build): .old now deleted only after the new version's window/device are up (check thread, or
  startCheck when the check is skipped for a non-automated reason; kept when installed update is not newer);
  JSON enum -> JSON_* names; PE lambdas rd16/rd32; casts for /W4; PROGRESS.md "Updates" section. Unit test 194 OK.
  Blob table: /tmp/upd/scripts/blobs.sh (WRITE=1 to write blobs). Heavy scripts: shots.sh smoke.sh unreach.sh
  restart_flow.sh setting_off.sh; light: e2e_game.sh live.sh harness_build.sh. Waiting for lead flag + memory.
  ORDER after builds: harness_build -> e2e_game -> live -> shots -> smoke -> unreach -> restart_flow -> setting_off -> sign-off
- 20:47 Lead: HEAD now 1a83e24 (set 17; app.cpp base blob 17333c63). Private tree moved to 1a83e24 (+ my changes);
  app.cpp new blob 41ec8031 (one line). headfull = 1a83e24. dev exe built 20:31 on OLD base -> queued rebuild
  (build3.sh after build2.sh's 9.9.9 build). Don't edit tree while builds run.
- 21:00 pickExeAsset prefers exact NeonTide-<ver>.exe (197 checks). Waiting on builds (machine saturated).
- 22:11 FINAL builds done (tree frozen; blob list /tmp/upd/blobs_at_build.txt): nt_upd.exe dev 22:10, nt_upd999.exe 21:51. Next: harness_build, e2e_game, live, smoke, shots, unreach, restart_flow, setting_off
- 22:50 e2e_game PASSED (results /tmp/upd/results/e2e_game.txt). live: 403 rate limited (shared IP; proxy not used by Wine
  WinHTTP automatic mode) -> /tmp/upd/results/live.txt. HTTP server bg task hit 2h max -> tests start their own server now.
  Heavy chain running in bg: smoke -> shots -> unreach -> restart_flow -> setting_off (results in /tmp/upd/results/).
- 00:00 smoke PASSED (/tmp/upd/results/smoke.txt, log /tmp/upd/smoke/log.txt). Viewer shots done (/tmp/upd/shots/*.png):
  u -> menu action 12, pady -> 13. Layout fix in menus.cpp drawUpdateCorner (percent on bar row, title fit) + viewer
  'updlong' keyword AFTER the builds -> final_ui.sh (bg) rebuilds bin/nt_upd_final.exe after the chain and reshoots
  into /tmp/upd/shots_final. Chain (bg) now: unreach -> restart_flow -> setting_off.
- 01:45 unreach PASSED, restart_flow PASSED (real game, XTest 'u'), final build nt_upd_final.exe (menus.cpp layout fix +
  viewer updlong) + final shots OK; UI regression 2646/0 + unit 196 on final code. Evidence: $SP/autoupdater_evidence.
  setting_off rerun in bg (on final exe). Then: WRITE=1 blobs.sh, sign-off SendMessage, SubagentHandback.
- 01:50 ALL TESTS DONE. blobs written (WRITE=1). Sending sign-off to main, then handback.
- 01:55 M6 DONE: sign-off sent to main (blobs in /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/autoupdater_evidence/blobs.txt; app.cpp 41ec8031 on 17333c63, c94a642d on 6f3ee5da/HEAD 2b3102e). Handback next.

# SOURCE UPDATES (task 2, from the USER via lead, 2026-10-20 05:18)
- Ask: updater builds from source on main (merged PRs) instead of release exes; shows new PRs. Lead's design: NT_COMMIT
  embedded (build.sh/.bat: git rev-parse HEAD / env NT_COMMIT / unknown); compare/{NT_COMMIT}...main status ahead ->
  update; PR list from merge commits; tarball of main head via api.github.com -> codeload; tar.exe (System32);
  build in %LOCALAPPDATA%\NeonTide\updates\<sha>\ below normal priority, MSVC (vswhere -> vcvars64.bat -> build.bat)
  else MinGW (PATH, w64devkit, MSYS2) else notice; log kept; .pending + record (sha, PRs, sha256) as now; remove
  release path; menu "0.02.7-dev · <short sha>"; README/PROGRESS. Tests: real fixtures (curl compare/pulls), toolchain
  detection pure fns, build-command assembly, Wine e2e local server + stub toolchain, drive smoke.
- Private tree: /tmp/upd2/tree (git archive bcb8a71). Base everything on HEAD bcb8a71.
- 05:45 design confirmation sent to main (requests: compare page=100 -> commits?sha=main -> github.com/archive/H.tar.gz;
  NT_COMMIT env > git (only with .git in the build dir) > unknown; build.bat via cmd /d /c, VERSION removed, PATH prefix
  for known MinGW dirs; LF->CRLF of build.bat; exe must contain H; record {commit,from,size,sha256,prs}; failed.txt
  marker per commit+toolchain; font ASCII only -> dot drawn as circle, titles to ASCII; urls github.com + *.github.com;
  Wine: stub tar.exe in prefix system32, stub toolchains, payload = exe with SHA bytes swapped). Implementing.
- 06:35 CODE WRITTEN in /tmp/upd2/tree (diff vs /tmp/upd2/base = git archive bcb8a71): update_logic.{h,cpp} rewritten
  (compare/commits parsing, pullFromMessage, newChangesSince/changesSummary(+Latest), asciiText, pickToolchain,
  mingwCandidateDirs, splitPathList, quoteArgument, build/unpackCommand, buildEnvironment, archive checks, withCrlf,
  StreamSearch, foldersToRemove, urlAllowed github-only, PendingInfo{commit,from,...}, failedRecord); updater.cpp
  rewritten (manual redirects, runChild job object, findUpdate/buildUpdate/buildSource); updater.h doc; version.h
  NT_COMMIT; build.sh/.bat NT_COMMIT (+ bat: skip VS without vcvars64); hud.h kinds; menus.cpp corner/line; viewer.cpp
  shot keywords; main.cpp comments; win32.cpp start line; README Updates section; PROGRESS; CI step name + MSVC
  commit check step. Unit test 243 checks OK (UBSan, ASan, -Wshadow -Wconversion).
  Fixtures sanitized (trailers/agent identity removed): tests/updater/fixtures/{compare_*,commits_main}.json.
- Scripts /tmp/upd2/scripts: build_harness.sh (harness a/b/u + stubs), mkprefix.sh (/tmp/upd2/wine), e2e.sh
  (12 cases; E2E_GAME=1 e2e.sh <game exe> skips case 12), build_game.sh (bin/nt_upd2.exe QUICK, commit A), shots.sh,
  smoke.sh, ui_test.sh (pause_menu_test tree+base), live.sh (needs chk/harness_live.exe = NT_COMMIT 837b39c...).
  A=a0c1e2f3a4b5c6d7e8f90123456789abcdef0a1b B=b5ad2c3e0f1e2d3c4b5a69788796a5b4c3d2e1f0. Server port 8766.
  ORDER: build_harness (rebuild if updater.cpp newer) -> mkprefix -> e2e -> ui_test -> build_game -> E2E_GAME e2e ->
  shots -> smoke -> live -> blobs -> sign-off.
- 07:38 (before the container restart ~13:30): e2e run 2 on the final harnesses 56/56 PASSED (results/e2e_2.txt);
  menu regression (pause_menu_test MinGW+Wine) tree 2646/0 and base 2646/0; game build bin/nt_upd2.exe (commit A,
  QUICK) rc=0, no warnings, carries A once. Code change after e2e run 1: buildUpdate deletes src after every build
  (archive kept after a failure). worldmodel review sent to city (see tools.md).
- 13:35 RESUMED after restart. Remaining, one at a time: E2E_GAME=1 GATE=3300 e2e (game exe as A) -> shots.sh ->
  smoke.sh -> live.sh -> blobs (WRITE=1) -> sign-off to main -> handback.
- 13:40 LEAD ADDITION: --datadir=<dir> / NEONTIDE_DATADIR (win32.cpp dataDirOverride + userDataDir; platform.h
  comment; updater updatesRoot now = userDataDir()+"updates\"; README Running + Developer options; PROGRESS line).
  All other data writes already go through userDataDir (log, settings, saves, Photos, benchmark, ui_font.bin,
  shadercache). Game-exe e2e (old build) 53/53; shots 13 OK (evidence2/shots); e2e run 3 (harness, with case 13
  datadir flag+env) 62/62. NEXT: build_game.sh (bin/nt_upd2b.exe) -> datadir_game.sh -> smoke.sh -> live.sh -> blobs
  -> sign-off (include --datadir). PC worker live test offered by lead later.
- 15:10 ALL DONE + SIGNED OFF to main: source-built updater + --datadir. Blobs on bcb8a71 in
  /tmp/upd2/blobs_signed.txt (copy in evidence2), windows-build.yml + PROGRESS.md also merged onto 1846b52
  (93c8045f / 814a5e2d). Tests: unit 243, e2e harness 62/62, e2e game exe 53/53, menu regression 2646/0 x2, 13 shots,
  drive smoke OK, datadir game run OK, live = 403 rate limited (shared IP). Pending on lead: merge; PC-worker live test
  (MSVC + real GitHub) when routed. Next for tools role: worldv stage 1 on city's worldmodel v0 (after it lands).
- ~15:40 LEAD ACCEPTED updater + datadir for snapshot 62 (merged yml 93c8045f, PROGRESS 814a5e2d). worldv fix on PR #54
  as 5acba43. Asked for a PC live-test package -> written $SP/pc/2026-10-20-03.md and sent. Found + fixed: PC session
  sets NoDefaultCurrentDirectoryInExePath -> updater now runs `cmd /d /c .\build.bat` (update_logic.cpp d780fc0c, .h
  25db691b, test 09bf4e1f, PROGRESS 83cec335 / on 1846b52 ca9479c7). e2e run 4 62/62, unit 243.
