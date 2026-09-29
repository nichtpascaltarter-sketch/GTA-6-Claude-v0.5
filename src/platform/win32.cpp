// Win32 implementation of the platform layer.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <xinput.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <psapi.h>
#include <cstdarg>
#include "platform.h"

namespace Platform {
namespace {
HWND g_hwnd = nullptr;
InputState g_input;
int g_width = 1280, g_height = 720;
bool g_focus = true, g_resized = false, g_quit = false;
bool g_mouseCaptured = false, g_fullscreen = false;
WINDOWPLACEMENT g_prevPlacement = {sizeof(WINDOWPLACEMENT)};
LARGE_INTEGER g_freq, g_start;
std::vector<std::string> g_args;
std::string g_userDir;
FILE* g_logFile = nullptr;
std::mutex g_logMutex;

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);
PFN_XInputGetState pXInputGetState = nullptr;
PFN_XInputSetState pXInputSetState = nullptr;
double g_nextPadPoll = 0;
int g_padIndex = -1;

void loadXInput() {
    const char* names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (const char* n : names) {
        HMODULE m = LoadLibraryA(n);
        if (m) {
            pXInputGetState = (PFN_XInputGetState)(void*)GetProcAddress(m, "XInputGetState");
            pXInputSetState = (PFN_XInputSetState)(void*)GetProcAddress(m, "XInputSetState");
            if (pXInputGetState) return;
        }
    }
}

vec2 stickDeadzone(SHORT x, SHORT y, float dz) {
    vec2 v(x / 32767.f, y / 32767.f);
    float l = length(v);
    if (l < dz) return vec2(0, 0);
    float nl = Min(1.f, (l - dz) / (1.f - dz));
    return v * (nl / l);
}

void pollGamepad() {
    GamepadState& p = g_input.pad;
    p.prevButtons = p.buttons;
    if (!pXInputGetState) { p.connected = false; return; }
    double now = Platform::timeSeconds();
    if (g_padIndex < 0) {
        if (now < g_nextPadPoll) { p.connected = false; p.buttons = 0; return; }
        g_nextPadPoll = now + 1.5;  // polling disconnected pads is slow; throttle
        for (DWORD i = 0; i < 4; i++) {
            XINPUT_STATE s;
            if (pXInputGetState(i, &s) == ERROR_SUCCESS) { g_padIndex = (int)i; break; }
        }
        if (g_padIndex < 0) { p.connected = false; p.buttons = 0; return; }
    }
    XINPUT_STATE s;
    if (pXInputGetState((DWORD)g_padIndex, &s) != ERROR_SUCCESS) {
        g_padIndex = -1; p.connected = false; p.buttons = 0;
        p.leftStick = p.rightStick = vec2(0, 0); p.leftTrigger = p.rightTrigger = 0;
        return;
    }
    p.connected = true;
    p.buttons = s.Gamepad.wButtons;
    p.leftStick = stickDeadzone(s.Gamepad.sThumbLX, s.Gamepad.sThumbLY, 0.24f);
    p.rightStick = stickDeadzone(s.Gamepad.sThumbRX, s.Gamepad.sThumbRY, 0.20f);
    p.leftTrigger = Max(0.f, (s.Gamepad.bLeftTrigger - 30) / 225.f);
    p.rightTrigger = Max(0.f, (s.Gamepad.bRightTrigger - 30) / 225.f);
    if (p.buttons != 0 || length(p.leftStick) > 0.3f || length(p.rightStick) > 0.3f || p.leftTrigger > 0.3f || p.rightTrigger > 0.3f)
        g_input.lastInputWasPad = true;
}

void applyMouseCapture() {
    bool want = g_mouseCaptured && g_focus;
    if (want) {
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT tl = {r.left, r.top}, br = {r.right, r.bottom};
        ClientToScreen(g_hwnd, &tl);
        ClientToScreen(g_hwnd, &br);
        RECT clip = {tl.x, tl.y, br.x, br.y};
        ClipCursor(&clip);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE: g_quit = true; return 0;
        case WM_DESTROY: return 0;
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != g_width || h != g_height)) { g_width = w; g_height = h; g_resized = true; }
            if (g_mouseCaptured) applyMouseCapture();
            return 0;
        }
        case WM_ACTIVATE:
            g_focus = LOWORD(wp) != WA_INACTIVE;
            if (!g_focus) memset(g_input.keys, 0, sizeof(g_input.keys));
            applyMouseCapture();
            return 0;
        case WM_INPUT: {
            UINT size = 0;
            GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            if (size > 0 && size <= 256) {
                alignas(8) BYTE buf[256];
                if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size) {
                    RAWINPUT* ri = (RAWINPUT*)buf;
                    if (ri->header.dwType == RIM_TYPEMOUSE && !(ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        g_input.mouseDelta.x += (float)ri->data.mouse.lLastX;
                        g_input.mouseDelta.y += (float)ri->data.mouse.lLastY;
                        if (ri->data.mouse.lLastX != 0 || ri->data.mouse.lLastY != 0) g_input.lastInputWasPad = false;
                    }
                }
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int vk = (int)wp;
            if (vk == VK_SHIFT) vk = (int)MapVirtualKeyA((lp >> 16) & 0xff, MAPVK_VSC_TO_VK_EX);
            if (vk >= 0 && vk < KEY_COUNT) g_input.keys[vk] = true;
            if (vk == VK_LSHIFT || vk == VK_RSHIFT) g_input.keys[VK_SHIFT] = true;
            if (vk == VK_LCONTROL || vk == VK_RCONTROL || (int)wp == VK_CONTROL) g_input.keys[VK_CONTROL] = true;
            g_input.lastInputWasPad = false;
            if (msg == WM_SYSKEYDOWN && wp == VK_RETURN) { Platform::setFullscreen(!g_fullscreen); return 0; }
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) { g_quit = true; return 0; }
            if (msg == WM_SYSKEYDOWN) return 0;  // swallow Alt menu activation
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            int vk = (int)wp;
            if (vk == VK_SHIFT) {
                g_input.keys[VK_LSHIFT] = (GetKeyState(VK_LSHIFT) & 0x8000) != 0;
                g_input.keys[VK_RSHIFT] = (GetKeyState(VK_RSHIFT) & 0x8000) != 0;
                g_input.keys[VK_SHIFT] = g_input.keys[VK_LSHIFT] || g_input.keys[VK_RSHIFT];
            } else if (vk >= 0 && vk < KEY_COUNT) g_input.keys[vk] = false;
            if ((int)wp == VK_CONTROL) { g_input.keys[VK_LCONTROL] = g_input.keys[VK_RCONTROL] = false; }
            if (msg == WM_SYSKEYUP) return 0;
            break;
        }
        case WM_CHAR:
            if (wp >= 32 && wp < 127) g_input.textInput.push_back((char)wp);
            return 0;
        case WM_LBUTTONDOWN: g_input.keys[VK_LBUTTON] = true; g_input.lastInputWasPad = false; SetCapture(hwnd); return 0;
        case WM_LBUTTONUP: g_input.keys[VK_LBUTTON] = false; ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: g_input.keys[VK_RBUTTON] = true; g_input.lastInputWasPad = false; return 0;
        case WM_RBUTTONUP: g_input.keys[VK_RBUTTON] = false; return 0;
        case WM_MBUTTONDOWN: g_input.keys[VK_MBUTTON] = true; return 0;
        case WM_MBUTTONUP: g_input.keys[VK_MBUTTON] = false; return 0;
        case WM_XBUTTONDOWN: g_input.keys[GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2] = true; return TRUE;
        case WM_XBUTTONUP: g_input.keys[GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2] = false; return TRUE;
        case WM_MOUSEWHEEL: g_input.wheelDelta += (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; return 0;
        case WM_MOUSEMOVE: g_input.mousePos = vec2((float)(short)LOWORD(lp), (float)(short)HIWORD(lp)); break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                if (g_mouseCaptured && g_focus) SetCursor(nullptr);
                else SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    char buf[512];
    snprintf(buf, sizeof(buf), "Unhandled exception 0x%08lX at address %p", ep->ExceptionRecord->ExceptionCode,
             ep->ExceptionRecord->ExceptionAddress);
    LogPrintf("%s", buf);
    HMODULE base = GetModuleHandleA(nullptr);
    LogPrintf("Module base %p, offset 0x%llx", (void*)base,
              (unsigned long long)((char*)ep->ExceptionRecord->ExceptionAddress - (char*)base));
    if (g_logFile) fflush(g_logFile);
    ClipCursor(nullptr);
    MessageBoxA(nullptr, buf, "Neon Tide - crash", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace
}  // namespace Platform
using Platform::g_logFile;
using Platform::g_logMutex;
using Platform::g_freq;

void LogPrintf(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(g_logMutex);
    double t = g_freq.QuadPart ? Platform::timeSeconds() : 0.0;
    if (g_logFile) { fprintf(g_logFile, "[%8.3f] %s\n", t, buf); fflush(g_logFile); }
    fprintf(stdout, "[%8.3f] %s\n", t, buf);
    fflush(stdout);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

void FatalError(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LogPrintf("FATAL: %s", buf);
    ClipCursor(nullptr);
    if (!Platform::hasArg("autotest")) MessageBoxA(nullptr, buf, "Neon Tide - fatal error", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

std::string StrFormat(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

double TimeSeconds() { return Platform::timeSeconds(); }

namespace Platform {
void parseCommandLine() {
    if (!g_args.empty()) return;
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 0; i < argc; i++) {
        char tmp[1024];
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, tmp, sizeof(tmp), nullptr, nullptr);
        g_args.push_back(tmp);
    }
    LocalFree(argvW);
}

bool init(const char* title, int width, int height, bool fullscreen, bool hidden) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);
    SetUnhandledExceptionFilter(crashHandler);
    parseCommandLine();
    // User data dir + log
    std::string dir = userDataDir();
    g_logFile = fopen((dir + "log.txt").c_str(), "w");
    LOG("Neon Tide starting. Data dir: %s", dir.c_str());

    // DPI awareness (per-monitor v2 when available)
    HMODULE user32 = GetModuleHandleA("user32.dll");
    typedef BOOL(WINAPI * PFN_SetDpiCtx)(HANDLE);
    PFN_SetDpiCtx setDpiCtx = user32 ? (PFN_SetDpiCtx)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
    if (!setDpiCtx || !setDpiCtx((HANDLE)-4)) SetProcessDPIAware();

    WNDCLASSEXA wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = "NeonTideWindow";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExA(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT r = {0, 0, width, height};
    AdjustWindowRect(&r, style, FALSE);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    g_hwnd = CreateWindowExA(0, wc.lpszClassName, title, style, Max(0, (sw - ww) / 2), Max(0, (sh - wh) / 2), ww, wh,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return false;
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    g_width = Max(1, (int)(cr.right - cr.left));
    g_height = Max(1, (int)(cr.bottom - cr.top));
    if (!hidden) {
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
    }
    if (fullscreen) setFullscreen(true);

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;  // mouse
    rid.dwFlags = 0;
    rid.hwndTarget = g_hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    loadXInput();
    timeBeginPeriod(1);
    return true;
}

void shutdown() {
    setGamepadRumble(0, 0);
    ClipCursor(nullptr);
    timeEndPeriod(1);
    if (g_hwnd) DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    LOG("Shutdown complete.");
    if (g_logFile) fclose(g_logFile);
    g_logFile = nullptr;
}

bool pumpMessages() {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_quit = true;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    pollGamepad();
    return !g_quit;
}

void beginFrameInput() {
    memcpy(g_input.prevKeys, g_input.keys, sizeof(g_input.keys));
    g_input.mouseDelta = vec2(0, 0);
    g_input.wheelDelta = 0;
    g_input.textInput.clear();
}

InputState& input() { return g_input; }
void* windowHandle() { return g_hwnd; }
int clientWidth() { return g_width; }
int clientHeight() { return g_height; }
bool hasFocus() { return g_focus; }
bool wasResized() {
    bool r = g_resized;
    g_resized = false;
    return r;
}
void setMouseCaptured(bool c) {
    if (c == g_mouseCaptured) return;
    g_mouseCaptured = c;
    applyMouseCapture();
}

void setFullscreen(bool fs) {
    if (fs == g_fullscreen || !g_hwnd) return;
    g_fullscreen = fs;
    DWORD style = GetWindowLongA(g_hwnd, GWL_STYLE);
    if (fs) {
        MONITORINFO mi = {sizeof(mi)};
        GetWindowPlacement(g_hwnd, &g_prevPlacement);
        GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(g_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &g_prevPlacement);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    applyMouseCapture();
}
bool isFullscreen() { return g_fullscreen; }

void setGamepadRumble(float low, float high) {
    if (!pXInputSetState || g_padIndex < 0) return;
    XINPUT_VIBRATION v;
    v.wLeftMotorSpeed = (WORD)(Saturate(low) * 65535.f);
    v.wRightMotorSpeed = (WORD)(Saturate(high) * 65535.f);
    pXInputSetState((DWORD)g_padIndex, &v);
}

void setWindowTitle(const char* t) { SetWindowTextA(g_hwnd, t); }

std::string userDataDir() {
    if (!g_userDir.empty()) return g_userDir;
    char path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, path))) {
        g_userDir = std::string(path) + "\\NeonTide\\";
    } else {
        g_userDir = ".\\NeonTideData\\";
    }
    CreateDirectoryA(g_userDir.c_str(), nullptr);
    return g_userDir;
}

void showMessageBox(const char* title, const char* msg) { MessageBoxA(g_hwnd, msg, title, MB_OK); }

double timeSeconds() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - g_start.QuadPart) / (double)g_freq.QuadPart;
}
void sleepMs(int ms) { Sleep((DWORD)ms); }

