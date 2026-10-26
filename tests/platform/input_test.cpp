// Tests for the window procedure's key and button state (src/platform/win32.cpp), through its real message pump: a
// press and its release that arrive within one frame's messages still show as a press in that frame, and as a release
// in the next one; held keys and clicks behave as before.
// Build with MinGW-w64 from the repo root, then run it under Wine (or on Windows); the exit code is the failure count:
//   x86_64-w64-mingw32-g++ -std=c++17 -O1 -Wall tests/platform/input_test.cpp -o build/input_test.exe -static -mwindows
//       -lxinput -lwinmm -lshell32 -lole32 -luuid -lpsapi                                         (all on one line)
//   wine64 build/input_test.exe --datadir=<a folder for its log>
#include "../../src/core/math.cpp"
#include "../../src/platform/win32.cpp"

static int gFails = 0;
static FILE* gOut = nullptr;

static void expect(bool ok, const char* what) {
    if (!ok) gFails++;
    if (gOut) fprintf(gOut, "%s: %s\n", ok ? "ok" : "FAIL", what);
}

// One frame: the input snapshot, then the messages posted before it
static void frame(HWND w, std::initializer_list<std::pair<UINT, WPARAM>> msgs) {
    Platform::beginFrameInput();
    for (const auto& m : msgs) {
        const LPARAM lp = m.first == WM_KEYUP ? (LPARAM)0xC0000001 : (LPARAM)1;
        PostMessageA(w, m.first, m.second, lp);
    }
    Platform::pumpMessages();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    Platform::parseCommandLine();
    if (!Platform::init("Neon Tide input test", 320, 240, false, true)) return 100;
    gOut = fopen((Platform::userDataDir() + "input_test.txt").c_str(), "w");
    HWND w = (HWND)Platform::windowHandle();
    const InputState& in = Platform::input();
    frame(w, {});
    // a tap within one frame: down and up in the same messages
    frame(w, {{WM_KEYDOWN, 'U'}, {WM_KEYUP, 'U'}});
    expect(in.pressed('U') && in.down('U'), "a tap is pressed in its frame");
    frame(w, {});
    expect(in.released('U') && !in.down('U'), "and released in the next");
    frame(w, {});
    expect(!in.down('U') && !in.released('U') && !in.pressed('U'), "and gone after that");
    // a key held across frames
    frame(w, {{WM_KEYDOWN, 'W'}});
    expect(in.pressed('W'), "a held key is pressed");
    frame(w, {{WM_KEYDOWN, 'W'}});   // (auto-repeat)
    expect(in.down('W') && !in.pressed('W'), "held, not pressed again");
    frame(w, {{WM_KEYUP, 'W'}});
    expect(in.released('W') && !in.down('W'), "released in the frame its key comes up");
    // down, up and down again in one frame: still held afterwards
    frame(w, {{WM_KEYDOWN, 'E'}, {WM_KEYUP, 'E'}, {WM_KEYDOWN, 'E'}});
    expect(in.pressed('E'), "a re-press is pressed");
    frame(w, {});
    expect(in.down('E') && !in.released('E'), "and still held in the next frame");
    frame(w, {{WM_KEYUP, 'E'}});
    expect(in.released('E'), "until it comes up");
    // a click within one frame
    frame(w, {{WM_LBUTTONDOWN, MK_LBUTTON}, {WM_LBUTTONUP, 0}});
    expect(in.pressed(VK_LBUTTON), "a quick click is pressed in its frame");
    frame(w, {});
    expect(in.released(VK_LBUTTON) && !in.down(VK_LBUTTON), "and released in the next");
    if (gOut) {
        fprintf(gOut, "input_test: %d failed\n", gFails);
        fclose(gOut);
    }
    Platform::shutdown();
    return gFails;
}
