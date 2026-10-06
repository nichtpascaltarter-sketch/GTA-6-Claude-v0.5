// Phone: the handset in the bottom-right corner. Built-in apps (Contacts, Messages, Tidegram, Camera with photo mode,
// Map, Quick Save) plus list apps supplied by the game; incoming / outgoing call screens; full-screen photo mode with a
// free camera and UI-side depth of field and grading (draw2d photoEffect). Immediate mode like the menus: the game
// owns PhoneState and calls Phone::update every gameplay frame.
#include "ui_internal.h"

namespace UI {
namespace phone_ui {

using namespace uix;

enum Screen : u8 {
    SC_HOME = 0, SC_CONTACTS, SC_MESSAGES, SC_THREAD, SC_TIDEGRAM, SC_CAMERA, SC_GALLERY, SC_MAP, SC_QUICKSAVE, SC_LIST,
    SC_CALL, SC_COUNT
};
enum BuiltinApp : int { APP_CONTACTS = 0, APP_MESSAGES, APP_TIDEGRAM, APP_CAMERA, APP_MAP, APP_QUICKSAVE, APP_BUILTIN_COUNT };

const char* const kFilterNames[] = {"Natural", "Neon Nights", "Golden Hour", "Noir", "Vintage 86", "Chrome", "Vapor", "Sepia",
                                    "Tropic", "Pixel"};
const int kFilterCount = (int)ARRAY_COUNT(kFilterNames);
const float kApertures[] = {1.4f, 2.f, 2.8f, 4.f, 5.6f, 8.f, 11.f, 16.f};
const char* const kFrameNames[] = {"None", "Polaroid", "Film Strip", "Neon"};

struct Nav {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false;
    bool pad = false;
};

struct Internal {
    float openAnim = 0.f;
    bool visible = false;
    bool wasOpen = false;
    bool justOpened = false;
    bool autoOpened = false;
    Screen screen = SC_HOME;
    float screenT = 10.f;
    int dir = 1;
    int homeCursor = 0;
    int cursor[SC_COUNT] = {};
    float scroll[SC_COUNT] = {};
    int threadIdx = -1;
    int threadButton = 0;
    int listApp = -1;
    int cameraButton = 1;
    int galleryOpen = -1;
    float mapZoom = 2.4f;
    int saveState = 0;          // 0 confirm, 1 waiting for the game, 2 done
    float saveT = 0.f;
    int saveChoice = 0;
    int callButton = 1;         // incoming: 0 decline, 1 answer
    PhoneCallState lastCall = CALL_NONE;
    float callEndT = 0.f;
    std::string callEndName;
    Screen callReturn = SC_HOME;
    std::string toast;
    float toastT = 0.f;
    int repeatKey = -1;
    float repeatT = 0.f;
    // camera + photo mode
    int quickFilter = 0;
    bool photoUi = true;
    int photoRow = 0;
    float photoScroll = 0.f;
    int captureStage = 0;       // 1: this frame shows the bare photo and is captured
    float flashT = 0.f;
    float savedT = 0.f;
    std::vector<int> gallery;   // snapshot ids, oldest first
    float tideNewT = 0.f;
    int tideSeenTop = -1;
    float time = 0.f;
    std::vector<std::pair<int, float>> postHeights;   // Tidegram card heights cache (post id, height)
    float postHeightW = 0.f;
};
Internal I;
bool g_phoneCovers = false;
bool g_photoModeOn = false;

// ------------------------------------------------------------------------------------------------------------------
// Helpers
TextStyle tstyle(FontId f, float size, u32 color, Align al = ALIGN_LEFT) {
    TextStyle t;
    t.font = f;
    t.size = size;
    t.color = color;
    t.align = al;
    return t;
}

std::string fitText(const std::string& s, const TextStyle& st, float maxW) {
    if (textWidth(s.c_str(), st) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && textWidth((t + "...").c_str(), st) > maxW) t.pop_back();
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t + "...";
}

u32 hashName(const std::string& s) {
    u32 h = 2166136261u;
    for (char c : s) h = (h ^ (u8)c) * 16777619u;
    return h;
}

const float kAvatarPal[8][6] = {
    {1.00f, 0.42f, 0.66f, 0.70f, 0.16f, 0.52f}, {1.00f, 0.68f, 0.30f, 0.88f, 0.34f, 0.14f}, {0.34f, 0.88f, 0.64f, 0.10f, 0.52f, 0.44f},
    {0.52f, 0.56f, 1.00f, 0.32f, 0.22f, 0.76f}, {0.30f, 0.82f, 1.00f, 0.10f, 0.44f, 0.86f}, {0.98f, 0.82f, 0.34f, 0.82f, 0.44f, 0.12f},
    {0.96f, 0.48f, 0.46f, 0.66f, 0.16f, 0.34f}, {0.64f, 0.92f, 0.40f, 0.22f, 0.62f, 0.32f},
};

void avatarColors(const std::string& name, u32 given, u32& top, u32& bottom) {
    if (given) {
        top = given;
        bottom = lerpColor(given, C(0.f, 0.f, 0.f), 0.35f);
        return;
    }
    int i = (int)(hashName(name) % 8u);
    top = C(kAvatarPal[i][0], kAvatarPal[i][1], kAvatarPal[i][2]);
    bottom = C(kAvatarPal[i][3], kAvatarPal[i][4], kAvatarPal[i][5]);
}

std::string initials(const std::string& name) {
    std::string o;
    bool take = true;
    for (char c : name) {
        if (c == ' ') { take = true; continue; }
        if (take && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            o.push_back(c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c);
            if (o.size() == 2) break;
        }
        take = false;
    }
    return o.empty() ? std::string("?") : o;
}

void drawAvatar(float cx, float cy, float r, const std::string& name, u32 top, u32 bottom, float a) {
    roundRectGradient(cx - r, cy - r, r * 2.f, r * 2.f, r, withAlpha(top, a), withAlpha(bottom, a));
    circleSoft(cx - r * 0.25f, cy - r * 0.45f, r * 0.35f, r * 0.7f, withAlpha(kWhite, 0.14f * a));
    std::string ini = initials(name);
    TextStyle t = tstyle(FONT_HEADING, r * (ini.size() > 1 ? 0.86f : 1.f), withAlpha(kWhite, a), ALIGN_CENTER);
    t.shadow = Max(1.f, r * 0.05f);
    text(cx, cy - t.size * 0.56f, ini.c_str(), t);
}

std::string clockText(float hours) {
    int m = (int)(fmodf(Max(hours, 0.f), 24.f) * 60.f);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
    return b;
}

std::string agoText(double seconds) {
    if (seconds < 50.0) return "now";
    if (seconds < 3600.0) return std::to_string((int)(seconds / 60.0 + 0.5)) + "m";
    if (seconds < 86400.0) return std::to_string((int)(seconds / 3600.0)) + "h";
    return std::to_string((int)(seconds / 86400.0)) + "d";
}

std::string countText(int n) {
    if (n < 1000) return std::to_string(n);
    char b[24];
    if (n < 10000) snprintf(b, sizeof(b), "%.1fK", n / 1000.f);
    else if (n < 1000000) snprintf(b, sizeof(b), "%dK", n / 1000);
    else snprintf(b, sizeof(b), "%.1fM", n / 1000000.f);
    return b;
}

Nav readNav(const InputState& in, float dt, bool wheelScrolls = true) {
    Nav n;
    const GamepadState& p = in.pad;
    n.pad = in.lastInputWasPad && p.connected;
    const int keys[4] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT};
    const u16 btns[4] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT};
    bool* outs[4] = {&n.up, &n.down, &n.left, &n.right};
    bool anyHeld = false;
    for (int k = 0; k < 4; k++) {
        bool pr = in.pressed(keys[k]) || p.pressed(btns[k]);
        bool dn = in.down(keys[k]) || p.down(btns[k]);
        if (pr) {
            *outs[k] = true;
            I.repeatKey = k;
            I.repeatT = 0.38f;
        } else if (dn && I.repeatKey == k) {
            I.repeatT -= dt;
            if (I.repeatT <= 0.f) {
                *outs[k] = true;
                I.repeatT = 0.075f;
            }
        }
        anyHeld |= dn;
    }
    if (!anyHeld) I.repeatKey = -1;
    n.confirm = in.pressed(KEY_ENTER) || p.pressed(PAD_A);
    n.back = in.pressed(KEY_BACK) || in.pressed(KEY_MOUSE_RIGHT) || p.pressed(PAD_B);
    if (wheelScrolls && in.wheelDelta > 0.5f) n.up = true;
    if (wheelScrolls && in.wheelDelta < -0.5f) n.down = true;
    return n;
}

void go(Screen s, int dir) {
    I.screen = s;
    I.screenT = 0.f;
    I.dir = dir;
}

void showToast(const std::string& t) {
    I.toast = t;
    I.toastT = 2.6f;
}

// ------------------------------------------------------------------------------------------------------------------
// Phone geometry
struct PL {
    float s;
    float x, y, w, h, r;          // body
    float sx, sy, sw, sh, sr;     // screen
    float top, bottom;            // content area (below the status bar, above the home indicator)
};

PL phoneLayout(const Layout& L, float slide) {
    PL p;
    p.s = L.s;
    p.w = 318.f * L.s;
    p.h = 640.f * L.s;
    p.r = 46.f * L.s;
    p.x = L.right - p.w;
    p.y = L.bottom - p.h + (1.f - slide) * (p.h + 90.f * L.s);
    float inset = 10.f * L.s;
    p.sx = p.x + inset;
    p.sy = p.y + inset;
    p.sw = p.w - inset * 2.f;
    p.sh = p.h - inset * 2.f;
    p.sr = p.r - inset;
    p.top = p.sy + 38.f * L.s;
    p.bottom = p.sy + p.sh - 24.f * L.s;
    return p;
}

// Synthwave wallpaper: gradient sky, striped sun, horizon grid and palm silhouettes
void drawWallpaper(const PL& p, float a, float t) {
    float s = p.s;
    float hz = p.sy + p.sh * 0.60f;
    gradientRect(p.sx, p.sy, p.sw, hz - p.sy, withAlpha(C(0.07f, 0.04f, 0.20f), a), withAlpha(C(0.62f, 0.16f, 0.46f), a));
    gradientRect(p.sx, hz - p.sh * 0.12f, p.sw, p.sh * 0.12f, withAlpha(C(1.f, 0.40f, 0.42f), 0.f), withAlpha(C(1.f, 0.50f, 0.40f), 0.55f * a));
    // stars
    for (int i = 0; i < 26; i++) {
        float fx = fmodf(i * 0.618034f + 0.13f, 1.f), fy = fmodf(i * 0.414214f + 0.07f, 1.f);
        float tw = 0.45f + 0.35f * sinf(t * 1.3f + i * 2.1f);
        circle(p.sx + fx * p.sw, p.sy + 40.f * s + fy * (hz - p.sy) * 0.55f, (0.8f + (i % 3) * 0.35f) * s, withAlpha(kWhite, tw * a));
    }
    // sun with stripes
    float sr = p.sw * 0.30f, scx = p.sx + p.sw * 0.5f, scy = hz - sr * 0.28f;
    circleSoft(scx, scy, sr * 1.25f, sr * 1.2f, withAlpha(C(1.f, 0.45f, 0.55f), 0.30f * a));
    ClipState saved = getClip();
    roundRectGradient(scx - sr, scy - sr, sr * 2.f, sr * 2.f, sr, withAlpha(C(1.f, 0.88f, 0.42f), a), withAlpha(C(1.f, 0.32f, 0.52f), a));
    for (int k = 0; k < 5; k++) {
        float yy = scy + sr * (0.08f + k * 0.17f);
        float hh = sr * (0.03f + k * 0.018f);
        rect(scx - sr, yy, sr * 2.f, hh, withAlpha(C(0.36f, 0.10f, 0.40f), a));
    }
    setClip(saved);
    // sea / grid below the horizon
    gradientRect(p.sx, hz, p.sw, p.sy + p.sh - hz, withAlpha(C(0.16f, 0.04f, 0.26f), a), withAlpha(C(0.03f, 0.02f, 0.10f), a));
    for (int k = 1; k < 9; k++) {
        float f = (float)k / 9.f;
        float yy = hz + (p.sy + p.sh - hz) * f * f;
        rect(p.sx, yy, p.sw, Max(1.f, 1.2f * s), withAlpha(C(1.f, 0.30f, 0.80f), (0.18f + 0.3f * f) * a));
    }
    for (int k = -6; k <= 6; k++) {
        vec2 a0(scx + k * 8.f * s, hz), a1(scx + k * 60.f * s, p.sy + p.sh);
        capsule(a0.x, a0.y, a1.x, a1.y, 1.1f * s, withAlpha(C(1.f, 0.30f, 0.80f), 0.22f * a));
    }
    rect(p.sx, hz - 1.f * s, p.sw, 2.f * s, withAlpha(C(1.f, 0.62f, 0.66f), 0.7f * a));
    // palms
    drawIcon(ICO_PALM, p.sx + p.sw * 0.14f, hz - 36.f * s, 120.f * s, withAlpha(C(0.05f, 0.02f, 0.10f), a));
    drawIcon(ICO_PALM, p.sx + p.sw * 0.90f, hz - 22.f * s, 90.f * s, withAlpha(C(0.05f, 0.02f, 0.10f), a), 0.f, 0, -0.2f);
}

void drawStatusBar(const PhoneState& st, const PL& p, float a, bool darkBg) {
    float s = p.s;
    u32 fg = withAlpha(darkBg ? kWhite : C(0.06f, 0.07f, 0.12f), a);
    TextStyle ts = tstyle(FONT_BODY, 15.f * s, fg);
    std::string clock = clockText(st.timeOfDay);
    text(p.sx + 26.f * s, p.sy + 10.f * s, clock.c_str(), ts);
    // punch-hole camera
    circle(p.sx + p.sw * 0.5f, p.sy + 18.f * s, 6.f * s, withAlpha(C(0.01f, 0.01f, 0.02f), a));
    circle(p.sx + p.sw * 0.5f - 1.6f * s, p.sy + 16.4f * s, 1.6f * s, withAlpha(C(0.3f, 0.35f, 0.6f), 0.7f * a));
    // signal bars, network, battery
    float rx = p.sx + p.sw - 24.f * s;
    float bw = 24.f * s, bh = 11.f * s, by = p.sy + 13.f * s;
    roundRect(rx - bw, by, bw, bh, 3.f * s, 0, 1.2f * s, withAlpha(fg, 0.9f));
    rect(rx, by + 3.5f * s, 2.f * s, 4.f * s, withAlpha(fg, 0.9f));
    float lvl = Saturate(st.battery);
    u32 bc = lvl < 0.2f ? withAlpha(kRed, a) : fg;
    roundRect(rx - bw + 2.2f * s, by + 2.2f * s, (bw - 4.4f * s) * lvl, bh - 4.4f * s, 1.5f * s, bc);
    float gx = rx - bw - 38.f * s;
    TextStyle ns = tstyle(FONT_HEADING, 12.f * s, fg);
    text(gx - 20.f * s, by - 0.5f * s, "5G", ns);
    for (int k = 0; k < 4; k++) {
        float hh = (4.f + k * 2.6f) * s;
        u32 c = k < st.signal ? fg : withAlpha(fg, 0.3f);
        roundRect(gx + k * 5.f * s, by + bh - hh, 3.2f * s, hh, 1.f * s, c);
    }
}

