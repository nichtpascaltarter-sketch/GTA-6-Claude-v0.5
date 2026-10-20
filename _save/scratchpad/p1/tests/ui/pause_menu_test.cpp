// Runs the real menu state machine with drawing/world-data doubles: no GPU,
// game startup, user settings, or save-file writes. See build.bat in this folder.
#include "../../src/core/math.cpp"
#include "../../src/core/rng.h"
#include "../../src/ui/ui_internal.h"
#ifdef small
#undef small
#endif
#include "menu_test_stubs.cpp"
#include "../../src/ui/settings.cpp"
#include "../../src/ui/ui_common.cpp"
#include "../../src/ui/menus.cpp"

#include <cstdio>

static int checks = 0, failures = 0;
#define EXPECT(cond) do { ++checks; if (!(cond)) { \
    ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
} } while (0)

struct Harness {
    UI::MenuState menu;
    InputState input;
    float dt;
    explicit Harness(float step) : dt(step) { UI::Menus::reset(); }
    void buttons(int key = 0, u16 pad = 0) {
        std::memcpy(input.prevKeys, input.keys, sizeof(input.keys));
        std::memset(input.keys, 0, sizeof(input.keys));
        if (key) input.keys[key] = true;
        input.pad.prevButtons = input.pad.buttons;
        input.pad.buttons = pad;
        input.pad.connected = true;
        input.lastInputWasPad = pad != 0;
        input.mousePos = vec2(960.f, 540.f);
    }
    void open(UI::MenuScreen screen = UI::MENU_PAUSE) {
#ifdef NT_REPRO_BASELINE
        // The original App::openPause: no fresh-session notification or input consumption.
        menu.screen = screen;
        menu.cursor = menu.tab = 0;
#else
        UI::Menus::openPause(menu, screen);
#endif
    }
    UI::MenuAction tick() {
        UI::beginFrame(1920, 1080);
        // Match the app: it does NOT call Menus::update while playing with no menu.
        auto action = menu.screen == UI::MENU_NONE ? UI::MenuAction() : UI::Menus::update(menu, input, dt);
        UI::endFrame();
        return action;
    }
};

static void testEscapeCycles(float dt) {
    Harness h(dt);
    // Reproduce leaving the main menu without update ever seeing MENU_NONE.
    h.menu.screen = UI::MENU_MAIN;
    h.tick();
    h.menu.screen = UI::MENU_NONE;
    for (int cycle = 0; cycle < 20; ++cycle) {
        h.buttons();
        h.buttons(KEY_ESCAPE);
        EXPECT(h.input.pressed(KEY_ESCAPE));
        h.open();
        EXPECT(h.tick().type == UI::MA_NONE);
        EXPECT(h.menu.screen == UI::MENU_MAP);
        EXPECT(UI::menus_ui::I.root == UI::MENU_PAUSE);
        for (int hold = 0; hold < 4; ++hold) {
            h.buttons(KEY_ESCAPE);
            EXPECT(!h.input.pressed(KEY_ESCAPE));
            EXPECT(h.tick().type == UI::MA_NONE);
            EXPECT(h.menu.screen != UI::MENU_NONE);
        }
        h.buttons();
        EXPECT(h.tick().type == UI::MA_NONE);
        EXPECT(h.menu.screen != UI::MENU_NONE);
        h.buttons(KEY_ESCAPE);
        EXPECT(h.tick().type == UI::MA_RESUME);
        EXPECT(h.menu.screen == UI::MENU_NONE);
    }
}

static void testControllerCycles(float dt) {
    Harness h(dt);
    for (int cycle = 0; cycle < 20; ++cycle) {
        h.buttons();
        h.buttons(0, PAD_START);
        h.open();
        EXPECT(h.tick().type == UI::MA_NONE);
        EXPECT(h.menu.screen != UI::MENU_NONE);
        h.buttons(0, PAD_START);
        EXPECT(h.tick().type == UI::MA_NONE);
        EXPECT(h.menu.screen != UI::MENU_NONE);
        h.buttons();
        h.tick();
        // A second press must work even before the old 200 ms timeout.
        h.buttons(0, PAD_START);
        EXPECT(h.tick().type == UI::MA_RESUME);
        EXPECT(h.menu.screen == UI::MENU_NONE);
    }
}

