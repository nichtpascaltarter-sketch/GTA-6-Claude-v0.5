// Automatic updates from the game's GitHub releases.
//
// After the window opens, a background thread reads the release list. A newer NeonTide exe is downloaded next to the
// running one (<exe>.download) and verified: HTTPS, the size GitHub lists, an x64 PE image, and the published SHA-256
// (GitHub's asset digest, a .sha256 / SHA256SUMS asset or the release notes; without one, the other checks). It is
// then kept as <exe>.pending with its record in <exe>.pending.json. The next start, before any window exists, checks
// it again and installs it: the running exe is renamed to <exe>.old (Windows allows that for a running program), the
// pending file takes its place and starts with the same arguments; the new version deletes <exe>.old. The main menu
// shows the download, "Restart now" once it is ready, or the release page when it could not be downloaded.
//
// Automated runs (--autotest, --benchmark, --shot ... see automatedRun) never update, nor do --no-update and
// Settings > Gameplay > Automatic Updates Off. Network and file errors only go to the log.
// Test hooks: --update-url <url> reads the releases from another URL (plain http only to this machine) and also
// checks during automated runs, never installing there; --update-test installs a pending update or else checks,
// downloads and verifies one, without a window, and exits (0 pending or installed, 1 no update, 2 not downloaded).
#pragma once

namespace UI {
struct MenuState;
struct MenuAction;
}

namespace Updater {
// First thing in WinMain: installs a verified download and starts it (true: return exitCode from WinMain), and runs
// --update-test. Opens the log when it does anything.
bool beforeStart(int& exitCode);
// After the window opens: the background check (logs why it is skipped)
void startCheck();
// Every menu frame, after Menus::update: fills the update notice for the next frame, follows Settings > Gameplay >
// Automatic Updates, opens the release page, and turns "Restart now" into MA_QUIT_GAME (the restart follows shutdown)
void menuFrame(UI::MenuState& menu, UI::MenuAction& act);
// The game loop ended: a running check stops and no longer logs. True when the player chose "Restart now".
bool stop();
// After shutdown, following "Restart now": installs the download and starts the game again
void restart();
}  // namespace Updater