// App header: back chevron, title, optional subtitle and right-hand text
void drawHeader(const PL& p, const char* title, const std::string& sub, float a, u32 accent) {
    float s = p.s;
    float y = p.top;
    drawIcon(ICO_CHEVRON, p.sx + 22.f * s, y + 20.f * s, 22.f * s, withAlpha(accent, a), 0.f, 0, kPi);
    TextStyle ts = tstyle(FONT_HEADING, 25.f * s, withAlpha(kWhite, a));
    ts.tracking = 0.02f;
    text(p.sx + 38.f * s, y + 5.f * s, title, ts);
    if (!sub.empty()) {
        TextStyle ss = tstyle(FONT_BODY, 12.5f * s, withAlpha(kTextDim, a));
        std::string f = fitText(sub, ss, p.sw - 60.f * s);
        text(p.sx + 39.f * s, y + 33.f * s, f.c_str(), ss);
    }
    rect(p.sx + 16.f * s, y + (sub.empty() ? 40.f : 54.f) * s, p.sw - 32.f * s, 1.f, withAlpha(kWhite, 0.08f * a));
}

float headerHeight(const std::string& sub) { return sub.empty() ? 46.f : 60.f; }

// Keeps `cursor` inside the scrolled viewport; returns eased scroll offset (pixels)
float listScroll(int screen, float rowTop, float rowH, float viewH, float dt) {
    float target = I.scroll[screen];
    float curTop = rowTop, curBottom = rowTop + rowH;
    if (curTop - target < 0.f) target = curTop;
    if (curBottom - target > viewH) target = curBottom - viewH;
    target = Max(target, 0.f);
    I.scroll[screen] = target;
    return target;
}

struct ScrollAnim {
    float shown[SC_COUNT] = {};
};
ScrollAnim g_scrollShown;

float easedScroll(int screen, float dt) {
    float& v = g_scrollShown.shown[screen];
    v = approachExp(v, I.scroll[screen], 16.f, dt);
    if (fabsf(v - I.scroll[screen]) < 0.3f) v = I.scroll[screen];
    return v;
}

// ------------------------------------------------------------------------------------------------------------------
// Home screen
struct Tile {
    std::string name;
    int icon;
    u32 top, bottom, glyph;
    int badge;
    bool enabled;
};

void appTiles(const PhoneState& st, std::vector<Tile>& out) {
    out.clear();
    int unread = 0;
    for (const PhoneMessage& m : st.messages) unread += m.unread;
    int missions = 0;
    for (const PhoneContact& c : st.contacts) missions += c.mission && c.enabled;
    out.push_back({"Contacts", ICO_CONTACTS, C(1.f, 0.64f, 0.28f), C(0.90f, 0.34f, 0.16f), kWhite, missions, true});
    out.push_back({"Messages", ICO_MESSAGE, C(0.38f, 0.90f, 0.48f), C(0.10f, 0.60f, 0.30f), kWhite, unread, true});
    out.push_back({"Tidegram", ICO_TIDE, C(0.26f, 0.86f, 1.f), C(0.60f, 0.22f, 0.94f), kWhite, Min(tidegramUnread(), 99), true});
    out.push_back({"Camera", ICO_CAMERA, C(0.40f, 0.42f, 0.50f), C(0.14f, 0.15f, 0.20f), kWhite, 0, true});
    out.push_back({"Maps", ICO_MAP, C(0.97f, 0.97f, 1.f), C(0.74f, 0.80f, 0.90f), C(0.16f, 0.46f, 0.96f), 0, true});
    out.push_back({"Quick Save", ICO_SAVE, C(0.40f, 0.60f, 1.f), C(0.16f, 0.28f, 0.82f), kWhite, 0, true});
    static const float pal[6][6] = {{1.f, 0.36f, 0.62f, 0.72f, 0.12f, 0.46f}, {1.f, 0.80f, 0.30f, 0.86f, 0.46f, 0.08f},
                                    {0.62f, 0.44f, 1.f, 0.34f, 0.18f, 0.74f}, {0.22f, 0.86f, 0.80f, 0.06f, 0.50f, 0.52f},
                                    {1.f, 0.44f, 0.36f, 0.76f, 0.14f, 0.16f}, {0.46f, 0.74f, 1.f, 0.14f, 0.38f, 0.84f}};
    static const int glyphIcon[PG_COUNT] = {ICO_STAR, ICO_BLIP0 + BLIP_CAR_SHOP, ICO_BLIP0 + BLIP_SAFEHOUSE, ICO_CASE, ICO_REPLAY,
                                            ICO_SWITCH, ICO_DOLLAR, ICO_TROPHY, ICO_BLIP0 + BLIP_BOAT, ICO_BLIP0 + BLIP_PLANE,
                                            ICO_MUSIC, ICO_GEAR, ICO_MAP, ICO_USER};
    for (const PhoneListApp& app : st.apps) {
        Tile t;
        t.name = app.name;
        t.icon = glyphIcon[Clamp((int)app.glyph, 0, (int)PG_COUNT - 1)];
        int pi = (int)((u32)app.id % 6u);
        if (app.color) {
            t.top = app.color;
            t.bottom = lerpColor(app.color, C(0.f, 0.f, 0.f), 0.35f);
        } else {
            t.top = C(pal[pi][0], pal[pi][1], pal[pi][2]);
            t.bottom = C(pal[pi][3], pal[pi][4], pal[pi][5]);
        }
        t.glyph = kWhite;
        t.badge = app.badge;
        t.enabled = app.enabled;
        out.push_back(t);
    }
}

void drawTile(const Tile& t, float cx, float cy, float size, bool sel, float a, float s, float time) {
    float sc = sel ? 1.08f + 0.02f * sinf(time * 4.f) : 1.f;
    float sz = size * sc, r = sz * 0.27f;
    float x = cx - sz * 0.5f, y = cy - sz * 0.5f;
    float ea = t.enabled ? a : a * 0.45f;
    if (sel) circleSoft(cx, cy, sz * 0.62f, sz * 0.5f, withAlpha(kPink, 0.45f * a));
    roundRect(x, y + 3.f * s, sz, sz, r, withAlpha(C(0.f, 0.f, 0.02f), 0.35f * ea));
    roundRectGradient(x, y, sz, sz, r, withAlpha(t.top, ea), withAlpha(t.bottom, ea));
    gradientRect(x + r * 0.5f, y + 1.5f * s, sz - r, sz * 0.36f, withAlpha(kWhite, 0.20f * ea), withAlpha(kWhite, 0.f));
    drawIconGlow(t.icon, cx, cy + 1.f * s, sz * 0.62f, withAlpha(C(0.f, 0.f, 0.f), 0.25f * ea), 2.f * s);
    drawIcon(t.icon, cx, cy, sz * 0.62f, withAlpha(t.glyph, ea));
    if (sel) roundRect(x - 3.f * s, y - 3.f * s, sz + 6.f * s, sz + 6.f * s, r + 3.f * s, 0, 2.2f * s, withAlpha(kWhite, 0.95f * a));
    if (t.badge > 0) {
        std::string b = t.badge > 99 ? "99+" : std::to_string(t.badge);
        TextStyle bs = tstyle(FONT_HEADING, 13.f * s, withAlpha(kWhite, a), ALIGN_CENTER);
        float bw = Max(20.f * s, textWidth(b.c_str(), bs) + 10.f * s);
        float bx = x + sz - bw * 0.6f, by = y - 7.f * s;
        roundRect(bx, by, bw, 20.f * s, 10.f * s, withAlpha(C(1.f, 0.20f, 0.30f), a), 1.5f * s, withAlpha(C(0.05f, 0.03f, 0.10f), a));
        text(bx + bw * 0.5f, by + 10.f * s - bs.size * 0.55f, b.c_str(), bs);
    }
}

void activateTile(PhoneState& st, int idx, PhoneAction& act) {
    if (idx < APP_BUILTIN_COUNT) {
        switch (idx) {
            case APP_CONTACTS: go(SC_CONTACTS, 1); break;
            case APP_MESSAGES: go(SC_MESSAGES, 1); break;
            case APP_TIDEGRAM:
                go(SC_TIDEGRAM, 1);
                I.cursor[SC_TIDEGRAM] = 0;
                I.scroll[SC_TIDEGRAM] = 0.f;
                tideMarkSeen();
                break;
            case APP_CAMERA: go(SC_CAMERA, 1); I.cameraButton = 1; break;
            case APP_MAP: go(SC_MAP, 1); break;
            case APP_QUICKSAVE:
                go(SC_QUICKSAVE, 1);
                I.saveState = 0;
                I.saveChoice = 0;
                break;
            default: break;
        }
        return;
    }
    int ai = idx - APP_BUILTIN_COUNT;
    if (ai < 0 || ai >= (int)st.apps.size()) return;
    const PhoneListApp& app = st.apps[ai];
    if (!app.enabled) return;
    if (app.action) {
        act.type = PA_APP_ACTION;
        act.app = app.id;
        return;
    }
    I.listApp = ai;
    I.cursor[SC_LIST] = 0;
    I.scroll[SC_LIST] = 0.f;
    g_scrollShown.shown[SC_LIST] = 0.f;
    go(SC_LIST, 1);
}

void homeScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    drawWallpaper(p, a, I.time);
    std::vector<Tile> tiles;
    appTiles(st, tiles);
    int count = (int)tiles.size();
    int& cur = I.homeCursor;
    cur = Clamp(cur, 0, Max(count - 1, 0));
    if (n.left) cur = (cur + count - 1) % count;
    if (n.right) cur = (cur + 1) % count;
    if (n.up && cur >= 3) cur -= 3;
    if (n.down && cur + 3 < count) cur += 3;
    else if (n.down && cur / 3 < (count - 1) / 3) cur = count - 1;
    // clock widget
    float gridTop = p.top + 128.f * s, rowH = 102.f * s;
    int rows = (count + 2) / 3;
    float viewH = p.bottom - gridTop - 4.f * s;
    int curRow = cur / 3;
    float want = Max(0.f, (curRow + 1) * rowH - viewH);
    I.scroll[SC_HOME] = Min(want, Max(0.f, rows * rowH - viewH));
    float off = easedScroll(SC_HOME, dt);
    TextStyle cs = tstyle(FONT_HEADING, 66.f * s, withAlpha(kWhite, a), ALIGN_CENTER);
    cs.shadow = 2.f * s;
    cs.shadowSoft = 0.6f;
    std::string clock = clockText(st.timeOfDay);
    text(p.sx + p.sw * 0.5f, p.top + 8.f * s - off, clock.c_str(), cs);
    TextStyle ds = tstyle(FONT_BODY, 14.f * s, withAlpha(C(1.f, 0.86f, 0.92f), a), ALIGN_CENTER);
    ds.shadow = 1.f * s;
    std::string date = "Day " + std::to_string(st.day) + "  |  Porto Sol";
    text(p.sx + p.sw * 0.5f, p.top + 82.f * s - off, date.c_str(), ds);
    // app grid over a soft dark veil (labels stay readable over the bright sun)
    gradientRect(p.sx, gridTop - 20.f * s - off, p.sw, 80.f * s, withAlpha(C(0.02f, 0.01f, 0.06f), 0.f), withAlpha(C(0.02f, 0.01f, 0.06f), 0.32f * a));
    rect(p.sx, gridTop + 60.f * s - off, p.sw, Max(0.f, p.sy + p.sh - (gridTop + 60.f * s - off)), withAlpha(C(0.02f, 0.01f, 0.06f), 0.32f * a));
    float colW = (p.sw - 24.f * s) / 3.f;
    for (int i = 0; i < count; i++) {
        float cx = p.sx + 12.f * s + colW * (i % 3) + colW * 0.5f;
        float cy = gridTop + rowH * (i / 3) + 34.f * s - off;
        if (cy < p.top - 40.f * s || cy > p.bottom + 40.f * s) continue;
        bool sel = i == cur;
        drawTile(tiles[i], cx, cy, 60.f * s, sel, a, s, I.time);
        TextStyle ls = tstyle(FONT_BODY, 13.f * s, withAlpha(sel ? kWhite : C(0.94f, 0.92f, 0.99f), (sel ? 1.f : 0.9f) * a), ALIGN_CENTER);
        ls.outline = 1.2f * s;
        ls.outlineColor = withAlpha(C(0.04f, 0.02f, 0.10f), 0.8f * a);
        std::string nm = fitText(tiles[i].name, ls, colW - 6.f * s);
        text(cx, cy + 38.f * s, nm.c_str(), ls);
    }
    if (n.confirm && count > 0) activateTile(st, cur, act);
}

// ------------------------------------------------------------------------------------------------------------------
// Generic list row
void rowHighlight(const PL& p, float y, float h, float a) {
    float s = p.s;
    roundRect(p.sx + 8.f * s, y + 2.f * s, p.sw - 16.f * s, h - 4.f * s, 12.f * s, withAlpha(kPink, 0.22f * a), 1.2f * s,
              withAlpha(kPink, 0.75f * a));
}

void appBackground(const PL& p, float a) {
    gradientRect(p.sx, p.sy, p.sw, p.sh, withAlpha(C(0.07f, 0.08f, 0.16f), a), withAlpha(C(0.03f, 0.03f, 0.08f), a));
}