static void testOpeningInput() {
    for (int key : {KEY_ESCAPE, KEY_ENTER, KEY_SPACE, KEY_Q, KEY_E, KEY_MOUSE_LEFT}) {
        Harness h(1.f / 60.f);
        h.buttons(key);
        h.input.wheelDelta = 1.f;
        h.open(UI::MENU_MAP);
        EXPECT(h.tick().type == UI::MA_NONE);
        EXPECT(h.menu.screen == UI::MENU_MAP);
        EXPECT(!h.menu.hasWaypoint);
        EXPECT(h.input.pressed(key)); // Filtering must not modify shared platform input.
    }
    Harness h(1.f / 60.f);
    h.buttons(0, PAD_A);
    h.open(UI::MENU_MAP); // phone's map app, opened by confirm
    EXPECT(h.tick().type == UI::MA_NONE);
    EXPECT(!h.menu.hasWaypoint);
    h.buttons(); h.tick();
    h.buttons(0, PAD_A);
    EXPECT(h.tick().type == UI::MA_SET_WAYPOINT); // new confirm still works
    EXPECT(h.menu.hasWaypoint);
    h.buttons(); h.tick();
    h.buttons(0, PAD_B);
    EXPECT(h.tick().type == UI::MA_RESUME);
}

static void testFreshSession() {
    Harness h(1.f / 60.f);
    h.menu.screen = UI::MENU_SETTINGS;
    h.menu.prevScreen = UI::MENU_MAIN;
    h.tick();
    UI::menus_ui::I.dialog = UI::menus_ui::DLG_QUIT_GAME;
    UI::menus_ui::I.bindCapture = true;
    h.menu.screen = UI::MENU_NONE; // no sentinel update before reopening
    h.buttons(KEY_ESCAPE);
    h.open(UI::MENU_SETTINGS);
    EXPECT(h.tick().type == UI::MA_NONE);
    EXPECT(UI::menus_ui::I.root == UI::MENU_PAUSE);
    EXPECT(UI::menus_ui::I.dialog == UI::menus_ui::DLG_NONE);
    EXPECT(!UI::menus_ui::I.bindCapture);
    EXPECT(h.menu.prevScreen == UI::MENU_NONE);
    EXPECT(h.menu.screen == UI::MENU_SETTINGS);
}

static void testSettingsBack() {
    Harness h(1.f / 60.f);
    h.buttons(KEY_ESCAPE);
    h.open(UI::MENU_SETTINGS);
    h.tick();
    h.buttons(); h.tick();
    h.buttons(KEY_ENTER); h.tick(); // enter the settings item list
    EXPECT(UI::menus_ui::I.setItemsFocus);
    h.buttons(); h.tick();
    h.buttons(KEY_ESCAPE);
    EXPECT(h.tick().type == UI::MA_NONE); // back to categories, NOT resume
    EXPECT(!UI::menus_ui::I.setItemsFocus);
    EXPECT(h.menu.screen == UI::MENU_SETTINGS);
    h.buttons(); h.tick();
    h.buttons(KEY_ESCAPE);
    EXPECT(h.tick().type == UI::MA_RESUME);
}

static void testDialogBack() {
    Harness h(1.f / 60.f);
    h.buttons(KEY_ESCAPE); h.open(); h.tick();
    h.buttons(); h.tick();
    h.buttons(KEY_Q); h.tick(); // wrap MAP to QUIT
    EXPECT(h.menu.tab == UI::menus_ui::PT_QUIT);
    h.buttons(); h.tick();
    h.buttons(KEY_ENTER); h.tick(); // confirm quit-to-main-menu prompt
    EXPECT(UI::menus_ui::I.dialog != UI::menus_ui::DLG_NONE);
    h.buttons(); h.tick();
    h.buttons(KEY_ESCAPE);
    EXPECT(h.tick().type == UI::MA_NONE);
    EXPECT(UI::menus_ui::I.dialog == UI::menus_ui::DLG_NONE);
    EXPECT(h.menu.screen != UI::MENU_NONE);
    h.buttons(); h.tick();
    h.buttons(KEY_ESCAPE);
    EXPECT(h.tick().type == UI::MA_RESUME);
}

int main() {
    for (float dt : {0.f, 1.f / 240.f, 1.f / 60.f, 1.f / 30.f, 0.1f}) {
        testEscapeCycles(dt);
        testControllerCycles(dt);
    }
    testOpeningInput();
    testFreshSession();
    testSettingsBack();
    testDialogBack();
    std::printf("Pause-menu regression: %d checks, %d failures.\n", checks, failures);
    return failures ? 1 : 0;
}