void memoryUsageMB(float& workingSet, float& privateBytes) {
    // K32GetProcessMemoryInfo lives in kernel32 since Windows 7 (no psapi.lib needed)
    typedef BOOL(WINAPI * PFN)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    static PFN fn = (PFN)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
    workingSet = privateBytes = 0.f;
    if (!fn) return;
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    if (fn(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc))) {
        workingSet = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));
        privateBytes = (float)(pmc.PrivateUsage / (1024.0 * 1024.0));
    }
}
int argCount() { parseCommandLine(); return (int)g_args.size(); }
const char* arg(int i) { parseCommandLine(); return i >= 0 && i < (int)g_args.size() ? g_args[i].c_str() : nullptr; }
const char* argValue(const char* name) {
    parseCommandLine();
    std::string key = std::string("--") + name;
    for (size_t i = 0; i < g_args.size(); i++) {
        const std::string& a = g_args[i];
        if (a == key && i + 1 < g_args.size()) return g_args[i + 1].c_str();
        if (a.size() > key.size() && a.compare(0, key.size(), key) == 0 && a[key.size()] == '=') return a.c_str() + key.size() + 1;
    }
    return nullptr;
}
bool hasArg(const char* name) {
    parseCommandLine();
    std::string key = std::string("--") + name;
    for (auto& a : g_args)
        if (a == key || (a.size() > key.size() && a.compare(0, key.size(), key) == 0 && a[key.size()] == '=')) return true;
    return false;
}
}  // namespace Platform
