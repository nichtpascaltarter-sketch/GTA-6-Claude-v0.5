// OS abstraction: window, raw input, gamepad, paths, timing.
#pragma once
#include "../core/base.h"
#include "../core/math.h"

// Virtual key codes follow Win32 VK_* values so they can be used directly.
enum Key {
    KEY_BACK = 0x08, KEY_TAB = 0x09, KEY_ENTER = 0x0D, KEY_SHIFT = 0x10, KEY_CONTROL = 0x11, KEY_ALT = 0x12,
    KEY_ESCAPE = 0x1B, KEY_SPACE = 0x20, KEY_PGUP = 0x21, KEY_PGDN = 0x22, KEY_END = 0x23, KEY_HOME = 0x24,
    KEY_LEFT = 0x25, KEY_UP = 0x26, KEY_RIGHT = 0x27, KEY_DOWN = 0x28, KEY_DELETE = 0x2E,
    KEY_0 = 0x30, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A = 0x41, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_F1 = 0x70, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_LSHIFT = 0xA0, KEY_RSHIFT = 0xA1, KEY_LCONTROL = 0xA2, KEY_RCONTROL = 0xA3,
    KEY_MOUSE_LEFT = 0x01, KEY_MOUSE_RIGHT = 0x02, KEY_MOUSE_MIDDLE = 0x04, KEY_MOUSE_X1 = 0x05, KEY_MOUSE_X2 = 0x06,
    KEY_COUNT = 256
};

enum PadButton {
    PAD_UP = 0x0001, PAD_DOWN = 0x0002, PAD_LEFT = 0x0004, PAD_RIGHT = 0x0008,
    PAD_START = 0x0010, PAD_BACK = 0x0020, PAD_LTHUMB = 0x0040, PAD_RTHUMB = 0x0080,
    PAD_LB = 0x0100, PAD_RB = 0x0200, PAD_A = 0x1000, PAD_B = 0x2000, PAD_X = 0x4000, PAD_Y = 0x8000,
};

struct GamepadState {
    bool connected = false;
    u16 buttons = 0, prevButtons = 0;
    vec2 leftStick, rightStick;  // [-1,1], deadzone applied
    float leftTrigger = 0, rightTrigger = 0;
    bool down(u16 b) const { return (buttons & b) != 0; }
    bool pressed(u16 b) const { return (buttons & b) && !(prevButtons & b); }
    bool released(u16 b) const { return !(buttons & b) && (prevButtons & b); }
};

struct InputState {
    bool keys[KEY_COUNT] = {};
    bool prevKeys[KEY_COUNT] = {};
    vec2 mouseDelta;       // raw counts this frame
    float wheelDelta = 0;  // notches this frame
    vec2 mousePos;         // client pixels
    std::string textInput; // characters typed this frame
    GamepadState pad;
    bool lastInputWasPad = false;
    bool down(int k) const { return keys[k]; }
    bool pressed(int k) const { return keys[k] && !prevKeys[k]; }
    bool released(int k) const { return !keys[k] && prevKeys[k]; }
};

namespace Platform {
void parseCommandLine();
bool init(const char* title, int width, int height, bool fullscreen, bool hidden);
void shutdown();
// Pumps OS messages; returns false when the app should quit.
bool pumpMessages();
void beginFrameInput();  // snapshot prev state, clear deltas
InputState& input();
void* windowHandle();
int clientWidth();
int clientHeight();
bool hasFocus();
bool wasResized();  // true once after a size change
void setMouseCaptured(bool captured);
void setFullscreen(bool fs);
bool isFullscreen();
void setGamepadRumble(float low, float high);
void setWindowTitle(const char* t);
std::string userDataDir();  // created on demand, ends with separator
void showMessageBox(const char* title, const char* msg);
double timeSeconds();
void sleepMs(int ms);
// Process memory in MB: working set and private (committed) bytes.
void memoryUsageMB(float& workingSet, float& privateBytes);
int argCount();
const char* arg(int i);
// Returns value after "--name" or "--name=value" or nullptr.
const char* argValue(const char* name);
bool hasArg(const char* name);
}  // namespace Platform