// Contacts
void contactsScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    appBackground(p, a);
    std::string sub = std::to_string(st.contacts.size()) + (st.contacts.size() == 1 ? " contact" : " contacts");
    drawHeader(p, "Contacts", sub, a, kOrange);
    int count = (int)st.contacts.size();
    int& cur = I.cursor[SC_CONTACTS];
    if (count == 0) {
        TextStyle es = tstyle(FONT_BODY, 15.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(p.sx + p.sw * 0.5f, p.top + 140.f * s, "No contacts yet.", es);
        return;
    }
    cur = Clamp(cur, 0, count - 1);
    if (n.up) cur = (cur + count - 1) % count;
    if (n.down) cur = (cur + 1) % count;
    float listTop = p.top + headerHeight(sub) * s + 4.f * s, rowH = 64.f * s;
    float viewH = p.bottom - listTop;
    listScroll(SC_CONTACTS, cur * rowH, rowH, viewH, dt);
    float off = easedScroll(SC_CONTACTS, dt);
    ClipState saved = getClip();
    setClipRect(p.sx, listTop, p.sw, viewH);
    for (int i = 0; i < count; i++) {
        float y = listTop + i * rowH - off;
        if (y + rowH < listTop || y > p.bottom) continue;
        const PhoneContact& c = st.contacts[i];
        float ea = c.enabled ? a : a * 0.45f;
        if (i == cur) rowHighlight(p, y, rowH, a);
        u32 top, bottom;
        avatarColors(c.name, c.color, top, bottom);
        drawAvatar(p.sx + 40.f * s, y + rowH * 0.5f, 21.f * s, c.name, top, bottom, ea);
        TextStyle ns = tstyle(FONT_BODY, 17.f * s, withAlpha(kWhite, ea));
        float textX = p.sx + 72.f * s, maxW = p.sw - 72.f * s - (c.mission ? 64.f : 38.f) * s;
        std::string nm = fitText(c.name, ns, maxW);
        float ny = c.subtitle.empty() ? y + rowH * 0.5f - 10.f * s : y + 12.f * s;
        text(textX, ny, nm.c_str(), ns);
        if (!c.subtitle.empty()) {
            TextStyle ss = tstyle(FONT_BODY, 12.5f * s, withAlpha(c.mission ? C(1.f, 0.78f, 0.36f) : kTextDim, ea));
            std::string sb = fitText(c.subtitle, ss, maxW);
            text(textX, y + 35.f * s, sb.c_str(), ss);
        }
        float rx = p.sx + p.sw - 22.f * s;
        if (c.mission) {
            TextStyle js = tstyle(FONT_HEADING, 12.f * s, withAlpha(C(0.10f, 0.05f, 0.02f), ea), ALIGN_CENTER);
            js.tracking = 0.08f;
            float jw = 40.f * s;
            roundRectGradient(rx - jw, y + rowH * 0.5f - 10.f * s, jw, 20.f * s, 10.f * s, withAlpha(C(1.f, 0.84f, 0.36f), ea),
                              withAlpha(C(1.f, 0.60f, 0.18f), ea));
            text(rx - jw * 0.5f, y + rowH * 0.5f - js.size * 0.55f, "JOB", js);
        } else {
            drawIcon(ICO_HANGUP, rx - 10.f * s, y + rowH * 0.5f, 22.f * s, withAlpha(C(0.40f, 0.90f, 0.50f), 0.9f * ea), 0.f, 0, -2.3f);
        }
        if (i + 1 < count) rect(textX, y + rowH - 1.f, p.sx + p.sw - textX - 14.f * s, 1.f, withAlpha(kWhite, 0.06f * a));
    }
    setClip(saved);
    if (n.confirm) {
        const PhoneContact& c = st.contacts[cur];
        if (c.enabled && st.call == CALL_NONE) {
            act.type = PA_CALL;
            act.id = c.id;
            st.call = CALL_OUTGOING;
            st.callName = c.name;
            st.callContactId = c.id;
            st.callSeconds = 0.f;
        }
    }
}

// Messages list
void messagesScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    appBackground(p, a);
    int unread = 0;
    for (const PhoneMessage& m : st.messages) unread += m.unread;
    std::string sub = unread ? std::to_string(unread) + " unread" : std::string("All caught up");
    drawHeader(p, "Messages", sub, a, kGreen);
    int count = (int)st.messages.size();
    int& cur = I.cursor[SC_MESSAGES];
    if (count == 0) {
        TextStyle es = tstyle(FONT_BODY, 15.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(p.sx + p.sw * 0.5f, p.top + 140.f * s, "No messages.", es);
        return;
    }
    cur = Clamp(cur, 0, count - 1);
    if (n.up) cur = (cur + count - 1) % count;
    if (n.down) cur = (cur + 1) % count;
    float listTop = p.top + headerHeight(sub) * s + 4.f * s, rowH = 70.f * s;
    float viewH = p.bottom - listTop;
    listScroll(SC_MESSAGES, cur * rowH, rowH, viewH, dt);
    float off = easedScroll(SC_MESSAGES, dt);
    ClipState saved = getClip();
    setClipRect(p.sx, listTop, p.sw, viewH);
    for (int i = 0; i < count; i++) {
        // newest first
        const PhoneMessage& m = st.messages[count - 1 - i];
        float y = listTop + i * rowH - off;
        if (y + rowH < listTop || y > p.bottom) continue;
        if (i == cur) rowHighlight(p, y, rowH, a);
        u32 top, bottom;
        avatarColors(m.from, 0, top, bottom);
        drawAvatar(p.sx + 40.f * s, y + rowH * 0.5f, 21.f * s, m.from, top, bottom, a);
        float textX = p.sx + 72.f * s;
        TextStyle ts = tstyle(FONT_BODY, 12.f * s, withAlpha(kTextDim, a), ALIGN_RIGHT);
        text(p.sx + p.sw - 20.f * s, y + 13.f * s, m.time.c_str(), ts);
        float tw = textWidth(m.time.c_str(), ts);
        TextStyle ns = tstyle(FONT_BODY, 16.f * s, withAlpha(kWhite, a));
        std::string nm = fitText(m.from, ns, p.sx + p.sw - 28.f * s - tw - textX - (m.mission ? 44.f * s : 0.f));
        float nw = text(textX, y + 10.f * s, nm.c_str(), ns);
        if (m.mission) {
            TextStyle js = tstyle(FONT_HEADING, 10.5f * s, withAlpha(C(0.10f, 0.05f, 0.02f), a), ALIGN_CENTER);
            roundRect(textX + nw + 6.f * s, y + 12.f * s, 34.f * s, 16.f * s, 8.f * s, withAlpha(C(1.f, 0.78f, 0.30f), a));
            text(textX + nw + 23.f * s, y + 20.f * s - js.size * 0.55f, "JOB", js);
        }
        TextStyle ps = tstyle(FONT_BODY, 13.f * s, withAlpha(m.unread ? C(0.86f, 0.90f, 1.f) : kTextDim, a));
        std::string preview = fitText(stripCodes(m.text), ps, p.sx + p.sw - textX - 34.f * s);
        text(textX, y + 36.f * s, preview.c_str(), ps);
        if (m.unread) circle(p.sx + p.sw - 22.f * s, y + 44.f * s, 5.f * s, withAlpha(kCyan, a));
        if (i + 1 < count) rect(textX, y + rowH - 1.f, p.sx + p.sw - textX - 14.f * s, 1.f, withAlpha(kWhite, 0.06f * a));
    }
    setClip(saved);
    if (n.confirm) {
        int idx = count - 1 - cur;
        I.threadIdx = idx;
        I.threadButton = 0;
        I.scroll[SC_THREAD] = 0.f;
        g_scrollShown.shown[SC_THREAD] = 0.f;
        go(SC_THREAD, 1);
        PhoneMessage& m = st.messages[idx];
        if (m.unread) {
            m.unread = false;
            act.type = PA_READ_MESSAGE;
            act.id = m.id;
        }
    }
}

