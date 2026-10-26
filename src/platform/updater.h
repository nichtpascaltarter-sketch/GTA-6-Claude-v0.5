// Automatic updates, built from the source of GitHub's main branch (merged pull requests only).
//
// Every build carries the commit it was made from (NT_COMMIT, src/core/version.h). After the window opens, a
// background thread asks GitHub how that commit relates to main: only when main is ahead (it has commits this build
// lacks, and this build none that main lacks) is there an update. It then lists main's new commits (the pull requests
// they merged go to the log and the main menu), downloads main's source as an archive, unpacks it with Windows'
// tar.exe and runs its build.bat at below-normal priority in <data folder>\updates\<commit>\ (MSVC through
// vswhere, else MinGW-w64 g++ from PATH or a usual folder; build.log keeps the output). The exe it builds must be an
// x64 program that carries the new commit; it is kept next to the running one as <exe>.pending with its record in
// <exe>.pending.json. The next start, before any window exists, checks it again and installs it: the running exe is
// renamed to <exe>.old (Windows allows that for a running program), the pending file takes its place and starts with
// the same arguments; the new build deletes <exe>.old. The main menu shows each stage (checking, downloading,
// unpacking, building with its time), then "Restart now", or the failed build with its log, or what building needs
// when no compiler (or no tar.exe, or no writable game folder) is there. A build that failed is not repeated for the
// same commit and toolchain; a newer commit on main tries again. A build without a commit ("unknown") updates to
// main's newest once. A build that is not behind main (identical, ahead of it, on another branch, or a commit GitHub
// does not know) does not update.
//
// Automated runs (--autotest, --benchmark, --shot ... see automatedRun) never update, nor do --no-update and
// Settings > Gameplay > Automatic Updates Off. Network, file and build errors only go to the log and the notice.
// Test hooks: --update-url <url> asks another server instead of api.github.com and github.com (plain http only to this
// machine) and also checks during automated runs, never installing there; --update-test installs a pending update or
// else checks, downloads, builds and verifies one, without a window, and exits (0 pending or installed, 1 no update,
// 2 not built).
#pragma once
#include <string>
#include <vector>

namespace UI {
struct MenuState;
struct MenuAction;
}

namespace Updater {
// First thing in WinMain: installs a verified build and starts it (true: return exitCode from WinMain), and runs
// --update-test. Opens the log when it does anything.
bool beforeStart(int& exitCode);
// After the window opens: the background check (logs why it is skipped)
void startCheck();
// A test run (--autotest, --benchmark, --shot ...): no updates, and no first-start setup page
bool isAutomatedRun();
// The update this start installed, for the main menu's "Updated to": the install keeps the record as
// <exe>.updated.json, and the new build's first start reads it and deletes it, whatever it holds (automated runs leave
// it alone). It counts only when it names this build's commit and, when the old build started this one, the commit
// it came from. True once, with its commit's 7 digits and its pull requests as printable lines ("#56 A setup page on
// the first start"), newest first.
struct Installed {
    std::string commit;
    std::vector<std::string> pulls;
    int commits = 0;   // the commits it brought
};
bool takeInstalled(Installed& out);
// Every menu frame, after Menus::update: fills the update notice for the next frame, follows Settings > Gameplay >
// Automatic Updates, opens the build log or the help page, and turns "Restart now" into MA_QUIT_GAME (the restart
// follows shutdown)
void menuFrame(UI::MenuState& menu, UI::MenuAction& act);
// The game loop ended: a running check or build stops and no longer logs. True when the player chose "Restart now".
bool stop();
// After shutdown, following "Restart now": installs the build and starts the game again
void restart();
}  // namespace Updater