// One message thread: bubble with the text and action buttons
void threadScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    appBackground(p, a);
    if (I.threadIdx < 0 || I.threadIdx >= (int)st.messages.size()) {
        go(SC_MESSAGES, -1);
        return;
    }
    const PhoneMessage& m = st.messages[I.threadIdx];
    drawHeader(p, m.from.c_str(), m.mission ? std::string("Job offer") : std::string(), a, kGreen);
    // buttons
    struct Btn { const char* label; int icon; int kind; };
    std::vector<Btn> btns;
    if (!m.actionLabel.empty()) btns.push_back({m.actionLabel.c_str(), ICO_CHECK, 0});
    if (m.hasLocation) btns.push_back({"Mark on map", ICO_BLIP0 + BLIP_WAYPOINT, 1});
    if (m.contactId >= 0) btns.push_back({"Call back", ICO_HANGUP, 2});
    int nb = (int)btns.size();
    int& cur = I.threadButton;
    if (nb) {
        cur = Clamp(cur, 0, nb - 1);
        if (n.up) cur = (cur + nb - 1) % nb;
        if (n.down) cur = (cur + 1) % nb;
    }
    float top = p.top + headerHeight(m.mission ? "x" : "") * s + 14.f * s;
    TextStyle ts = tstyle(FONT_BODY, 15.f * s, withAlpha(C(0.94f, 0.95f, 1.f), a));
    RichOpts ro;
    ro.pad = n.pad;
    ro.alpha = a;
    ro.lineSpacing = 1.32f;
    float maxW = p.sw * 0.74f;
    vec2 sz = richMeasure(m.text.c_str(), ts, maxW, ro);
    float bx = p.sx + 16.f * s, bw = sz.x + 28.f * s, bh = sz.y + 22.f * s;
    float btnH = 46.f * s, btnGap = 8.f * s;
    float buttonsH = nb ? nb * (btnH + btnGap) + 8.f * s : 0.f;
    float viewBottom = p.bottom - buttonsH;
    float scrollMax = Max(0.f, top + bh + 30.f * s - viewBottom);
    I.scroll[SC_THREAD] = scrollMax;
    float off = easedScroll(SC_THREAD, dt);
    ClipState saved = getClip();
    setClipRect(p.sx, p.top + headerHeight(m.mission ? "x" : "") * s, p.sw, viewBottom - p.top - headerHeight(m.mission ? "x" : "") * s);
    TextStyle tm = tstyle(FONT_BODY, 11.5f * s, withAlpha(kTextMute, a), ALIGN_CENTER);
    text(p.sx + p.sw * 0.5f, top - off - 4.f * s, m.time.c_str(), tm);
    float by = top + 16.f * s - off;
    roundRect(bx, by, bw, bh, 18.f * s, withAlpha(C(0.18f, 0.21f, 0.34f), a));
    triangle(vec2(bx + 2.f * s, by + bh - 16.f * s), vec2(bx + 16.f * s, by + bh - 2.f * s), vec2(bx - 6.f * s, by + bh + 4.f * s),
             withAlpha(C(0.18f, 0.21f, 0.34f), a));
    richDraw(bx + 14.f * s, by + 11.f * s, m.text.c_str(), ts, maxW, ro);
    setClip(saved);
    for (int i = 0; i < nb; i++) {
        float y = viewBottom + 8.f * s + i * (btnH + btnGap);
        float x = p.sx + 16.f * s, w = p.sw - 32.f * s;
        bool sel = i == cur;
        if (sel) roundRectGradient(x, y, w, btnH, 14.f * s, withAlpha(C(1.f, 0.30f, 0.64f), a), withAlpha(C(0.80f, 0.12f, 0.50f), a));
        else roundRect(x, y, w, btnH, 14.f * s, withAlpha(C(1.f, 1.f, 1.f), 0.08f * a), 1.f * s, withAlpha(kWhite, 0.14f * a));
        float ang = btns[i].kind == 2 ? -2.3f : 0.f;
        drawIcon(btns[i].icon, x + 26.f * s, y + btnH * 0.5f, 22.f * s, withAlpha(kWhite, a), 0.f, 0, ang);
        TextStyle bs = tstyle(FONT_HEADING, 17.f * s, withAlpha(kWhite, a));
        bs.tracking = 0.03f;
        std::string lab = fitText(btns[i].label, bs, w - 60.f * s);
        text(x + 48.f * s, y + btnH * 0.5f - bs.size * 0.56f, lab.c_str(), bs);
    }
    if (n.confirm && nb) {
        const Btn& b = btns[cur];
        if (b.kind == 0) {
            act.type = PA_MESSAGE_ACTION;
            act.id = m.id;
        } else if (b.kind == 1) {
            act.type = PA_SET_WAYPOINT;
            act.id = m.id;
            act.pos = m.location;
            showToast("Location marked on your map");
        } else if (b.kind == 2 && st.call == CALL_NONE) {
            act.type = PA_CALL;
            act.id = m.contactId;
            std::string name = m.from;
            for (const PhoneContact& c : st.contacts)
                if (c.id == m.contactId) name = c.name;
            st.call = CALL_OUTGOING;
            st.callName = name;
            st.callContactId = m.contactId;
            st.callSeconds = 0.f;
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Tidegram
// Procedural thumbnails for NPC posts
void drawPostArt(int kind, u32 seed, float x, float y, float w, float h, float a) {
    auto R = [&](int k) { return (float)((seed >> (k * 3)) & 1023u) / 1023.f; };
    switch (kind) {
        case 1: {   // sunset over the water with a palm
            gradientRect(x, y, w, h * 0.62f, withAlpha(C(0.36f, 0.12f, 0.48f), a), withAlpha(C(1.f, 0.52f, 0.36f), a));
            float sx = x + w * (0.35f + 0.3f * R(0)), sy = y + h * 0.58f, sr = h * 0.26f;
            circleSoft(sx, sy, sr * 1.4f, sr * 1.4f, withAlpha(C(1.f, 0.6f, 0.4f), 0.5f * a));
            circle(sx, sy, sr, withAlpha(C(1.f, 0.82f, 0.46f), a));
            gradientRect(x, y + h * 0.62f, w, h * 0.38f, withAlpha(C(0.46f, 0.20f, 0.42f), a), withAlpha(C(0.10f, 0.05f, 0.18f), a));
            for (int k = 0; k < 5; k++) rect(sx - sr * (1.f - k * 0.15f), y + h * (0.66f + k * 0.06f), sr * 2.f * (1.f - k * 0.15f), h * 0.012f,
                                             withAlpha(C(1.f, 0.72f, 0.5f), 0.6f * a));
            drawIcon(ICO_PALM, x + w * 0.16f, y + h * 0.52f, h * 0.95f, withAlpha(C(0.06f, 0.02f, 0.08f), a));
            break;
        }
        case 2: {   // night skyline with lit windows
            gradientRect(x, y, w, h, withAlpha(C(0.04f, 0.04f, 0.14f), a), withAlpha(C(0.20f, 0.06f, 0.28f), a));
            float bx = x;
            int k = 0;
            while (bx < x + w) {
                float bw = w * (0.07f + 0.08f * R(k % 9));
                float bh = h * (0.3f + 0.55f * R((k + 3) % 9));
                rect(bx, y + h - bh, bw - 1.f, bh, withAlpha(C(0.06f, 0.05f, 0.14f), a));
                for (int wy = 0; wy < (int)(bh / (h * 0.07f)); wy++)
                    for (int wx = 0; wx < 3; wx++)
                        if (((seed >> ((k + wy * 3 + wx) % 29)) & 3u) == 0u)
                            rect(bx + bw * (0.18f + wx * 0.26f), y + h - bh + h * 0.04f + wy * h * 0.07f, bw * 0.12f, h * 0.025f,
                                 withAlpha((wx + wy) % 3 ? C(1.f, 0.82f, 0.46f) : C(0.40f, 0.90f, 1.f), 0.85f * a));
                bx += bw;
                k++;
            }
            rect(x, y + h - h * 0.05f, w, h * 0.05f, withAlpha(C(1.f, 0.30f, 0.70f), 0.6f * a));
            break;
        }
        case 3: {   // police lights in the dark
            gradientRect(x, y, w, h, withAlpha(C(0.03f, 0.03f, 0.07f), a), withAlpha(C(0.08f, 0.08f, 0.12f), a));
            for (int k = 0; k < 7; k++) {
                float cx = x + w * (0.1f + 0.8f * R(k)), cy = y + h * (0.3f + 0.4f * R(k + 5));
                u32 c = k % 2 ? C(0.2f, 0.4f, 1.f) : C(1.f, 0.12f, 0.16f);
                circleSoft(cx, cy, h * (0.12f + 0.12f * R(k + 2)), h * 0.2f, withAlpha(c, 0.55f * a));
            }
            drawIcon(ICO_BLIP0 + BLIP_VEHICLE, x + w * 0.5f, y + h * 0.72f, h * 0.7f, withAlpha(C(0.01f, 0.01f, 0.02f), a));
            break;
        }
        case 4: {   // beach with umbrella
            gradientRect(x, y, w, h * 0.5f, withAlpha(C(0.40f, 0.72f, 0.98f), a), withAlpha(C(0.72f, 0.90f, 1.f), a));
            rect(x, y + h * 0.5f, w, h * 0.18f, withAlpha(C(0.10f, 0.62f, 0.74f), a));
            rect(x, y + h * 0.5f + h * 0.03f, w, h * 0.012f, withAlpha(C(0.9f, 1.f, 1.f), 0.6f * a));
            gradientRect(x, y + h * 0.68f, w, h * 0.32f, withAlpha(C(0.98f, 0.86f, 0.62f), a), withAlpha(C(0.90f, 0.74f, 0.50f), a));
            float ux = x + w * (0.3f + 0.4f * R(1)), uy = y + h * 0.56f;
            capsule(ux, uy, ux + w * 0.02f, y + h * 0.92f, h * 0.02f, withAlpha(C(0.4f, 0.3f, 0.25f), a));
            triangle(vec2(ux - w * 0.14f, uy + h * 0.06f), vec2(ux + w * 0.14f, uy + h * 0.06f), vec2(ux, uy - h * 0.12f),
                     withAlpha(C(1.f, 0.30f, 0.40f), a));
            circle(x + w * 0.82f, y + h * 0.16f, h * 0.08f, withAlpha(C(1.f, 0.96f, 0.72f), a));
            break;
        }
        case 5: {   // car with speed lines
            gradientRect(x, y, w, h, withAlpha(C(0.98f, 0.52f, 0.36f), a), withAlpha(C(0.40f, 0.12f, 0.44f), a));
            quad(vec2(x + w * 0.42f, y + h * 0.55f), vec2(x + w * 0.58f, y + h * 0.55f), vec2(x + w, y + h), vec2(x, y + h),
                 withAlpha(C(0.12f, 0.08f, 0.16f), a));
            for (int k = 0; k < 5; k++)
                capsule(x + w * (0.05f + 0.1f * k), y + h * (0.35f + 0.08f * k), x + w * (0.25f + 0.1f * k), y + h * (0.35f + 0.08f * k),
                        h * 0.012f, withAlpha(kWhite, 0.35f * a));
            drawIcon(ICO_BLIP0 + BLIP_CAR_SHOP, x + w * 0.56f, y + h * 0.62f, h * 0.8f, withAlpha(C(0.04f, 0.03f, 0.07f), a));
            break;
        }
        case 6: {   // smoke and fire glow
            gradientRect(x, y, w, h, withAlpha(C(0.34f, 0.32f, 0.36f), a), withAlpha(C(0.95f, 0.46f, 0.16f), a));
            for (int k = 0; k < 9; k++) {
                float cx = x + w * (0.3f + 0.4f * R(k)), cy = y + h * (0.15f + 0.5f * R(k + 4));
                circleSoft(cx, cy, h * (0.16f + 0.12f * R(k + 1)), h * 0.2f, withAlpha(C(0.12f, 0.12f, 0.14f), 0.7f * a));
            }
            circleSoft(x + w * 0.5f, y + h * 0.95f, h * 0.3f, h * 0.35f, withAlpha(C(1.f, 0.7f, 0.2f), 0.8f * a));
            for (int k = 0; k < 6; k++) rect(x + w * (0.08f + k * 0.16f), y + h * (0.62f + 0.1f * R(k)), w * 0.1f, h, withAlpha(C(0.05f, 0.04f, 0.06f), a));
            break;
        }
        case 7: {   // food on a plate
            gradientRect(x, y, w, h, withAlpha(C(0.62f, 0.40f, 0.26f), a), withAlpha(C(0.40f, 0.24f, 0.16f), a));
            float cx = x + w * 0.5f, cy = y + h * 0.54f, r = h * 0.38f;
            circle(cx + 2.f, cy + 3.f, r, withAlpha(C(0.f, 0.f, 0.f), 0.3f * a));
            circle(cx, cy, r, withAlpha(C(0.96f, 0.96f, 0.94f), a));
            circle(cx, cy, r * 0.8f, withAlpha(C(0.88f, 0.88f, 0.86f), a), r * 0.04f);
            const float cols[5][3] = {{0.95f, 0.72f, 0.26f}, {0.36f, 0.72f, 0.28f}, {0.90f, 0.26f, 0.20f}, {0.98f, 0.86f, 0.52f}, {0.52f, 0.30f, 0.16f}};
            for (int k = 0; k < 7; k++) {
                float ang = R(k) * kTwoPi, d = r * 0.45f * R(k + 3);
                circle(cx + cosf(ang) * d, cy + sinf(ang) * d, r * (0.16f + 0.12f * R(k + 6)),
                       withAlpha(C(cols[k % 5][0], cols[k % 5][1], cols[k % 5][2]), a));
            }
            break;
        }
        case 8: {   // rain on the street
            gradientRect(x, y, w, h, withAlpha(C(0.30f, 0.36f, 0.44f), a), withAlpha(C(0.12f, 0.14f, 0.20f), a));
            for (int k = 0; k < 5; k++) {
                float bh = h * (0.3f + 0.4f * R(k));
                rect(x + w * (0.02f + 0.2f * k), y + h * 0.7f - bh, w * 0.16f, bh, withAlpha(C(0.16f, 0.18f, 0.24f), a));
            }
            for (int k = 0; k < 40; k++) {
                float rx = x + w * R(k % 11) * 0.97f + (k * 7 % 13) * w * 0.003f, ry = y + h * fmodf(R((k + 3) % 11) + k * 0.13f, 1.f);
                capsule(rx, ry, rx - w * 0.015f, ry + h * 0.08f, 0.8f, withAlpha(C(0.8f, 0.9f, 1.f), 0.45f * a));
            }
            rect(x, y + h * 0.7f, w, h * 0.3f, withAlpha(C(0.10f, 0.12f, 0.16f), a));
            for (int k = 0; k < 4; k++) rect(x + w * (0.1f + 0.22f * k), y + h * 0.78f, w * 0.1f, h * 0.012f, withAlpha(C(1.f, 0.8f, 0.4f), 0.5f * a));
            break;
        }
        case 9: {   // palms against the sky
            gradientRect(x, y, w, h, withAlpha(C(0.36f, 0.72f, 1.f), a), withAlpha(C(0.86f, 0.94f, 1.f), a));
            for (int k = 0; k < 4; k++)
                drawIcon(ICO_PALM, x + w * (0.15f + 0.24f * k), y + h * (0.55f + 0.1f * R(k)), h * (0.9f + 0.3f * R(k + 2)),
                         withAlpha(C(0.08f, 0.24f, 0.16f), a), 0.f, 0, (R(k + 5) - 0.5f) * 0.4f);
            break;
        }
        default: {  // bright day skyline
            gradientRect(x, y, w, h, withAlpha(C(0.46f, 0.74f, 1.f), a), withAlpha(C(0.84f, 0.92f, 1.f), a));
            float bx = x;
            int k = 0;
            while (bx < x + w) {
                float bw = w * (0.08f + 0.07f * R(k % 9));
                float bh = h * (0.25f + 0.5f * R((k + 2) % 9));
                rect(bx, y + h - bh, bw - 1.f, bh, withAlpha(C(0.58f, 0.66f, 0.80f), a));
                bx += bw;
                k++;
            }
            circle(x + w * 0.8f, y + h * 0.22f, h * 0.09f, withAlpha(C(1.f, 0.98f, 0.86f), a));
            break;
        }
    }
}

float postHeight(const TidePost& p, float w, float s, bool pad) {
    for (const auto& e : I.postHeights)
        if (e.first == p.id) return e.second;
    TextStyle ts = tstyle(FONT_BODY, 14.f * s, kWhite);
    RichOpts ro;
    ro.pad = pad;
    ro.lineSpacing = 1.3f;
    vec2 sz = richMeasure(p.text.c_str(), ts, w - 66.f * s, ro);
    float h = 12.f * s + 22.f * s + sz.y + 8.f * s;
    if (p.image) h += (w - 66.f * s) * 0.5625f + 10.f * s;
    h += 28.f * s;
    I.postHeights.push_back({p.id, h});
    if (I.postHeights.size() > 96) I.postHeights.erase(I.postHeights.begin());
    return h;
}

void tidegramScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    gradientRect(p.sx, p.sy, p.sw, p.sh, withAlpha(C(0.05f, 0.05f, 0.11f), a), withAlpha(C(0.02f, 0.02f, 0.06f), a));
    const std::vector<TidePost>& posts = tidePosts();
    tideMarkSeen();
    // header: back chevron + gradient wordmark
    drawIcon(ICO_CHEVRON, p.sx + 22.f * s, p.top + 20.f * s, 22.f * s, withAlpha(kCyan, a), 0.f, 0, kPi);
    TextStyle ws = tstyle(FONT_TITLE, 28.f * s, withAlpha(C(0.40f, 0.92f, 1.f), a));
    ws.colorBottom = withAlpha(C(0.86f, 0.36f, 1.f), a);
    ws.skew = 0.12f;
    ws.tracking = 0.01f;
    float ww = text(p.sx + 40.f * s, p.top + 3.f * s, "tidegram", ws);
    drawIcon(ICO_TIDE, p.sx + 64.f * s + ww, p.top + 20.f * s, 22.f * s, withAlpha(C(0.60f, 0.70f, 1.f), a));
    rect(p.sx + 16.f * s, p.top + 42.f * s, p.sw - 32.f * s, 1.f, withAlpha(kWhite, 0.08f * a));
    int count = (int)posts.size();
    int& cur = I.cursor[SC_TIDEGRAM];
    if (count == 0) return;
    if (I.postHeightW != p.sw) {
        I.postHeights.clear();
        I.postHeightW = p.sw;
    }
    // keep the selection on the same post when new ones arrive on top
    static int selectedId = -1;
    if (selectedId >= 0 && cur < count && posts[cur].id != selectedId)
        for (int i = 0; i < count; i++)
            if (posts[i].id == selectedId) {
                cur = i;
                break;
            }
    cur = Clamp(cur, 0, count - 1);
    if (n.up && cur > 0) cur--;
    if (n.down && cur + 1 < count) cur++;
    selectedId = posts[cur].id;
    float listTop = p.top + 46.f * s, viewH = p.bottom - listTop;
    float y0 = 0.f;
    for (int i = 0; i < cur; i++) y0 += postHeight(posts[i], p.sw, s, n.pad);
    float hCur = postHeight(posts[cur], p.sw, s, n.pad);
    listScroll(SC_TIDEGRAM, y0, Min(hCur, viewH), viewH, dt);
    float off = easedScroll(SC_TIDEGRAM, dt);
    ClipState saved = getClip();
    setClipRect(p.sx, listTop, p.sw, viewH);
    double now = tideClock();
    float y = listTop - off;
    for (int i = 0; i < count; i++) {
        const TidePost& post = posts[i];
        float h = postHeight(post, p.sw, s, n.pad);
        if (y > p.bottom) break;
        if (y + h >= listTop) {
            float x = p.sx;
            if (i == cur) roundRect(x + 6.f * s, y + 2.f * s, p.sw - 12.f * s, h - 4.f * s, 12.f * s, withAlpha(C(0.30f, 0.34f, 0.60f), 0.22f * a),
                                    1.f * s, withAlpha(C(0.50f, 0.60f, 1.f), 0.35f * a));
            drawAvatar(x + 32.f * s, y + 30.f * s, 17.f * s, post.author, post.color, post.color2, a);
            float tx = x + 58.f * s;
            TextStyle ns = tstyle(FONT_BODY, 14.5f * s, withAlpha(kWhite, a));
            std::string name = fitText(post.author, ns, p.sw * 0.42f);
            float nw = text(tx, y + 12.f * s, name.c_str(), ns);
            float hx = tx + nw + 4.f * s;
            if (post.verified) {
                drawIcon(ICO_VERIFIED, hx + 8.f * s, y + 21.f * s, 17.f * s, withAlpha(kCyan, a));
                hx += 19.f * s;
            }
            TextStyle hs = tstyle(FONT_BODY, 12.5f * s, withAlpha(kTextMute, a));
            std::string meta = "@" + post.handle + "  " + agoText(now - post.time);
            meta = fitText(meta, hs, p.sx + p.sw - 16.f * s - hx);
            text(hx, y + 14.f * s, meta.c_str(), hs);
            TextStyle ts = tstyle(FONT_BODY, 14.f * s, withAlpha(C(0.92f, 0.93f, 0.98f), a));
            RichOpts ro;
            ro.pad = n.pad;
            ro.alpha = a;
            ro.lineSpacing = 1.3f;
            vec2 tsz = richDraw(tx, y + 34.f * s, post.text.c_str(), ts, p.sw - 66.f * s, ro);
            float iy = y + 34.f * s + tsz.y + 8.f * s;
            if (post.image) {
                float iw = p.sw - 66.f * s, ih = iw * 0.5625f;
                // the image clip is the intersection of its rect with the list viewport
                float cy0 = Max(iy, listTop), cy1 = Min(iy + ih, p.bottom);
                if (cy1 > cy0) {
                    bool inside = cy0 == iy && cy1 == iy + ih;
                    if (inside) setClipRoundRect(tx, iy, iw, ih, 10.f * s);
                    else setClipRect(tx, cy0, iw, cy1 - cy0);
                    if (post.image < 0) {
                        ID3D11ShaderResourceView* srv = snapshotSrv(post.snapshot);
                        if (srv) image(srv, tx, iy, iw, ih, 0, 0, 1, 1, withAlpha(kWhite, a));
                        else {
                            rect(tx, iy, iw, ih, withAlpha(C(0.12f, 0.12f, 0.18f), a));
                            drawIcon(ICO_IMAGE, tx + iw * 0.5f, iy + ih * 0.5f, ih * 0.4f, withAlpha(kTextMute, a));
                        }
                    } else drawPostArt(post.image, post.seed, tx, iy, iw, ih, a);
                    setClipRect(p.sx, listTop, p.sw, viewH);
                }
                iy += ih + 10.f * s;
            }
            // engagement row
            TextStyle es = tstyle(FONT_BODY, 12.5f * s, withAlpha(kTextDim, a));
            float ex = tx, ey = iy + 10.f * s;
            int replies = (int)(post.likes * 0.045f), reposts = (int)(post.likes * 0.12f);
            drawIcon(ICO_COMMENT, ex + 8.f * s, ey, 17.f * s, withAlpha(kTextDim, a));
            text(ex + 20.f * s, ey - 8.f * s, countText(replies).c_str(), es);
            ex += (p.sw - 66.f * s) * 0.34f;
            drawIcon(ICO_REPOST, ex + 8.f * s, ey, 18.f * s, withAlpha(kTextDim, a));
            text(ex + 21.f * s, ey - 8.f * s, countText(reposts).c_str(), es);
            ex += (p.sw - 66.f * s) * 0.34f;
            u32 hc = post.liked ? C(1.f, 0.30f, 0.52f) : kTextDim;
            drawIcon(post.liked ? ICO_HEART : ICO_HEART_OUTLINE, ex + 8.f * s, ey, 17.f * s, withAlpha(hc, a));
            TextStyle ls = es;
            ls.color = withAlpha(hc, a);
            text(ex + 21.f * s, ey - 8.f * s, countText(post.likes).c_str(), ls);
            if (i + 1 < count) rect(p.sx + 16.f * s, y + h - 1.f, p.sw - 32.f * s, 1.f, withAlpha(kWhite, 0.07f * a));
        }
        y += h;
    }
    setClip(saved);
    if (n.confirm) {
        bool liked = tideToggleLike(posts[cur].id);
        act.type = PA_LIKE_POST;
        act.id = posts[cur].id;
        (void)liked;
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Camera (viewfinder), gallery
PhotoFx fxFromPhoto(const PhotoMode& ph, float time, bool dof) {
    PhotoFx fx;
    fx.filter = Clamp(ph.filter, 0, kFilterCount - 1);
    fx.strength = ph.filterStrength;
    fx.exposure = ph.exposure;
    fx.contrast = ph.contrast;
    fx.saturation = ph.saturation;
    fx.temperature = ph.temperature;
    fx.vignette = ph.vignette;
    fx.grain = ph.grain;
    fx.dof = dof && ph.dof;
    float focus = ph.autoFocus && ph.autoFocusDistance > 0.f ? ph.autoFocusDistance : ph.focusDistance;
    fx.focusDistance = focus;
    fx.aperture = ph.aperture;
    fx.fovY = ph.camFov;
    fx.time = time;
    return fx;
}

void enterPhotoMode(PhoneState& st, PhoneAction& act) {
    PhotoMode& ph = st.photo;
    ph.active = true;
    ph.camPos = st.cameraPos;
    ph.camYaw = st.cameraYaw;
    ph.camPitch = st.cameraPitch;
    ph.camRoll = 0.f;
    ph.camFov = Clamp(st.cameraFov, 0.3f, 1.6f);
    ph.filter = I.quickFilter;
    I.photoUi = true;
    I.photoRow = 0;
    act.type = PA_ENTER_PHOTO_MODE;
}

void cameraScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    rect(p.sx, p.sy, p.sw, p.sh, withAlpha(C(0.f, 0.f, 0.f), a));
    int& cur = I.cameraButton;
    if (n.left) cur = (cur + 2) % 3;
    if (n.right) cur = (cur + 1) % 3;
    if (n.up) I.quickFilter = (I.quickFilter + kFilterCount - 1) % kFilterCount;
    if (n.down) I.quickFilter = (I.quickFilter + 1) % kFilterCount;
    // viewfinder: the center of the frame, graded with the chosen filter
    float vx = p.sx, vy = p.top + 4.f * s, vw = p.sw, vh = p.bottom - 118.f * s - vy;
    float W = (float)screenWidth(), H = (float)screenHeight();
    float srcAspect = vw / Max(vh, 1.f), scrAspect = W / Max(H, 1.f);
    float uw = Min(1.f, srcAspect / scrAspect), vhh = Min(1.f, scrAspect / srcAspect);
    PhotoFx fx;
    fx.filter = I.quickFilter;
    fx.vignette = 0.2f;
    fx.time = I.time;
    photoEffect(vx, vy, vw, vh, 0.5f - uw * 0.5f, 0.5f - vhh * 0.5f, 0.5f + uw * 0.5f, 0.5f + vhh * 0.5f, fx);
    // thirds grid
    for (int k = 1; k < 3; k++) {
        rect(vx + vw * k / 3.f, vy, 1.f, vh, withAlpha(kWhite, 0.22f * a));
        rect(vx, vy + vh * k / 3.f, vw, 1.f, withAlpha(kWhite, 0.22f * a));
    }
    // filter name pill
    {
        TextStyle fs = tstyle(FONT_HEADING, 14.f * s, withAlpha(kWhite, a), ALIGN_CENTER);
        fs.tracking = 0.08f;
        std::string nm = upper(kFilterNames[I.quickFilter]);
        float fw = textWidth(nm.c_str(), fs) + 54.f * s;
        float fx0 = vx + vw * 0.5f - fw * 0.5f, fy0 = vy + 12.f * s;
        roundRect(fx0, fy0, fw, 28.f * s, 14.f * s, withAlpha(C(0.f, 0.f, 0.f), 0.55f * a));
        text(vx + vw * 0.5f, fy0 + 14.f * s - fs.size * 0.56f, nm.c_str(), fs);
        // up / down change the filter
        drawIcon(ICO_CHEVRON, fx0 + 15.f * s, fy0 + 14.f * s, 13.f * s, withAlpha(kWhite, 0.8f * a), 0.f, 0, -kHalfPi);
        drawIcon(ICO_CHEVRON, fx0 + fw - 15.f * s, fy0 + 14.f * s, 13.f * s, withAlpha(kWhite, 0.8f * a), 0.f, 0, kHalfPi);
    }
    // controls
    float cy = p.bottom - 62.f * s;
    float cxs[3] = {p.sx + 52.f * s, p.sx + p.sw * 0.5f, p.sx + p.sw - 52.f * s};
    // gallery thumbnail
    {
        float tw = 44.f * s;
        int last = I.gallery.empty() ? -1 : I.gallery.back();
        ID3D11ShaderResourceView* srv = snapshotSrv(last);
        roundRect(cxs[0] - tw * 0.5f, cy - tw * 0.5f, tw, tw, 10.f * s, withAlpha(C(0.16f, 0.16f, 0.2f), a));
        if (srv) {
            ClipState saved = getClip();
            setClipRoundRect(cxs[0] - tw * 0.5f, cy - tw * 0.5f, tw, tw, 10.f * s);
            image(srv, cxs[0] - tw * 0.89f, cy - tw * 0.5f, tw * 1.78f, tw, 0, 0, 1, 1, withAlpha(kWhite, a));
            setClip(saved);
        } else drawIcon(ICO_IMAGE, cxs[0], cy, 26.f * s, withAlpha(kTextDim, a));
        if (cur == 0) roundRect(cxs[0] - tw * 0.5f - 3.f * s, cy - tw * 0.5f - 3.f * s, tw + 6.f * s, tw + 6.f * s, 12.f * s, 0, 2.f * s, withAlpha(kPink, a));
    }
    // shutter
    {
        float r = 30.f * s;
        circle(cxs[1], cy, r, withAlpha(kWhite, a), 3.5f * s);
        circle(cxs[1], cy, r - 7.f * s, withAlpha(cur == 1 ? kWhite : C(0.85f, 0.85f, 0.9f), a));
        if (cur == 1) circle(cxs[1], cy, r + 5.f * s, withAlpha(kPink, a), 2.f * s);
    }
    // photo mode
    {
        float r = 22.f * s;
        circle(cxs[2], cy, r, withAlpha(C(0.16f, 0.16f, 0.20f), a));
        drawIcon(ICO_APERTURE, cxs[2], cy, 26.f * s, withAlpha(kWhite, a));
        if (cur == 2) circle(cxs[2], cy, r + 4.f * s, withAlpha(kPink, a), 2.f * s);
    }
    TextStyle ls = tstyle(FONT_HEADING, 11.5f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
    ls.tracking = 0.1f;
    const char* labels[3] = {"GALLERY", "PHOTO", "PHOTO MODE"};
    text(cxs[cur], cy + 36.f * s, labels[cur], ls);
    if (n.confirm) {
        if (cur == 0) {
            go(SC_GALLERY, 1);
            I.cursor[SC_GALLERY] = Max(0, (int)I.gallery.size() - 1);
            I.galleryOpen = -1;
        } else if (cur == 1) {
            I.captureStage = 1;
        } else enterPhotoMode(st, act);
    }
}

void galleryScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt) {
    float s = p.s;
    appBackground(p, a);
    int count = (int)I.gallery.size();
    std::string sub = count ? std::to_string(count) + (count == 1 ? " photo" : " photos") : std::string("No photos yet");
    drawHeader(p, "Gallery", sub, a, kCyan);
    if (count == 0) {
        drawIcon(ICO_IMAGE, p.sx + p.sw * 0.5f, p.top + 180.f * s, 70.f * s, withAlpha(kTextMute, a));
        TextStyle es = tstyle(FONT_BODY, 14.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(p.sx + p.sw * 0.5f, p.top + 230.f * s, "Photos you take appear here.", es);
        return;
    }
    int& cur = I.cursor[SC_GALLERY];
    cur = Clamp(cur, 0, count - 1);
    if (I.galleryOpen >= 0) {
        // single photo, newest first order in the grid; shown large
        if (n.left && I.galleryOpen > 0) I.galleryOpen--;
        if (n.right && I.galleryOpen + 1 < count) I.galleryOpen++;
        int id = I.gallery[count - 1 - I.galleryOpen];
        float iw = p.sw - 24.f * s, ih = iw * 0.5625f;
        float ix = p.sx + 12.f * s, iy = p.top + 120.f * s;
        ID3D11ShaderResourceView* srv = snapshotSrv(id);
        ClipState saved = getClip();
        setClipRoundRect(ix, iy, iw, ih, 10.f * s);
        if (srv) image(srv, ix, iy, iw, ih, 0, 0, 1, 1, withAlpha(kWhite, a));
        else rect(ix, iy, iw, ih, withAlpha(C(0.12f, 0.12f, 0.18f), a));
        setClip(saved);
        TextStyle ts = tstyle(FONT_BODY, 13.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
        std::string idx = std::to_string(I.galleryOpen + 1) + " / " + std::to_string(count);
        text(p.sx + p.sw * 0.5f, iy + ih + 14.f * s, idx.c_str(), ts);
        TextStyle hs = tstyle(FONT_BODY, 12.5f * s, withAlpha(kTextMute, a), ALIGN_CENTER);
        text(p.sx + p.sw * 0.5f, iy + ih + 36.f * s, "Saved to your Photos folder", hs);
        if (n.back) I.galleryOpen = -1;
        return;
    }
    if (n.left && cur > 0) cur--;
    if (n.right && cur + 1 < count) cur++;
    if (n.up && cur >= 2) cur -= 2;
    if (n.down && cur + 2 < count) cur += 2;
    float gx = p.sx + 12.f * s, gw = (p.sw - 32.f * s) * 0.5f, gh = gw * 0.5625f;
    float top = p.top + headerHeight(sub) * s + 8.f * s;
    float viewH = p.bottom - top;
    listScroll(SC_GALLERY, (cur / 2) * (gh + 8.f * s), gh + 8.f * s, viewH, dt);
    float off = easedScroll(SC_GALLERY, dt);
    ClipState saved = getClip();
    for (int i = 0; i < count; i++) {
        int id = I.gallery[count - 1 - i];
        float x = gx + (i % 2) * (gw + 8.f * s), y = top + (i / 2) * (gh + 8.f * s) - off;
        if (y + gh < top || y > p.bottom) continue;
        float cy0 = Max(y, top), cy1 = Min(y + gh, p.bottom);
        if (cy0 == y && cy1 == y + gh) setClipRoundRect(x, y, gw, gh, 8.f * s);
        else setClipRect(x, cy0, gw, cy1 - cy0);
        ID3D11ShaderResourceView* srv = snapshotSrv(id);
        if (srv) image(srv, x, y, gw, gh, 0, 0, 1, 1, withAlpha(kWhite, a));
        else rect(x, y, gw, gh, withAlpha(C(0.12f, 0.12f, 0.18f), a));
        setClip(saved);
        if (i == cur) roundRect(x - 2.f * s, y - 2.f * s, gw + 4.f * s, gh + 4.f * s, 10.f * s, 0, 2.2f * s, withAlpha(kPink, a));
    }
    if (n.confirm) I.galleryOpen = cur;
}

// ------------------------------------------------------------------------------------------------------------------
// Map app: full-bleed map around the player with blips and the GPS route
void mapScreen(PhoneState& st, const HudState& hud, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    rect(p.sx, p.sy, p.sw, p.sh, withAlpha(C(0.05f, 0.09f, 0.14f), a));
    if (n.up) I.mapZoom = Max(0.8f, I.mapZoom / 1.35f);
    if (n.down) I.mapZoom = Min(14.f, I.mapZoom * 1.35f);
    static float zoomShown = 2.4f;
    zoomShown = approachExp(zoomShown, I.mapZoom, 10.f, dt);
    MapView v;
    v.center = hud.playerPos;
    v.mpp = zoomShown / Max(s, 0.1f);
    v.rot = 0.f;
    v.screenCenter = vec2(p.sx + p.sw * 0.5f, p.sy + p.sh * 0.52f);
    if (mapReady()) {
        MapDrawOpts o;
        o.style = MAPSTYLE_RADAR;
        o.alpha = a;
        o.extentMin = vec2(p.sx, p.sy);
        o.extentMax = vec2(p.sx + p.sw, p.sy + p.sh);
        o.extentPx = Max(p.sw, p.sh);
        o.buildings = true;
        drawMapBase(v, o);
        drawMapLines(v, a, s * 0.8f, vec2(p.sx, p.sy), vec2(p.sx + p.sw, p.sy + p.sh), false);
        if (!hud.gpsRoute.empty())
            drawMapRoute(v, hud.gpsRoute, C(1.f, 0.4f, 0.8f), Max(2.5f, 3.5f * s), a, vec2(p.sx, p.sy), vec2(p.sx + p.sw, p.sy + p.sh));
        // street names when zoomed in (kept clear of the info card, the hint bar and the player)
        float streetA = Saturate((2.0f - zoomShown) / 0.6f) * a;
        if (streetA > 0.01f) {
            static std::vector<vec4> occupied;
            occupied.clear();
            occupied.push_back(vec4(p.sx, p.sy, p.sx + p.sw, p.top + 64.f * s));
            occupied.push_back(vec4(p.sx, p.bottom - 50.f * s, p.sx + p.sw, p.sy + p.sh));
            vec2 pp0 = v.toScreen(hud.playerPos);
            occupied.push_back(vec4(pp0.x - 18.f * s, pp0.y - 18.f * s, pp0.x + 18.f * s, pp0.y + 18.f * s));
            drawStreetNames(v, vec2(p.sx + 6.f * s, p.sy), vec2(p.sx + p.sw - 6.f * s, p.sy + p.sh), streetA, s * 0.85f, occupied);
        }
    }
    float t = I.time;
    for (const Blip& b : hud.blips) {
        if (b.icon == BLIP_PLAYER) continue;
        vec2 bp = v.toScreen(b.pos);
        if (bp.x < p.sx - 10.f || bp.x > p.sx + p.sw + 10.f || bp.y < p.sy - 10.f || bp.y > p.sy + p.sh + 10.f) continue;
        drawBlipGlyph(b, bp, 24.f * s, a, t, false, false);
    }
    if (hud.hasWaypoint) {
        Blip wb;
        wb.pos = hud.waypoint;
        wb.icon = BLIP_WAYPOINT;
        drawBlipGlyph(wb, v.toScreen(hud.waypoint), 26.f * s, a, t, false, false);
    }
    vec2 pp = v.toScreen(hud.playerPos);
    float pulse = fmodf(t * 0.8f, 1.f);
    circle(pp.x, pp.y, (10.f + 18.f * pulse) * s, withAlpha(kWhite, 0.35f * (1.f - pulse) * a), 1.5f * s);
    drawIconGlow(BLIP_PLAYER, pp.x, pp.y, 28.f * s, C(0.f, 0.f, 0.f, 0.5f * a), 2.5f * s);
    drawIcon(BLIP_PLAYER, pp.x, pp.y, 28.f * s, withAlpha(kWhite, a), 1.5f * s, C(0.02f, 0.02f, 0.06f, a), v.screenAngle(hud.playerHeading));
    // top card: district and street
    std::string district = hud.zoneName.empty() ? (mapReady() ? mapDistrictAt(hud.playerPos) : std::string()) : hud.zoneName;
    std::string street = hud.streetName.empty() ? (mapReady() ? mapStreetAt(hud.playerPos, 120.f) : std::string()) : hud.streetName;
    float cardY = p.top + 4.f * s, cardH = street.empty() ? 40.f * s : 56.f * s;
    backdrop(p.sx + 12.f * s, cardY, p.sw - 24.f * s, cardH, 14.f * s, C(0.9f, 0.9f, 0.95f, a), C(0.03f, 0.05f, 0.12f, 0.7f), 0.6f);
    TextStyle ds = tstyle(FONT_HEADING, 18.f * s, withAlpha(kWhite, a));
    std::string dname = fitText(district.empty() ? std::string("Porto Sol") : district, ds, p.sw - 60.f * s);
    text(p.sx + 26.f * s, cardY + 9.f * s, dname.c_str(), ds);
    if (!street.empty()) {
        TextStyle ss = tstyle(FONT_BODY, 13.f * s, withAlpha(kTextDim, a));
        std::string sn = fitText(street, ss, p.sw - 60.f * s);
        text(p.sx + 26.f * s, cardY + 32.f * s, sn.c_str(), ss);
    }
    // bottom hints
    PromptItem pi[] = {{"UPDOWN", "DPADUD", "Zoom"}, {"ENTER", "A", "Full map"}};
    float bh = 19.f * s;
    backdrop(p.sx + 12.f * s, p.bottom - bh - 16.f * s, p.sw - 24.f * s, bh + 12.f * s, 12.f * s, C(0.9f, 0.9f, 0.95f, a),
             C(0.03f, 0.05f, 0.12f, 0.7f), 0.6f);
    drawPromptBar(p.sx + p.sw - 22.f * s, p.bottom - bh - 10.f * s, pi, 2, n.pad, bh, a);
    if (n.confirm) {
        act.type = PA_OPEN_MAP;
        st.open = false;
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Quick save
void quickSaveScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    appBackground(p, a);
    drawHeader(p, "Quick Save", std::string(), a, kBlue);
    float cx = p.sx + p.sw * 0.5f, cy = p.top + 150.f * s;
    roundRectGradient(cx - 48.f * s, cy - 48.f * s, 96.f * s, 96.f * s, 28.f * s, withAlpha(C(0.40f, 0.60f, 1.f), a),
                      withAlpha(C(0.16f, 0.28f, 0.82f), a));
    TextStyle hs = tstyle(FONT_HEADING, 22.f * s, withAlpha(kWhite, a), ALIGN_CENTER);
    TextStyle bs = tstyle(FONT_BODY, 14.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
    if (I.saveState == 1) {
        I.saveT += dt;
        // spinner
        for (int k = 0; k < 8; k++) {
            float ang = I.time * 6.f + k * kTwoPi / 8.f;
            circle(cx + cosf(ang) * 26.f * s, cy + sinf(ang) * 26.f * s, (2.f + k * 0.45f) * s, withAlpha(kWhite, (0.25f + k * 0.09f) * a));
        }
        text(cx, cy + 76.f * s, "Saving...", hs);
        if (I.saveT > 6.f) {
            I.saveState = 0;
            showToast("Save failed");
        }
        return;
    }
    if (I.saveState == 2) {
        drawIcon(ICO_CHECK, cx, cy, 58.f * s, withAlpha(kWhite, a));
        text(cx, cy + 76.f * s, "Saved", hs);
        I.saveT += dt;
        if (I.saveT > 1.4f) go(SC_HOME, -1);
        return;
    }
    drawIcon(ICO_SAVE, cx, cy, 52.f * s, withAlpha(kWhite, a));
    text(cx, cy + 76.f * s, "Save your progress?", hs);
    std::string body = st.canQuickSave ? std::string("Saves to the quick save slot. You can load it from the pause menu.")
                                       : (st.quickSaveNote.empty() ? std::string("Quick save is not available right now.") : st.quickSaveNote);
    TextStyle ws = bs;
    if (!st.canQuickSave) ws.color = withAlpha(C(1.f, 0.55f, 0.55f), a);
    textWrapped(cx, cy + 110.f * s, p.sw - 60.f * s, body.c_str(), ws, 1.3f);
    int& cur = I.saveChoice;
    if (n.up || n.down) cur = 1 - cur;
    const char* labels[2] = {"Save", "Cancel"};
    for (int i = 0; i < 2; i++) {
        float bw = p.sw - 48.f * s, bh = 46.f * s, bx = p.sx + 24.f * s, by = p.bottom - (2 - i) * (bh + 10.f * s) - 6.f * s;
        bool sel = i == cur;
        bool dis = i == 0 && !st.canQuickSave;
        float ea = dis ? a * 0.4f : a;
        if (sel) roundRectGradient(bx, by, bw, bh, 14.f * s, withAlpha(C(1.f, 0.30f, 0.64f), ea), withAlpha(C(0.80f, 0.12f, 0.50f), ea));
        else roundRect(bx, by, bw, bh, 14.f * s, withAlpha(C(1.f, 1.f, 1.f), 0.08f * ea), 1.f * s, withAlpha(kWhite, 0.14f * ea));
        TextStyle ls = tstyle(FONT_HEADING, 18.f * s, withAlpha(kWhite, ea), ALIGN_CENTER);
        ls.tracking = 0.05f;
        std::string lab = upper(labels[i]);
        text(bx + bw * 0.5f, by + bh * 0.5f - ls.size * 0.56f, lab.c_str(), ls);
    }
    if (n.confirm) {
        if (cur == 0 && st.canQuickSave) {
            act.type = PA_QUICK_SAVE;
            I.saveState = 1;
            I.saveT = 0.f;
        } else if (cur == 1) go(SC_HOME, -1);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Game-provided list app
void listScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    appBackground(p, a);
    if (I.listApp < 0 || I.listApp >= (int)st.apps.size()) {
        go(SC_HOME, -1);
        return;
    }
    const PhoneListApp& app = st.apps[I.listApp];
    drawHeader(p, app.name.c_str(), app.subtitle, a, kPink);
    int count = (int)app.items.size();
    int& cur = I.cursor[SC_LIST];
    if (count == 0) {
        TextStyle es = tstyle(FONT_BODY, 15.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(p.sx + p.sw * 0.5f, p.top + 140.f * s, "Nothing here yet.", es);
        return;
    }
    cur = Clamp(cur, 0, count - 1);
    if (n.up) cur = (cur + count - 1) % count;
    if (n.down) cur = (cur + 1) % count;
    float listTop = p.top + headerHeight(app.subtitle) * s + 4.f * s, rowH = 66.f * s;
    float viewH = p.bottom - listTop;
    listScroll(SC_LIST, cur * rowH, rowH, viewH, dt);
    float off = easedScroll(SC_LIST, dt);
    ClipState saved = getClip();
    setClipRect(p.sx, listTop, p.sw, viewH);
    for (int i = 0; i < count; i++) {
        const PhoneListItem& it = app.items[i];
        float y = listTop + i * rowH - off;
        if (y + rowH < listTop || y > p.bottom) continue;
        float ea = it.enabled ? a : a * 0.45f;
        if (i == cur) rowHighlight(p, y, rowH, a);
        float x = p.sx + 20.f * s;
        std::string right;
        u32 rc = kTextDim;
        if (it.price >= 0) {
            right = fmtMoney(it.price);
            rc = it.price > st.money ? C(1.f, 0.42f, 0.42f) : kGreenMoney;
        } else right = it.right;
        TextStyle rs = tstyle(FONT_HEADING, 15.f * s, withAlpha(rc, ea), ALIGN_RIGHT);
        float rw = right.empty() ? 0.f : textWidth(right.c_str(), rs) + 10.f * s;
        TextStyle ls = tstyle(FONT_BODY, 16.f * s, withAlpha(kWhite, ea));
        std::string lab = fitText(it.label, ls, p.sw - 44.f * s - rw);
        float ly = it.detail.empty() ? y + rowH * 0.5f - 10.f * s : y + 12.f * s;
        text(x, ly, lab.c_str(), ls);
        if (!right.empty()) text(p.sx + p.sw - 20.f * s, ly + 1.f * s, right.c_str(), rs);
        if (!it.detail.empty()) {
            TextStyle ds = tstyle(FONT_BODY, 12.f * s, withAlpha(kTextDim, ea));
            std::string d = fitText(it.detail, ds, p.sw - 40.f * s);
            text(x, y + 36.f * s, d.c_str(), ds);
        }
        if (i + 1 < count) rect(x, y + rowH - 1.f, p.sw - 34.f * s, 1.f, withAlpha(kWhite, 0.06f * a));
    }
    setClip(saved);
    if (n.confirm && app.items[cur].enabled) {
        act.type = PA_APP_ITEM;
        act.app = app.id;
        act.id = app.items[cur].id;
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Call screen
void callScreen(PhoneState& st, const PL& p, const Nav& n, float a, float dt, PhoneAction& act) {
    float s = p.s;
    gradientRect(p.sx, p.sy, p.sw, p.sh, withAlpha(C(0.16f, 0.08f, 0.26f), a), withAlpha(C(0.03f, 0.03f, 0.08f), a));
    circleSoft(p.sx + p.sw * 0.5f, p.top + 150.f * s, 150.f * s, 150.f * s, withAlpha(C(0.9f, 0.3f, 0.7f), 0.18f * a));
    bool ended = st.call == CALL_NONE;
    std::string name = ended ? I.callEndName : st.callName;
    u32 top, bottom;
    u32 given = 0;
    for (const PhoneContact& c : st.contacts)
        if (c.id == st.callContactId || c.name == name) given = c.color;
    avatarColors(name, given, top, bottom);
    float cx = p.sx + p.sw * 0.5f, ay = p.top + 140.f * s;
    if (st.call == CALL_INCOMING || st.call == CALL_OUTGOING) {
        for (int k = 0; k < 3; k++) {
            float ph = fmodf(I.time * 0.7f + k / 3.f, 1.f);
            circle(cx, ay, (50.f + ph * 60.f) * s, withAlpha(kWhite, 0.25f * (1.f - ph) * a), 1.5f * s);
        }
    }
    drawAvatar(cx, ay, 50.f * s, name, top, bottom, a);
    TextStyle ns = tstyle(FONT_HEADING, 28.f * s, withAlpha(kWhite, a), ALIGN_CENTER);
    std::string nm = fitText(name, ns, p.sw - 30.f * s);
    text(cx, ay + 66.f * s, nm.c_str(), ns);
    std::string status;
    if (ended) status = "Call ended";
    else if (st.call == CALL_INCOMING) status = "Incoming call";
    else if (st.call == CALL_OUTGOING) {
        int dots = (int)(I.time * 2.5f) % 4;
        status = "Calling" + std::string((size_t)dots, '.');
    } else status = fmtTime(st.callSeconds);
    TextStyle ss = tstyle(FONT_BODY, 16.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
    text(cx, ay + 104.f * s, status.c_str(), ss);
    if (ended) return;
    float by = p.bottom - 90.f * s, r = 32.f * s;
    if (st.call == CALL_INCOMING) {
        int& cur = I.callButton;
        if (n.left) cur = 0;
        if (n.right) cur = 1;
        float bx[2] = {cx - 70.f * s, cx + 70.f * s};
        u32 cols[2] = {C(1.f, 0.25f, 0.30f), C(0.24f, 0.86f, 0.42f)};
        const char* labels[2] = {"Decline", "Answer"};
        for (int i = 0; i < 2; i++) {
            float pulse = i == 1 ? 1.f + 0.06f * sinf(I.time * 8.f) : 1.f;
            circle(bx[i], by, r * pulse, withAlpha(cols[i], a));
            drawIcon(ICO_HANGUP, bx[i], by, 34.f * s, withAlpha(kWhite, a), 0.f, 0, i == 1 ? -2.3f : 0.f);
            if (i == cur) circle(bx[i], by, r + 6.f * s, withAlpha(kWhite, a), 2.f * s);
            TextStyle ls = tstyle(FONT_BODY, 13.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
            text(bx[i], by + r + 10.f * s, labels[i], ls);
        }
        if (n.confirm) {
            act.type = cur == 1 ? PA_ANSWER : PA_DECLINE;
            act.id = st.callContactId;
        }
        if (n.back) {
            act.type = PA_DECLINE;
            act.id = st.callContactId;
        }
    } else {
        circle(cx, by, r, withAlpha(C(1.f, 0.25f, 0.30f), a));
        drawIcon(ICO_HANGUP, cx, by, 34.f * s, withAlpha(kWhite, a));
        circle(cx, by, r + 6.f * s, withAlpha(kWhite, a), 2.f * s);
        TextStyle ls = tstyle(FONT_BODY, 13.f * s, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(cx, by + r + 10.f * s, "End call", ls);
        if (n.confirm || n.back) {
            act.type = PA_HANG_UP;
            act.id = st.callContactId;
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Photo mode
enum PhotoRowKind { PR_TOGGLE, PR_SLIDER, PR_OPTION };
struct PhotoRow {
    const char* label;
    PhotoRowKind kind;
};
const PhotoRow kPhotoRows[] = {
    {"Freeze Time", PR_TOGGLE}, {"Hide Player", PR_TOGGLE}, {"Field of View", PR_SLIDER}, {"Roll", PR_SLIDER},
    {"Depth of Field", PR_TOGGLE}, {"Autofocus", PR_TOGGLE}, {"Focus Distance", PR_SLIDER}, {"Aperture", PR_OPTION},
    {"Filter", PR_OPTION}, {"Filter Strength", PR_SLIDER}, {"Exposure", PR_SLIDER}, {"Contrast", PR_SLIDER},
    {"Saturation", PR_SLIDER}, {"Temperature", PR_SLIDER}, {"Vignette", PR_SLIDER}, {"Grain", PR_SLIDER},
    {"Grid", PR_OPTION}, {"Frame", PR_OPTION},
};
const int kPhotoRowCount = (int)ARRAY_COUNT(kPhotoRows);

// Row value as text and 0..1 fill (sliders); d = -1/+1 adjusts
std::string photoRowValue(PhotoMode& ph, int row, int d, float& fill) {
    char b[48];
    fill = -1.f;
    switch (row) {
        case 0: if (d) ph.freeze = !ph.freeze; return ph.freeze ? "On" : "Off";
        case 1: if (d) ph.hidePlayer = !ph.hidePlayer; return ph.hidePlayer ? "On" : "Off";
        case 2: {
            float deg = ph.camFov * kRadToDeg;
            deg = Clamp(deg + d * 2.f, 15.f, 100.f);
            ph.camFov = deg * kDegToRad;
            fill = (deg - 15.f) / 85.f;
            snprintf(b, sizeof(b), "%.0f deg", deg);
            return b;
        }
        case 3: {
            float deg = Clamp(ph.camRoll * kRadToDeg + d * 1.f, -45.f, 45.f);
            ph.camRoll = deg * kDegToRad;
            fill = (deg + 45.f) / 90.f;
            snprintf(b, sizeof(b), "%+.0f deg", deg);
            return b;
        }
        case 4: if (d) ph.dof = !ph.dof; return ph.dof ? "On" : "Off";
        case 5: if (d) ph.autoFocus = !ph.autoFocus; return ph.autoFocus ? "On" : "Off";
        case 6: {
            float lg = log10f(Clamp(ph.focusDistance, 0.5f, 300.f));
            lg = Clamp(lg + d * 0.05f, log10f(0.5f), log10f(300.f));
            ph.focusDistance = powf(10.f, lg);
            if (d) ph.autoFocus = false;
            fill = (lg - log10f(0.5f)) / (log10f(300.f) - log10f(0.5f));
            float shown = ph.autoFocus && ph.autoFocusDistance > 0.f ? ph.autoFocusDistance : ph.focusDistance;
            if (shown < 10.f) snprintf(b, sizeof(b), "%.1f m", shown);
            else snprintf(b, sizeof(b), "%.0f m", shown);
            return b;
        }
        case 7: {
            int idx = 0;
            float best = 1e9f;
            for (int i = 0; i < 8; i++)
                if (fabsf(kApertures[i] - ph.aperture) < best) { best = fabsf(kApertures[i] - ph.aperture); idx = i; }
            idx = Clamp(idx + d, 0, 7);
            ph.aperture = kApertures[idx];
            snprintf(b, sizeof(b), "f/%g", ph.aperture);
            return b;
        }
        case 8:
            ph.filter = (Clamp(ph.filter, 0, kFilterCount - 1) + d + kFilterCount) % kFilterCount;
            return kFilterNames[ph.filter];
        case 9:
            ph.filterStrength = Saturate(ph.filterStrength + d * 0.05f);
            fill = ph.filterStrength;
            snprintf(b, sizeof(b), "%.0f%%", ph.filterStrength * 100.f);
            return b;
        case 10:
            ph.exposure = Clamp(ph.exposure + d * 0.1f, -2.f, 2.f);
            if (fabsf(ph.exposure) < 0.01f) ph.exposure = 0.f;
            fill = (ph.exposure + 2.f) / 4.f;
            snprintf(b, sizeof(b), "%+.1f EV", ph.exposure);
            return b;
        case 11:
            ph.contrast = Clamp(ph.contrast + d * 0.05f, 0.5f, 1.5f);
            fill = (ph.contrast - 0.5f);
            snprintf(b, sizeof(b), "%.0f%%", ph.contrast * 100.f);
            return b;
        case 12:
            ph.saturation = Clamp(ph.saturation + d * 0.05f, 0.f, 2.f);
            fill = ph.saturation * 0.5f;
            snprintf(b, sizeof(b), "%.0f%%", ph.saturation * 100.f);
            return b;
        case 13:
            ph.temperature = Clamp(ph.temperature + d * 0.05f, -1.f, 1.f);
            if (fabsf(ph.temperature) < 0.01f) ph.temperature = 0.f;
            fill = (ph.temperature + 1.f) * 0.5f;
            snprintf(b, sizeof(b), "%+.0f", ph.temperature * 100.f);
            return b;
        case 14:
            ph.vignette = Saturate(ph.vignette + d * 0.05f);
            fill = ph.vignette;
            snprintf(b, sizeof(b), "%.0f%%", ph.vignette * 100.f);
            return b;
        case 15:
            ph.grain = Saturate(ph.grain + d * 0.05f);
            fill = ph.grain;
            snprintf(b, sizeof(b), "%.0f%%", ph.grain * 100.f);
            return b;
        case 16: if (d) ph.grid = 1 - Clamp(ph.grid, 0, 1); return ph.grid ? "Thirds" : "Off";
        case 17:
            ph.frame = (Clamp(ph.frame, 0, 3) + d + 4) % 4;
            return kFrameNames[ph.frame];
        default: return "";
    }
}

void resetPhotoLook(PhotoMode& ph) {
    PhotoMode def;
    ph.autoFocus = def.autoFocus;
    ph.dof = def.dof;
    ph.focusDistance = def.focusDistance;
    ph.aperture = def.aperture;
    ph.filter = 0;
    ph.filterStrength = def.filterStrength;
    ph.exposure = def.exposure;
    ph.contrast = def.contrast;
    ph.saturation = def.saturation;
    ph.temperature = def.temperature;
    ph.vignette = def.vignette;
    ph.grain = def.grain;
    ph.grid = def.grid;
    ph.frame = def.frame;
    ph.camRoll = 0.f;
}

// Free camera: fly with WASD / left stick, look with the mouse / right stick, Q/E or triggers for down / up
void flyCamera(PhoneState& st, const InputState& in, float dt) {
    PhotoMode& ph = st.photo;
    const GamepadState& p = in.pad;
    float look = 0.0024f;
    ph.camYaw -= in.mouseDelta.x * look * (ph.camFov / 0.87f);
    ph.camPitch -= in.mouseDelta.y * look * (ph.camFov / 0.87f);
    ph.camYaw -= p.rightStick.x * 1.7f * dt * (ph.camFov / 0.87f);
    ph.camPitch += p.rightStick.y * 1.3f * dt * (ph.camFov / 0.87f);
    ph.camPitch = Clamp(ph.camPitch, -1.45f, 1.45f);
    ph.camYaw = wrapAngle(ph.camYaw);
    vec3 fwd(-sinf(ph.camYaw) * cosf(ph.camPitch), cosf(ph.camYaw) * cosf(ph.camPitch), sinf(ph.camPitch));
    vec3 right(cosf(ph.camYaw), sinf(ph.camYaw), 0.f);
    vec3 mv(0.f);
    if (in.down(KEY_W)) mv += fwd;
    if (in.down(KEY_S)) mv -= fwd;
    if (in.down(KEY_D)) mv += right;
    if (in.down(KEY_A)) mv -= right;
    mv += fwd * p.leftStick.y + right * p.leftStick.x;
    if (in.down(KEY_E)) mv.z += 1.f;
    if (in.down(KEY_Q)) mv.z -= 1.f;
    mv.z += p.rightTrigger - p.leftTrigger;
    float speed = 4.f;
    if (in.down(KEY_SHIFT) || p.down(PAD_LTHUMB)) speed *= 3.f;
    if (in.down(KEY_CONTROL)) speed *= 0.3f;
    if (length2(mv) > 1.f) mv = normalize(mv);
    vec3 np = ph.camPos + mv * speed * Min(dt, 0.1f);
    // keep near the player, above the ground and the water
    vec3 d = np - st.playerPos;
    float maxR = 30.f;
    if (length(d) > maxR) np = st.playerPos + normalize(d) * maxR;
    if (World::gMap) {
        float gz = World::gMap->heightAt(np.x, np.y) + 0.35f;
        float wz = World::gMap->waterAt(np.x, np.y);
        if (wz > World::kNoWater + 1.f) gz = Max(gz, wz + 0.25f);
        np.z = Max(np.z, gz);
    }
    ph.camPos = np;
    // mouse wheel / bumpers zoom
    float zoom = in.wheelDelta + ((p.down(PAD_RB) ? 1.f : 0.f) - (p.down(PAD_LB) ? 1.f : 0.f)) * 2.5f * dt;
    if (zoom != 0.f) ph.camFov = Clamp(ph.camFov * powf(0.92f, zoom), 15.f * kDegToRad, 100.f * kDegToRad);
}

void drawPhotoFrameOverlay(int frame, const Layout& L, const PhoneState& st, float a) {
    float s = L.s;
    if (frame == 1) {   // polaroid: white border, thicker bottom with a caption
        float b = 40.f * s, bb = 150.f * s;
        u32 c = withAlpha(C(0.97f, 0.96f, 0.93f), a);
        rect(0, 0, L.W, b, c);
        rect(0, L.H - bb, L.W, bb, c);
        rect(0, 0, b, L.H, c);
        rect(L.W - b, 0, b, L.H, c);
        TextStyle ts = tstyle(FONT_HEADING, 44.f * s, withAlpha(C(0.16f, 0.16f, 0.22f), a), ALIGN_CENTER);
        ts.skew = 0.18f;
        std::string cap = "Porto Sol  -  Day " + std::to_string(st.day);
        text(L.W * 0.5f, L.H - bb * 0.62f - ts.size * 0.5f, cap.c_str(), ts);
    } else if (frame == 2) {   // film strip: black bands with sprocket holes and frame numbers
        float band = 64.f * s;
        rect(0, 0, L.W, band, withAlpha(C(0.02f, 0.02f, 0.02f), a));
        rect(0, L.H - band, L.W, band, withAlpha(C(0.02f, 0.02f, 0.02f), a));
        for (float x = 20.f * s; x < L.W; x += 58.f * s) {
            roundRect(x, 16.f * s, 30.f * s, 22.f * s, 4.f * s, withAlpha(C(0.85f, 0.82f, 0.74f), a));
            roundRect(x, L.H - 38.f * s, 30.f * s, 22.f * s, 4.f * s, withAlpha(C(0.85f, 0.82f, 0.74f), a));
        }
        TextStyle ts = tstyle(FONT_HEADING, 18.f * s, withAlpha(C(1.f, 0.62f, 0.2f), a));
        ts.tracking = 0.1f;
        text(40.f * s, 42.f * s, "NT-400  24", ts);
        text(L.W * 0.62f, L.H - 64.f * s + 42.f * s - 18.f * s, "24A      25", ts);
    } else if (frame == 3) {   // neon border
        float m = 26.f * s;
        roundRect(m, m, L.W - 2.f * m, L.H - 2.f * m, 26.f * s, 0, 6.f * s, withAlpha(C(1.f, 0.30f, 0.72f), 0.9f * a));
        roundRect(m + 10.f * s, m + 10.f * s, L.W - 2.f * m - 20.f * s, L.H - 2.f * m - 20.f * s, 18.f * s, 0, 3.f * s,
                  withAlpha(C(0.3f, 0.9f, 1.f), 0.9f * a));
        TextStyle ts = tstyle(FONT_TITLE, 60.f * s, withAlpha(C(1.f, 0.45f, 0.8f), a), ALIGN_RIGHT);
        ts.glow = 10.f * s;
        ts.glowColor = withAlpha(C(1.f, 0.2f, 0.7f), 0.8f * a);
        ts.skew = 0.14f;
        text(L.W - 70.f * s, L.H - 150.f * s, "Porto Sol", ts);
    }
}

void photoMode(PhoneState& st, const InputState& in, float dt, PhoneAction& act) {
    Layout L = layout();
    float s = L.s;
    PhotoMode& ph = st.photo;
    bool capture = I.captureStage == 1;
    Nav n = readNav(in, dt, false);
    if (!capture) {
        flyCamera(st, in, dt);
        if (in.pressed(KEY_H) || in.pad.pressed(PAD_Y)) I.photoUi = !I.photoUi;
        if (in.pressed(KEY_R) || in.pad.pressed(PAD_X)) resetPhotoLook(ph);
        if (n.up) I.photoRow = (I.photoRow + kPhotoRowCount - 1) % kPhotoRowCount;
        if (n.down) I.photoRow = (I.photoRow + 1) % kPhotoRowCount;
        int d = (n.left ? -1 : 0) + (n.right ? 1 : 0);
        if (d) {
            float fill;
            photoRowValue(ph, I.photoRow, d, fill);
        }
        if (in.pressed(KEY_SPACE) || in.pressed(KEY_ENTER) || in.pad.pressed(PAD_A)) {
            I.captureStage = 1;   // the next frame shows the bare photo and is captured
        }
        if (in.pressed(KEY_ESCAPE) || in.pressed(KEY_BACK) || in.pad.pressed(PAD_B)) {
            ph.active = false;
            act.type = PA_EXIT_PHOTO_MODE;
            go(SC_CAMERA, -1);
            return;
        }
    }
    // the graded frame (full screen)
    PhotoFx fx = fxFromPhoto(ph, I.time, true);
    photoEffect(0, 0, L.W, L.H, 0, 0, 1, 1, fx);
    drawPhotoFrameOverlay(ph.frame, L, st, 1.f);
    if (capture) return;
    if (ph.grid) {
        for (int k = 1; k < 3; k++) {
            rect(L.W * k / 3.f, 0, 1.f, L.H, withAlpha(kWhite, 0.35f));
            rect(0, L.H * k / 3.f, L.W, 1.f, withAlpha(kWhite, 0.35f));
        }
    }
    if (ph.dof) drawIcon(ICO_FOCUS, L.W * 0.5f, L.H * 0.5f, 54.f * s, withAlpha(kWhite, 0.75f), 1.f * s, C(0.f, 0.f, 0.f, 0.4f));
    if (!I.photoUi) {
        TextStyle hs = tstyle(FONT_BODY, 15.f * s, withAlpha(kWhite, 0.6f), ALIGN_RIGHT);
        hs.shadow = 1.5f * s;
        text(L.right, L.bottom - 20.f * s, n.pad ? "Y  Show interface" : "H  Show interface", hs);
        return;
    }
    // settings panel
    float px = L.left, py = L.top + 20.f * s, pw = 400.f * s, rowH = 36.f * s;
    float ph0 = 92.f * s + kPhotoRowCount * rowH + 14.f * s;
    roundRect(px + 3.f * s, py + 5.f * s, pw, ph0, 16.f * s, C(0.f, 0.f, 0.02f, 0.35f));
    backdrop(px, py, pw, ph0, 16.f * s, C(0.85f, 0.85f, 0.9f), C(0.03f, 0.04f, 0.12f, 0.74f), 0.5f);
    roundRect(px, py, pw, ph0, 16.f * s, 0, 1.2f * s, withAlpha(kWhite, 0.10f));
    roundRect(px, py, pw, 4.f * s, 2.f * s, kPink);
    drawIcon(ICO_APERTURE, px + 30.f * s, py + 36.f * s, 32.f * s, kPink);
    TextStyle ts = tstyle(FONT_HEADING, 28.f * s, kWhite);
    ts.tracking = 0.06f;
    text(px + 54.f * s, py + 18.f * s, "PHOTO MODE", ts);
    TextStyle ss = tstyle(FONT_BODY, 13.f * s, kTextDim);
    std::string sub = "Day " + std::to_string(st.day) + "  |  " + clockText(st.timeOfDay) + (ph.freeze ? "  |  Time frozen" : "");
    text(px + 56.f * s, py + 54.f * s, sub.c_str(), ss);
    float ry = py + 84.f * s;
    for (int i = 0; i < kPhotoRowCount; i++) {
        float y = ry + i * rowH;
        bool sel = i == I.photoRow;
        if (sel) {
            roundRectGradient(px + 8.f * s, y + 2.f * s, pw - 16.f * s, rowH - 4.f * s, 8.f * s, C(1.f, 0.30f, 0.64f, 0.9f), C(0.78f, 0.12f, 0.50f, 0.9f));
            roundRect(px + 8.f * s, y + 2.f * s, 3.f * s, rowH - 4.f * s, 1.5f * s, kWhite);
        }
        float fill;
        std::string val = photoRowValue(ph, i, 0, fill);
        bool dim = (i == 6 && ph.autoFocus && ph.autoFocusDistance > 0.f) || ((i == 5 || i == 6 || i == 7) && !ph.dof);
        TextStyle ls = tstyle(FONT_BODY, 15.5f * s, withAlpha(sel ? kWhite : kText, dim && !sel ? 0.5f : 1.f));
        text(px + 22.f * s, y + rowH * 0.5f - ls.size * 0.58f, kPhotoRows[i].label, ls);
        TextStyle vs = tstyle(FONT_HEADING, 16.f * s, withAlpha(sel ? kWhite : kTextDim, dim && !sel ? 0.5f : 1.f), ALIGN_RIGHT);
        float vx = px + pw - 22.f * s;
        if (sel) {
            drawIcon(ICO_CHEVRON, vx - 4.f * s, y + rowH * 0.5f, 14.f * s, kWhite);
            float vw = textWidth(val.c_str(), vs);
            drawIcon(ICO_CHEVRON, vx - vw - 26.f * s, y + rowH * 0.5f, 14.f * s, kWhite, 0.f, 0, kPi);
            vx -= 16.f * s;
        }
        text(vx, y + rowH * 0.5f - vs.size * 0.56f, val.c_str(), vs);
        if (fill >= 0.f) {
            float bw = 90.f * s, bx = px + pw - 22.f * s - bw - (sel ? 0.f : 0.f);
            (void)bx;
            float barX = px + 160.f * s, barW = pw - 160.f * s - 130.f * s;
            rect(barX, y + rowH * 0.5f - 1.f * s, barW, 2.f * s, withAlpha(kWhite, sel ? 0.35f : 0.15f));
            rect(barX, y + rowH * 0.5f - 1.f * s, barW * Saturate(fill), 2.f * s, sel ? kWhite : withAlpha(kCyan, 0.8f));
            circle(barX + barW * Saturate(fill), y + rowH * 0.5f, (sel ? 5.f : 3.5f) * s, sel ? kWhite : kCyan);
        }
    }
    // prompts
    PromptItem pi[] = {{"WASD", "LS", "Move"}, {"MOUSE", "RS", "Look"}, {"Q", "LT", "Down"}, {"E", "RT", "Up"},
                       {"WHEEL", "RB", "Zoom"}, {"UPDOWN", "DPADUD", "Setting"}, {"LEFTRIGHT", "DPADLR", "Adjust"},
                       {"R", "X", "Reset"}, {"H", "Y", "Hide"}, {"SPACE", "A", "Take photo"}, {"ESC", "B", "Exit"}};
    float bh = 26.f * s;
    backdrop(L.x0, L.H - 76.f * s, L.x1 - L.x0, 76.f * s, 0.f, C(0.8f, 0.8f, 0.85f), C(0.02f, 0.03f, 0.08f, 0.55f), 0.4f);
    drawPromptBar(L.right, L.H - 51.f * s, pi, 11, n.pad, bh, 1.f);
}

}  // namespace phone_ui

// ------------------------------------------------------------------------------------------------------------------
namespace uix {
bool phoneCoversBottomRight() { return phone_ui::g_phoneCovers; }
bool photoModeActive() { return phone_ui::g_photoModeOn; }
}  // namespace uix

namespace Phone {

const std::vector<std::string>& filterNames() {
    static std::vector<std::string> names;
    if (names.empty())
        for (int i = 0; i < phone_ui::kFilterCount; i++) names.push_back(phone_ui::kFilterNames[i]);
    return names;
}

bool isOpen() { return phone_ui::g_phoneCovers; }

void reset() {
    using namespace phone_ui;
    std::vector<int> gallery = I.gallery;   // photos of this session stay in the gallery
    int filter = I.quickFilter;
    I = Internal();
    I.gallery = gallery;
    I.quickFilter = filter;
    g_scrollShown = ScrollAnim();
    g_phoneCovers = false;
    g_photoModeOn = false;
}

PhoneAction update(PhoneState& st, const HudState& hud, const InputState& in, float dt) {
    using namespace phone_ui;
    PhoneAction act;
    ensureIcons();
    dt = Clamp(dt, 0.f, 0.1f);
    I.time += dt;
    tideTick(dt, st.timeOfDay, st.weather, st.owner);
    // toast from the game (quick save result, purchases...)
    if (!st.toast.empty()) {
        if (I.screen == SC_QUICKSAVE && I.saveState == 1) {
            I.saveState = 2;
            I.saveT = 0.f;
        }
        showToast(st.toast);
        st.toast.clear();
    }
    I.toastT = Max(0.f, I.toastT - dt);
    // calls: an incoming call opens the phone; the end of a call returns to where the player was
    if (st.call != CALL_NONE && I.lastCall == CALL_NONE) {
        if (I.screen != SC_CALL) I.callReturn = I.screen;
        if (!st.open) {
            st.open = true;
            I.autoOpened = true;
            I.callReturn = SC_HOME;
        }
        I.callButton = 1;
        go(SC_CALL, 1);
    }
    if (st.call == CALL_NONE && I.lastCall != CALL_NONE) {
        I.callEndT = 1.1f;
        I.callEndName = st.callName;
    }
    I.lastCall = st.call;
    if (I.screen == SC_CALL && st.call == CALL_NONE) {
        I.callEndT -= dt;
        if (I.callEndT <= 0.f) {
            if (I.autoOpened) {
                st.open = false;
                I.autoOpened = false;
                go(SC_HOME, -1);
            } else go(I.callReturn == SC_CALL ? SC_HOME : I.callReturn, -1);
        }
    }
    // Photo capture frame: only the graded photo is drawn (it covers the HUD drawn before it); the game saves the
    // back buffer after UI::endFrame.
    if (I.captureStage == 1) {
        g_phoneCovers = false;
        st.capturingInput = true;
        if (st.photo.active) photoMode(st, in, dt, act);
        else {
            Layout L = layout();
            PhotoFx fx;
            fx.filter = I.quickFilter;
            fx.vignette = 0.2f;
            fx.time = I.time;
            photoEffect(0, 0, L.W, L.H, 0, 0, 1, 1, fx);
        }
        int id = requestSnapshot();
        I.gallery.push_back(id);
        if (I.gallery.size() > 8) I.gallery.erase(I.gallery.begin());
        tidePostPlayerPhoto(id, st.photo.active ? st.photo.filter : I.quickFilter, vec2(st.playerPos.x, st.playerPos.y), st.timeOfDay);
        act.type = PA_TAKE_PHOTO;
        I.captureStage = 0;
        I.flashT = 0.35f;
        I.savedT = st.photo.active ? 2.2f : 0.f;
        if (!st.photo.active) showToast("Photo saved  |  posted to Tidegram");
        return act;
    }
    // Photo mode takes over the whole screen
    if (st.photo.active) {
        g_photoModeOn = true;
        g_phoneCovers = false;
        st.capturingInput = true;
        photoMode(st, in, dt, act);
        if (st.photo.active) {
            Layout L = layout();
            float s = L.s;
            if (I.flashT > 0.f && I.captureStage == 0) {
                rect(0, 0, L.W, L.H, withAlpha(kWhite, I.flashT / 0.35f * (uiOptions().reduceFlashing ? 0.2f : 0.85f)));
                I.flashT -= dt;
            }
            if (I.savedT > 0.f) {
                float k = Saturate(I.savedT / 0.3f) * Saturate((2.2f - I.savedT) / 0.2f);
                TextStyle ts = tstyle(FONT_HEADING, 20.f * s, withAlpha(kWhite, k), ALIGN_CENTER);
                ts.tracking = 0.06f;
                float w = 380.f * s, h = 44.f * s, x = L.W * 0.5f - w * 0.5f, y = L.top + 10.f * s;
                roundRect(x, y, w, h, h * 0.5f, C(0.03f, 0.04f, 0.1f, 0.8f * k));
                text(L.W * 0.5f, y + h * 0.5f - ts.size * 0.56f, "PHOTO SAVED  |  POSTED TO TIDEGRAM", ts);
                I.savedT -= dt;
            }
            return act;
        }
        // photo mode was left this frame: the phone comes back on the camera screen
        I.justOpened = true;
    }
    g_photoModeOn = false;
    // open / close animation
    if (st.open && !I.wasOpen) {
        I.justOpened = true;
        if (I.screen != SC_CALL) {
            I.screen = SC_HOME;
            I.screenT = 10.f;
        }
    }
    I.wasOpen = st.open;
    I.openAnim = approachExp(I.openAnim, st.open ? 1.f : 0.f, st.open ? 11.f : 14.f, dt);
    if (!st.open && I.openAnim < 0.01f) I.openAnim = 0.f;
    I.visible = I.openAnim > 0.001f;
    g_phoneCovers = I.visible && I.openAnim > 0.05f;
    st.capturingInput = st.open;
    if (!I.visible) return act;
    Layout L = layout();
    float slide = easeOutCubic(I.openAnim);
    PL p = phoneLayout(L, slide);
    float s = p.s;
    Nav n;
    if (st.open && !I.justOpened) n = readNav(in, dt);
    I.justOpened = false;
    I.screenT += dt;
    // body
    circleSoft(p.x + p.w * 0.5f, p.y + p.h * 0.55f, p.w * 0.75f, p.w * 0.6f, C(0.f, 0.f, 0.f, 0.35f));
    roundRectGradient(p.x, p.y, p.w, p.h, p.r, C(0.20f, 0.21f, 0.28f), C(0.08f, 0.08f, 0.12f), 1.6f * s, C(0.55f, 0.45f, 0.70f, 0.8f));
    rect(p.x + p.w - 1.f * s, p.y + 120.f * s, 3.5f * s, 60.f * s, C(0.30f, 0.30f, 0.38f));
    rect(p.x - 2.5f * s, p.y + 110.f * s, 3.5f * s, 36.f * s, C(0.30f, 0.30f, 0.38f));
    rect(p.x - 2.5f * s, p.y + 156.f * s, 3.5f * s, 36.f * s, C(0.30f, 0.30f, 0.38f));
    roundRect(p.sx - 1.f * s, p.sy - 1.f * s, p.sw + 2.f * s, p.sh + 2.f * s, p.sr + 1.f * s, C(0.01f, 0.01f, 0.02f));
    ClipState saved = getClip();
    setClipRoundRect(p.sx, p.sy, p.sw, p.sh, p.sr);
    // back navigation
    if (n.back && I.screen != SC_CALL) {
        bool consumed = true;
        switch (I.screen) {
            case SC_HOME:
                st.open = false;
                act.type = PA_CLOSED;
                break;
            case SC_THREAD: go(SC_MESSAGES, -1); break;
            case SC_GALLERY:
                if (I.galleryOpen < 0) go(SC_CAMERA, -1);
                else consumed = false;   // the gallery closes its enlarged photo itself
                break;
            case SC_QUICKSAVE:
                if (I.saveState != 1) go(SC_HOME, -1);
                break;
            default: go(SC_HOME, -1); break;
        }
        if (consumed) {
            n.back = false;
            n.confirm = false;
            n.up = n.down = n.left = n.right = false;
        }
    }
    // screen transition: the new screen slides in from the side it comes from while fading up
    float ta = easeOutCubic(I.screenT / 0.24f);
    float a = 0.35f + 0.65f * ta;
    PL pc = p;
    pc.sx += (1.f - ta) * 36.f * s * (float)I.dir;
    if (ta < 1.f) appBackground(p, 1.f);   // base under the sliding page
    Screen scr = I.screen;
    switch (scr) {
        case SC_HOME: homeScreen(st, pc, n, I.dir < 0 ? a : 1.f, dt, act); break;
        case SC_CONTACTS: contactsScreen(st, pc, n, a, dt, act); break;
        case SC_MESSAGES: messagesScreen(st, pc, n, a, dt, act); break;
        case SC_THREAD: threadScreen(st, pc, n, a, dt, act); break;
        case SC_TIDEGRAM: tidegramScreen(st, pc, n, a, dt, act); break;
        case SC_CAMERA: cameraScreen(st, pc, n, a, dt, act); break;
        case SC_GALLERY: galleryScreen(st, pc, n, a, dt); break;
        case SC_MAP: mapScreen(st, hud, pc, n, a, dt, act); break;
        case SC_QUICKSAVE: quickSaveScreen(st, pc, n, a, dt, act); break;
        case SC_LIST: listScreen(st, pc, n, a, dt, act); break;
        case SC_CALL: callScreen(st, pc, n, a, dt, act); break;
        default: break;
    }
    if (st.photo.active) {
        // entered photo mode this frame: the phone disappears from the next frame on
        setClip(saved);
        return act;
    }
    // status bar background for app screens
    if (scr != SC_HOME && scr != SC_CAMERA && scr != SC_MAP)
        gradientRect(p.sx, p.sy, p.sw, 38.f * s, C(0.02f, 0.02f, 0.06f, 0.55f), C(0.02f, 0.02f, 0.06f, 0.f));
    drawStatusBar(st, p, 1.f, true);
    // camera shutter flash on the screen
    if (I.flashT > 0.f) {
        rect(p.sx, p.sy, p.sw, p.sh, withAlpha(kWhite, I.flashT / 0.35f * (uiOptions().reduceFlashing ? 0.2f : 0.9f)));
        I.flashT -= dt;
    }
    // toast
    if (I.toastT > 0.f && !I.toast.empty()) {
        float k = Saturate(I.toastT / 0.25f) * Saturate((2.6f - I.toastT) / 0.2f);
        TextStyle ts = tstyle(FONT_BODY, 14.f * s, withAlpha(kWhite, k), ALIGN_CENTER);
        std::string t = fitText(I.toast, ts, p.sw - 60.f * s);
        float tw = textWidth(t.c_str(), ts) + 34.f * s, th = 34.f * s;
        float tx = p.sx + p.sw * 0.5f - tw * 0.5f, ty = p.bottom - th - (scr == SC_CAMERA ? 130.f : 18.f) * s;
        roundRect(tx, ty, tw, th, th * 0.5f, C(0.08f, 0.09f, 0.16f, 0.92f * k), 1.f * s, withAlpha(kWhite, 0.18f * k));
        text(p.sx + p.sw * 0.5f, ty + th * 0.5f - ts.size * 0.56f, t.c_str(), ts);
    }
    // home indicator
    capsule(p.sx + p.sw * 0.5f - 50.f * s, p.sy + p.sh - 10.f * s, p.sx + p.sw * 0.5f + 50.f * s, p.sy + p.sh - 10.f * s, 4.5f * s,
            withAlpha(kWhite, 0.75f));
    setClip(saved);
    return act;
}

}  // namespace Phone
}  // namespace UI
