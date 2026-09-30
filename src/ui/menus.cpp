// Front-end and pause menus: main menu, tabbed pause menu (map, brief, stats, settings, save, quit), full-screen
// map, settings pages, save/load slot lists with confirmation, quit dialogs and the loading screen. Immediate-mode:
// every frame processes keyboard / mouse / gamepad input and draws the active page over the live game frame.
#include "ui_internal.h"

namespace UI {
namespace menus_ui {

using namespace uix;

// ------------------------------------------------------------------------------------------------------------------
// Input
struct Nav {
    const InputState* in = nullptr;
    bool pad = false;
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false, tabL = false, tabR = false, btnX = false, btnY = false;
    bool start = false;   // gamepad START (closes the pause menu from any tab)
    bool pageUp = false, pageDown = false;
    bool click = false, release = false, rclick = false, mouseDown = false, mouseMoved = false;
    vec2 mouse;
    float wheel = 0.f;
    vec2 stick;
    float lt = 0.f, rt = 0.f;
};

enum DialogKind { DLG_NONE = 0, DLG_OVERWRITE, DLG_LOAD, DLG_QUIT_MENU, DLG_QUIT_GAME, DLG_NEW_GAME, DLG_BIND_CONFLICT };

struct Internal {
    MenuScreen lastScreen = MENU_NONE;
    MenuScreen root = MENU_NONE;
    float screenT = 0.f, openT = 0.f, tabT = 0.f;
    int lastTab = -1;
    vec2 lastMouse = vec2(-1.f, -1.f);
    bool mouseMode = false;
    // key repeat
    int vDir = 0, hDir = 0;
    float vTimer = 0.f, hTimer = 0.f;
    // list scroll
    float scroll = 0.f, scrollTarget = 0.f;
    // main menu highlight
    float hlY = -1.f;
    // settings
    int setCat = 0;
    bool setItemsFocus = false;
    int setCursor = 0;
    int dragSlider = -1;
    float setCatHl = -1.f, setRowHl = -1.f;
    float setScroll = 0.f, setScrollShown = 0.f;   // items list scroll (pixels)
    int bindCol = 0;                      // key bindings page: 0 primary, 1 secondary column
    bool bindCapture = false;             // waiting for a key
    bool bindCaptureFresh = false;        // the press that started the capture is ignored
    int bindAction = -1, bindSlot = 0;    // binding being captured
    int bindPendingKey = 0, bindOtherAction = -1, bindOtherSlot = 0;   // conflict dialog
    float slotHl = -1.f;                 // eased save/load slot highlight (row units)
    float briefScroll = 0.f, briefScrollShown = 0.f;
    bool briefScrollable = false;
    std::string briefSeen;
    float tabHlX = -1.f, tabHlW = 0.f;   // eased pause-tab highlight
    float lastHeaderT = -1.f;
    // dialog
    DialogKind dialog = DLG_NONE;
    bool dialogFresh = false;   // opened this frame: ignore the key press that opened it
    int dialogChoice = 1;
    float dialogT = 0.f;
    int dialogSlot = -1;
    // map
    bool mapOpen = false;
    vec2 mapCenter;
    float mapMpp = 6.f, mapMppTarget = 6.f;
    vec2 cursor;
    bool dragging = false;
    vec2 dragStart, dragLast;
    bool legend = true;
    vec4 legendRect;   // x, y, w, h of the legend panel last frame (clicks there do not set waypoints)
    float mapFade = 0.f;
    // loading
    float tipT = 0.f;
    int tipIndex = 0;
    std::string lastTip;
    // feedback
    float denyT = 10.f;
};

Internal I;

bool keyDown(const InputState& in, int k) { return in.keys[k]; }

Nav readInput(const InputState& in, float dt, bool wasdNav) {
    Nav n;
    n.in = &in;
    n.pad = in.lastInputWasPad;
    const GamepadState& p = in.pad;
    int v = 0, h = 0;
    if (keyDown(in, KEY_UP) || (wasdNav && keyDown(in, KEY_W)) || p.down(PAD_UP) || p.leftStick.y > 0.55f) v -= 1;
    if (keyDown(in, KEY_DOWN) || (wasdNav && keyDown(in, KEY_S)) || p.down(PAD_DOWN) || p.leftStick.y < -0.55f) v += 1;
    if (keyDown(in, KEY_LEFT) || (wasdNav && keyDown(in, KEY_A)) || p.down(PAD_LEFT) || p.leftStick.x < -0.55f) h -= 1;
    if (keyDown(in, KEY_RIGHT) || (wasdNav && keyDown(in, KEY_D)) || p.down(PAD_RIGHT) || p.leftStick.x > 0.55f) h += 1;
    auto rep = [&](int dir, int& lastDir, float& timer) {
        bool fire = false;
        if (dir != 0 && dir != lastDir) { fire = true; timer = 0.38f; }
        else if (dir != 0) {
            timer -= dt;
            if (timer <= 0.f) { fire = true; timer = 0.075f; }
        }
        lastDir = dir;
        return fire;
    };
    if (rep(v, I.vDir, I.vTimer)) { n.up = v < 0; n.down = v > 0; }
    if (rep(h, I.hDir, I.hTimer)) { n.left = h < 0; n.right = h > 0; }
    n.confirm = in.pressed(KEY_ENTER) || in.pressed(KEY_SPACE) || p.pressed(PAD_A);
    n.back = in.pressed(KEY_ESCAPE) || in.pressed(KEY_BACK) || p.pressed(PAD_B);
    n.start = p.pressed(PAD_START);
    n.tabL = in.pressed(KEY_Q) || p.pressed(PAD_LB);
    n.tabR = in.pressed(KEY_E) || p.pressed(PAD_RB);
    n.btnX = in.pressed(KEY_X) || in.pressed(KEY_DELETE) || p.pressed(PAD_X);
    n.btnY = in.pressed(KEY_HOME) || in.pressed(KEY_C) || p.pressed(PAD_Y);
    n.pageUp = in.pressed(KEY_PGUP);
    n.pageDown = in.pressed(KEY_PGDN);
    n.mouse = in.mousePos;
    n.click = in.pressed(KEY_MOUSE_LEFT);
    n.release = in.released(KEY_MOUSE_LEFT);
    n.mouseDown = in.down(KEY_MOUSE_LEFT);
    n.rclick = in.pressed(KEY_MOUSE_RIGHT);
    n.wheel = in.wheelDelta;
    n.mouseMoved = I.lastMouse.x >= 0.f && length(in.mousePos - I.lastMouse) > 0.5f;
    I.lastMouse = in.mousePos;
    if (n.mouseMoved || n.click || n.wheel != 0.f) I.mouseMode = true;
    if (n.up || n.down || n.left || n.right || n.confirm || n.tabL || n.tabR) I.mouseMode = false;
    if (n.pad) I.mouseMode = false;
    n.stick = p.leftStick;
    n.lt = p.leftTrigger;
    n.rt = p.rightTrigger;
    return n;
}

bool inRect(vec2 m, float x, float y, float w, float h) { return m.x >= x && m.x < x + w && m.y >= y && m.y < y + h; }

TextStyle style(FontId f, float size, u32 color, Align al = ALIGN_LEFT) {
    TextStyle st;
    st.font = f;
    st.size = size;
    st.color = color;
    st.align = al;
    return st;
}

// ------------------------------------------------------------------------------------------------------------------
// Shared drawing
void drawLogo(float x, float y, float size, float alpha, float t, bool centered) {
    float sc = size / 100.f;
    TextStyle a = style(FONT_TITLE, size, withAlpha(kPink, alpha));
    a.skew = 0.16f;
    a.tracking = 0.01f;
    TextStyle b = a;
    const char* w1 = "NEON";
    const char* w2 = "TIDE";
    float gap = size * 0.18f;
    float wa = textWidth(w1, a), wb = textWidth(w2, b);
    float total = wa + gap + wb;
    float x0 = centered ? x - total * 0.5f : x;
    // back glow
    setAdditive(true);
    circleSoft(x0 + wa * 0.5f, y + size * 0.55f, size * 0.9f, size * 1.1f, C(1.f, 0.15f, 0.55f, 0.22f * alpha));
    circleSoft(x0 + wa + gap + wb * 0.5f, y + size * 0.55f, size * 0.9f, size * 1.1f, C(0.1f, 0.7f, 1.f, 0.20f * alpha));
    setAdditive(false);
    // drop shadow
    TextStyle sh = a;
    sh.color = C(0.02f, 0.0f, 0.08f, 0.55f * alpha);
    text(x0 + 5.f * sc, y + 7.f * sc, w1, sh);
    text(x0 + wa + gap + 5.f * sc, y + 7.f * sc, w2, sh);
    // chrome-ish gradients with neon glow
    a.color = withAlpha(C(1.f, 0.62f, 0.86f), alpha);
    a.colorBottom = withAlpha(C(1.f, 0.10f, 0.50f), alpha);
    a.glow = size * 0.08f;
    a.glowColor = C(1.f, 0.2f, 0.6f, 0.55f * alpha);
    a.outline = Max(1.f, size * 0.018f);
    a.outlineColor = withAlpha(C(1.f, 0.9f, 0.97f), 0.9f * alpha);
    text(x0, y, w1, a);
    b.color = withAlpha(C(0.70f, 0.98f, 1.f), alpha);
    b.colorBottom = withAlpha(C(0.10f, 0.55f, 1.f), alpha);
    b.glow = size * 0.08f;
    b.glowColor = C(0.1f, 0.8f, 1.f, 0.5f * alpha);
    b.outline = a.outline;
    b.outlineColor = withAlpha(C(0.9f, 1.f, 1.f), 0.9f * alpha);
    text(x0 + wa + gap, y, w2, b);
    // tide wave underline
    std::vector<vec2> wave;
    float wy = y + size * 1.08f;
    for (int i = 0; i <= 64; i++) {
        float u = (float)i / 64.f;
        float xx = x0 + u * total;
        float yy = wy + sinf(u * kTwoPi * 2.f - t * 2.2f) * size * 0.035f * (0.3f + 0.7f * sinf(u * kPi));
        wave.push_back(vec2(xx, yy));
    }
    setAdditive(true);
    polyline(wave.data(), (int)wave.size(), size * 0.07f, C(0.2f, 0.85f, 1.f, 0.35f * alpha), false, size * 0.06f);
    setAdditive(false);
    polyline(wave.data(), (int)wave.size(), Max(1.5f, size * 0.022f), withAlpha(C(0.75f, 0.97f, 1.f), alpha));
    TextStyle tag = style(FONT_HEADING, size * 0.17f, withAlpha(kTextDim, alpha));
    tag.tracking = 0.48f;
    std::string tg = "STATE OF PALMERA";
    float tw = textWidth(tg.c_str(), tag);
    text(centered ? x - tw * 0.5f : x0 + 4.f * sc, y + size * 1.2f, tg.c_str(), tag);
}

void fullBackdrop(const Layout& L, float a, float sat, float darkness) {
    backdrop(0, 0, L.W, L.H, 0.f, C(0.8f, 0.8f, 0.85f, a), C(0.02f, 0.03f, 0.09f, darkness), sat);
}

void vignette(const Layout& L, float a) {
    float e = L.H * 0.35f;
    gradientRect(0, 0, L.W, e, C(0.f, 0.f, 0.03f, 0.55f * a), C(0.f, 0.f, 0.03f, 0.f));
    gradientRect(0, L.H - e, L.W, e, C(0.f, 0.f, 0.03f, 0.f), C(0.f, 0.f, 0.03f, 0.65f * a));
}

void panel(float x, float y, float w, float h, float a, float radius = 14.f) {
    Layout L = layout();
    float r = radius * L.s;
    roundRect(x + 3.f * L.s, y + 5.f * L.s, w, h, r, C(0.f, 0.f, 0.02f, 0.35f * a));
    backdrop(x, y, w, h, r, C(0.85f, 0.85f, 0.9f, a), C(0.03f, 0.04f, 0.12f, 0.72f), 0.55f);
    roundRect(x, y, w, h, r, 0, 1.2f * L.s, withAlpha(kWhite, 0.10f * a));
    gradientRect(x + r, y + 1.f, w - 2.f * r, 1.2f * L.s, withAlpha(kWhite, 0.12f * a), withAlpha(kWhite, 0.12f * a));
}

void footer(const Layout& L, const PromptItem* items, int n, bool pad, float a) {
    float h = 30.f * L.s;
    drawPromptBar(L.right, L.H - 40.f * L.s - h, items, n, pad, h, a);
}

// Animated selection bar (pink gradient pill) with a leading accent
void selectionBar(float x, float y, float w, float h, float a) {
    Layout L = layout();
    roundRectGradient(x, y, w, h, 6.f * L.s, C(1.f, 0.24f, 0.60f, 0.92f * a), C(0.78f, 0.10f, 0.52f, 0.92f * a));
    setAdditive(true);
    gradientRectH(x, y, w * 0.6f, h, C(1.f, 0.7f, 0.9f, 0.22f * a), C(1.f, 0.7f, 0.9f, 0.f));
    setAdditive(false);
    roundRect(x, y, 4.f * L.s, h, 2.f * L.s, withAlpha(kWhite, a));
}

// ------------------------------------------------------------------------------------------------------------------
// Dialog
struct DialogResult { bool yes = false, no = false; };

DialogResult drawDialog(const Layout& L, const Nav& n, const char* title, const char* msg, const char* yesLabel, const char* noLabel, float dt) {
    DialogResult r;
    bool fresh = I.dialogFresh;
    I.dialogFresh = false;
    I.dialogT += dt;
    float a = easeOutCubic(I.dialogT / 0.2f);
    float sc = L.s;
    rect(0, 0, L.W, L.H, C(0.f, 0.f, 0.02f, 0.55f * a));
    float w = 720.f * sc, h = 300.f * sc;
    float x = (L.W - w) * 0.5f, y = (L.H - h) * 0.5f + (1.f - a) * 20.f * sc;
    panel(x, y, w, h, a, 18.f);
    roundRect(x, y, w, 5.f * sc, 2.5f * sc, withAlpha(kPink, a));
    TextStyle ts = style(FONT_HEADING, 40.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
    ts.tracking = 0.04f;
    text(x + w * 0.5f, y + 34.f * sc, title, ts);
    TextStyle ms = style(FONT_BODY, 23.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
    RichOpts ro;
    ro.alpha = a;
    ro.pad = n.pad;
    richDraw(x + w * 0.5f, y + 98.f * sc, msg, ms, w - 80.f * sc, ro);
    float bw = 240.f * sc, bh = 58.f * sc, by = y + h - bh - 30.f * sc;
    float bx[2] = {x + w * 0.5f - bw - 12.f * sc, x + w * 0.5f + 12.f * sc};
    const char* labels[2] = {yesLabel, noLabel};
    if (!fresh && (n.left || n.right)) I.dialogChoice = 1 - I.dialogChoice;
    for (int i = 0; i < 2; i++) {
        bool hov = inRect(n.mouse, bx[i], by, bw, bh);
        if (hov && n.mouseMoved) I.dialogChoice = i;
        bool sel = I.dialogChoice == i;
        if (sel) selectionBar(bx[i], by, bw, bh, a);
        else roundRect(bx[i], by, bw, bh, 6.f * sc, C(1.f, 1.f, 1.f, 0.07f * a), 1.f * sc, withAlpha(kWhite, 0.15f * a));
        TextStyle bs = style(FONT_HEADING, 28.f * sc, withAlpha(sel ? kWhite : kTextDim, a), ALIGN_CENTER);
        bs.tracking = 0.06f;
        std::string lab = upper(labels[i]);
        text(bx[i] + bw * 0.5f, by + bh * 0.5f - bs.size * 0.55f, lab.c_str(), bs);
        if (!fresh && n.click && hov) {
            if (i == 0) r.yes = true;
            else r.no = true;
        }
    }
    // input prompts under the panel
    {
        PromptItem pi[] = {{"LEFTRIGHT", "DPADLR", "Choose"}, {"ENTER", "A", "Confirm"}, {"ESC", "B", "Cancel"}};
        drawPromptBar(x + w, y + h + 18.f * sc, pi, 3, n.pad, 28.f * sc, a);
    }
    if (fresh) return r;
    if (n.confirm) {
        if (I.dialogChoice == 0) r.yes = true;
        else r.no = true;
    }
    if (n.back) r.no = true;
    return r;
}

void openDialog(DialogKind k, int slot, int defaultChoice = 1) {
    I.dialog = k;
    I.dialogSlot = slot;
    I.dialogChoice = defaultChoice;
    I.dialogT = 0.f;
    I.dialogFresh = true;
}

// ------------------------------------------------------------------------------------------------------------------
// Loading screen art: synthwave sunset over Porto Sol with palms
u32 skyColorAt(float t) {
    // t: 0 top .. 1 horizon
    vec3 c0(0.07f, 0.03f, 0.19f), c1(0.33f, 0.08f, 0.38f), c2(0.95f, 0.30f, 0.42f), c3(1.f, 0.62f, 0.38f);
    vec3 c;
    if (t < 0.45f) c = lerp(c0, c1, t / 0.45f);
    else if (t < 0.8f) c = lerp(c1, c2, (t - 0.45f) / 0.35f);
    else c = lerp(c2, c3, (t - 0.8f) / 0.2f);
    return C(c.x, c.y, c.z);
}

void drawPalm(vec2 base, float height, float lean, float t, float phase, u32 col) {
    // trunk: tapered curve built as one polygon (no seams), subtle ring marks
    const int N = 18;
    vec2 pts[N + 1];
    float sway = sinf(t * 0.9f + phase) * height * 0.012f;
    for (int i = 0; i <= N; i++) {
        float u = (float)i / N;
        pts[i] = base + vec2(lean * u * u * height + sway * u * u, -u * height);
    }
    vec2 trunk[(N + 1) * 2];
    for (int i = 0; i <= N; i++) {
        float w = height * Lerp(0.034f, 0.016f, (float)i / N);
        vec2 d = normalize(i < N ? pts[i + 1] - pts[i] : pts[i] - pts[i - 1]), nn = perp(d);
        trunk[i] = pts[i] + nn * w;
        trunk[(N + 1) * 2 - 1 - i] = pts[i] - nn * w;
    }
    polygon(trunk, (N + 1) * 2, col);
    u32 ring = lerpColor(col, C(0.35f, 0.12f, 0.35f), 0.35f);
    for (int i = 2; i < N; i += 2) {
        float w = height * Lerp(0.034f, 0.016f, (float)i / N) * 0.85f;
        vec2 d = normalize(pts[i + 1] - pts[i]), nn = perp(d);
        capsule(pts[i].x - nn.x * w, pts[i].y - nn.y * w, pts[i].x + nn.x * w, pts[i].y + nn.y * w, Max(1.f, height * 0.003f), ring);
    }
    vec2 top = pts[N];
    // fronds: arching spine with leaflets hanging from both sides (gravity), tips taper into the last leaflets
    const int F = 10;
    for (int f = 0; f < F; f++) {
        float fa = (float)f / (F - 1);
        float ang = Lerp(-kPi - 0.35f, 0.35f, fa) + sinf(t * 1.1f + phase + f * 1.7f) * 0.035f;
        float side = fabsf(fa - 0.5f) * 2.f;   // 0 upward fronds .. 1 sideways fronds
        float len = height * (0.27f + 0.06f * sinf(f * 2.3f + phase * 3.f)) * (1.05f - 0.25f * (1.f - side));
        vec2 dir(cosf(ang), sinf(ang) * 0.8f);
        float droop = Lerp(0.30f, 0.62f, side);
        const int S = 20;
        vec2 spine[S + 1];
        for (int k = 0; k <= S; k++) {
            float u = (float)k / S;
            spine[k] = top + dir * (len * u) + vec2(0.f, len * droop * u * u);
        }
        for (int k = 0; k < S; k++) {
            float u = (float)k / S;
            vec2 a = spine[k], b = spine[k + 1];
            float w = height * 0.007f * (1.f - u * 0.85f);
            capsule(a.x, a.y, b.x, b.y, w * 2.f, col);
            vec2 d = normalize(b - a), nn = perp(d);
            float leaf = height * (0.062f * sinf((0.1f + u * 0.85f) * kPi) + 0.01f);
            vec2 mid = (a + b) * 0.5f, halfSeg = (b - a) * 0.32f;
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 hang = normalize(nn * (float)sd * 0.6f + d * 0.5f + vec2(0.f, 0.55f));
                vec2 tip = mid + hang * leaf;
                triangle(mid - halfSeg, mid + halfSeg, tip, col);
            }
        }
    }
    for (int k = 0; k < 3; k++) circle(top.x + (k - 1) * height * 0.018f, top.y + height * 0.025f, height * 0.016f, col);
}

void drawLoadingArt(const Layout& L, float t) {
    float W = L.W, H = L.H;
    float horizon = H * 0.64f;
    // sky
    const int bands = 24;
    for (int i = 0; i < bands; i++) {
        float y0 = horizon * i / bands, y1 = horizon * (i + 1) / bands;
        gradientRect(0, y0, W, y1 - y0 + 1.f, skyColorAt((float)i / bands), skyColorAt((float)(i + 1) / bands));
    }
    // stars
    Rng rng(0xC0FFEEu);
    for (int i = 0; i < 140; i++) {
        float x = rng.f() * W, y = rng.f() * horizon * 0.55f;
        float tw = 0.5f + 0.5f * sinf(t * (1.f + rng.f() * 2.f) + i);
        circle(x, y, (0.6f + rng.f() * 1.2f) * L.s, C(1.f, 0.95f, 1.f, (0.25f + 0.5f * rng.f()) * tw * (1.f - y / (horizon * 0.55f))));
    }
    // sun with synthwave stripes
    vec2 sc(W * 0.6f, horizon - H * 0.13f);
    float sr = H * 0.21f;
    setAdditive(true);
    circleSoft(sc.x, sc.y, sr * 1.2f, sr * 1.6f, C(1.f, 0.35f, 0.45f, 0.35f));
    setAdditive(false);
    {
        // sun disc: one rounded quad with a vertical gradient (radius = half size makes it a circle)
        roundRectGradient(sc.x - sr, sc.y - sr, 2.f * sr, 2.f * sr, sr, C(1.f, 0.93f, 0.45f), C(1.f, 0.25f, 0.55f));
        // stripes cut in the lower half, animated downward
        for (int k = 0; k < 8; k++) {
            float ph = fmodf(t * 0.06f + k / 8.f, 1.f);
            float yy = sc.y + sr * (-0.3f + ph * 1.3f);
            float th = sr * (0.012f + 0.075f * ph);
            float tt = Saturate((yy - 0.f) / horizon);
            ClipState pc = getClip();
            setClipCircle(sc.x, sc.y, sr + 1.f);
            rect(sc.x - sr - 2.f, yy - th * 0.5f, 2.f * sr + 4.f, th, lerpColor(skyColorAt(tt), C(0.25f, 0.05f, 0.30f), 0.35f));
            setClip(pc);
        }
    }
    // distant skyline
    Rng br(0x5EA5u);
    float x = -20.f * L.s;
    u32 farCol = C(0.20f, 0.07f, 0.26f), nearCol = C(0.09f, 0.03f, 0.15f);
    while (x < W) {
        float w = (18.f + br.f() * 40.f) * L.s;
        float centerBoost = 1.f - Saturate(fabsf(x - W * 0.35f) / (W * 0.5f));
        float h = (30.f + br.f() * 70.f + centerBoost * centerBoost * br.f() * 190.f) * L.s;
        rect(x, horizon - h, w, h + 1.f, farCol);
        x += w + br.f() * 6.f * L.s;
    }
    x = -10.f * L.s;
    Rng br2(0xB10Cu);
    while (x < W) {
        float w = (22.f + br2.f() * 46.f) * L.s;
        float centerBoost = 1.f - Saturate(fabsf(x - W * 0.3f) / (W * 0.45f));
        float h = (20.f + br2.f() * 50.f + centerBoost * centerBoost * br2.f() * 260.f) * L.s;
        rect(x, horizon - h, w, h + 1.f, nearCol);
        if (h > 120.f * L.s && br2.chance(0.4f)) rect(x + w * 0.45f, horizon - h - 26.f * L.s, 2.f * L.s, 26.f * L.s, nearCol);
        // windows
        int cols = (int)(w / (7.f * L.s)), rows = (int)(h / (9.f * L.s));
        for (int r = 1; r < rows; r++)
            for (int c = 1; c < cols; c++) {
                u32 hh = hash3i((int)x, r, c);
                if ((hh & 7) != 0) continue;
                float blink = ((hh >> 8) & 31) == 0 ? (0.5f + 0.5f * sinf(t * 3.f + (float)hh)) : 1.f;
                u32 wc = (hh >> 4) % 3 == 0 ? C(1.f, 0.35f, 0.7f, 0.85f * blink) : (hh >> 4) % 3 == 1 ? C(0.4f, 0.9f, 1.f, 0.8f * blink) : C(1.f, 0.85f, 0.5f, 0.8f * blink);
                rect(x + c * 7.f * L.s, horizon - h + r * 9.f * L.s, 2.2f * L.s, 3.f * L.s, wc);
            }
        x += w + br2.f() * 4.f * L.s;
    }
    // sea
    gradientRect(0, horizon, W, H - horizon, C(0.20f, 0.06f, 0.26f), C(0.03f, 0.02f, 0.08f));
    // sun reflection glints
    Rng gr(0x51A7u);
    for (int i = 0; i < 46; i++) {
        float u = gr.f();
        float yy = horizon + (H - horizon) * (u * u);
        float spread = (0.08f + u * 0.25f) * W * 0.25f;
        float xx = sc.x + (gr.f() - 0.5f) * spread * 2.f + sinf(t * 0.8f + i) * 6.f * L.s;
        float len = (20.f + gr.f() * 90.f) * L.s * (0.5f + u);
        float al = (0.35f + 0.5f * gr.f()) * (1.f - u * 0.6f) * (0.6f + 0.4f * sinf(t * 2.f + i * 1.7f));
        u32 col = gr.chance(0.5f) ? C(1.f, 0.55f, 0.55f, al) : C(1.f, 0.82f, 0.5f, al);
        capsule(xx - len * 0.5f, yy, xx + len * 0.5f, yy, (1.2f + u * 2.5f) * L.s, col);
    }
    // horizon haze line
    setAdditive(true);
    gradientRect(0, horizon - 10.f * L.s, W, 10.f * L.s, C(1.f, 0.5f, 0.5f, 0.f), C(1.f, 0.5f, 0.5f, 0.35f));
    gradientRect(0, horizon, W, 14.f * L.s, C(1.f, 0.5f, 0.5f, 0.3f), C(1.f, 0.5f, 0.5f, 0.f));
    setAdditive(false);
    // palms (foreground silhouettes)
    u32 palm = C(0.035f, 0.01f, 0.06f);
    drawPalm(vec2(W * 0.04f, H * 1.03f), H * 0.6f, 0.36f, t, 0.3f, palm);
    drawPalm(vec2(W * 0.15f, H * 1.06f), H * 0.42f, 0.55f, t, 1.7f, palm);
    drawPalm(vec2(W * 0.95f, H * 1.03f), H * 0.72f, -0.3f, t, 2.9f, palm);
    // foreground beach
    std::vector<vec2> dune;
    dune.push_back(vec2(0, H));
    for (int i = 0; i <= 24; i++) {
        float u = (float)i / 24.f;
        dune.push_back(vec2(u * W, H - (22.f + 18.f * sinf(u * 5.f + 0.6f) + 30.f * (1.f - u)) * L.s));
    }
    dune.push_back(vec2(W, H));
    polygon(dune.data(), (int)dune.size(), palm);
}

const char* kTips[] = {
    "Hold ~i:TAB|LB~ to open the weapon wheel. Time slows down while you choose.",
    "Police lose track of you faster outside the ~r~search area~s~. Break line of sight and lie low.",
    "Set a waypoint on the ~p~map~s~ and follow the GPS route on the radar.",
    "Armor absorbs most of the damage from gunfire. Pick it up at ~b~gun stores~s~.",
    "Radio stations keep playing while you are on foot. Change them with ~i:R|LEFT~.",
    "The Overseas Highway links Porto Sol with the Coral Keys and Key Solano.",
    "Airboats are the fastest way across The Sawgrass. Watch out for the gators.",
    "Save your progress at any ~g~safehouse~s~ bed.",
    "Health regenerates slowly up to half. Visit a hospital or eat to heal fully.",
    "Stunt jumps are marked on the map. Land them for a cash bonus.",
};

void drawLoading(MenuState& st, const Layout& L, float dt, float t) {
    drawLoadingArt(L, t);
    float sc = L.s;
    // logo top-left
    drawLogo(L.left + 10.f * sc, L.top + 30.f * sc, 110.f * sc, 1.f, t, false);
    // progress + tip panel
    I.tipT += dt;
    int nt = (int)ARRAY_COUNT(kTips);
    if (st.loadingTip.empty() && I.tipT > 7.f) I.tipIndex = (I.tipIndex + 1) % nt;
    std::string tip = st.loadingTip.empty() ? kTips[I.tipIndex] : st.loadingTip;
    if (tip != I.lastTip) {
        I.lastTip = tip;
        I.tipT = 0.f;
    }
    float tipA = Saturate(I.tipT / 0.5f) * (st.loadingTip.empty() ? 1.f - SmoothStep(6.4f, 7.f, I.tipT) : 1.f);
    float pw = 620.f * sc;
    float px = L.right - pw, py = L.bottom - 150.f * sc;
    roundRect(px, py, pw, 120.f * sc, 14.f * sc, C(0.02f, 0.01f, 0.06f, 0.72f));
    roundRect(px, py, pw, 120.f * sc, 14.f * sc, 0, 1.f * sc, withAlpha(kWhite, 0.1f));
    TextStyle hs = style(FONT_HEADING, 20.f * sc, withAlpha(kPink, 1.f));
    hs.tracking = 0.2f;
    text(px + 22.f * sc, py + 16.f * sc, "TIP", hs);
    TextStyle ts = style(FONT_BODY, 21.f * sc, withAlpha(kText, tipA));
    RichOpts ro;
    ro.alpha = tipA;
    ro.pad = false;
    richDraw(px + 22.f * sc, py + 44.f * sc, tip.c_str(), ts, pw - 44.f * sc, ro);
    // progress bar
    float p = Saturate(st.loadingProgress);
    float bx = L.left, by = L.bottom - 16.f * sc, bw = L.right - L.left, bh = 6.f * sc;
    roundRect(bx, by, bw, bh, bh * 0.5f, C(1.f, 1.f, 1.f, 0.12f));
    if (p > 0.f) {
        roundRectGradient(bx, by, Max(bh, bw * p), bh, bh * 0.5f, kPink, C(0.8f, 0.2f, 1.f));
        setAdditive(true);
        circleSoft(bx + bw * p, by + bh * 0.5f, 10.f * sc, 20.f * sc, C(1.f, 0.4f, 0.8f, 0.6f));
        setAdditive(false);
    }
    TextStyle ps = style(FONT_HEADING, 22.f * sc, kWhite, ALIGN_LEFT);
    ps.tracking = 0.14f;
    int dots = (int)(t * 2.5f) % 4;
    std::string lt = std::string("LOADING") + std::string((size_t)dots, '.');
    text(bx, by - 34.f * sc, lt.c_str(), ps);
    ps.align = ALIGN_RIGHT;
    text(bx + bw, by - 34.f * sc, StrFormat("%d%%", (int)(p * 100.f + 0.5f)).c_str(), ps);
    // spinner
    float ang = t * 5.f;
    arc(L.left + 170.f * sc, by - 22.f * sc, 9.f * sc, 3.f * sc, ang, 1.1f, withAlpha(kCyan, 0.9f));
}

// ------------------------------------------------------------------------------------------------------------------
// Main menu
struct MainItem { const char* label; int id; };

MenuAction drawMain(MenuState& st, const Layout& L, const Nav& n, float dt, float t) {
    MenuAction act;
    float sc = L.s;
    float a = easeOutCubic(I.screenT / 0.5f);
    fullBackdrop(L, 1.f, 0.55f, 0.35f);
    gradientRectH(0, 0, L.W * 0.62f, L.H, C(0.02f, 0.02f, 0.07f, 0.88f), C(0.02f, 0.02f, 0.07f, 0.f));
    vignette(L, 1.f);
    // decorative sun disc behind the logo
    setAdditive(true);
    circleSoft(L.left + 330.f * sc, L.H * 0.25f, 230.f * sc, 260.f * sc, C(1.f, 0.25f, 0.5f, 0.10f * a));
    setAdditive(false);
    drawLogo(L.left + 6.f * sc, L.H * 0.13f + (1.f - a) * -20.f * sc, 136.f * sc, a, t, false);
    std::vector<MainItem> items;
    items.push_back({"New Game", 0});
    if (st.canContinue) items.push_back({"Continue", 1});
    items.push_back({"Load Game", 2});
    items.push_back({"Settings", 3});
    items.push_back({"Quit", 4});
    int count = (int)items.size();
    if (I.dialog == DLG_NONE) {
        if (n.up) st.cursor = (st.cursor + count - 1) % count;
        if (n.down) st.cursor = (st.cursor + 1) % count;
    }
    st.cursor = Clamp(st.cursor, 0, count - 1);
    float x = L.left, y0 = L.H * 0.47f, ih = 60.f * sc, iw = 440.f * sc;
    int activate = -1;
    for (int i = 0; i < count; i++) {
        float y = y0 + i * (ih + 8.f * sc);
        if (I.dialog == DLG_NONE && inRect(n.mouse, x, y, iw, ih)) {
            if (n.mouseMoved) st.cursor = i;
            if (n.click) activate = i;
        }
    }
    float targetY = y0 + st.cursor * (ih + 8.f * sc);
    if (I.hlY < 0.f) I.hlY = targetY;
    I.hlY = approachExp(I.hlY, targetY, 18.f, dt);
    selectionBar(x, I.hlY, iw, ih, a);
    for (int i = 0; i < count; i++) {
        float y = y0 + i * (ih + 8.f * sc);
        float ia = easeOutCubic((I.screenT - 0.08f * i) / 0.4f);
        bool sel = i == st.cursor;
        TextStyle ts = style(FONT_HEADING, 36.f * sc, withAlpha(sel ? kWhite : kTextDim, ia), ALIGN_LEFT);
        ts.tracking = 0.08f;
        ts.shadow = sel ? 0.f : 1.5f * sc;
        std::string lab = upper(items[i].label);
        text(x + 30.f * sc + (1.f - ia) * -30.f * sc, y + ih * 0.5f - ts.size * 0.56f, lab.c_str(), ts);
        if (sel) drawIcon(ICO_CHEVRON, x + iw - 30.f * sc, y + ih * 0.5f, 26.f * sc, withAlpha(kWhite, ia));
    }
    if (I.dialog == DLG_NONE && n.confirm) activate = st.cursor;
    if (activate >= 0) {
        switch (items[activate].id) {
        case 0: act.type = MA_NEW_GAME; break;
        case 1: act.type = MA_CONTINUE; break;
        case 2: st.prevScreen = MENU_MAIN; st.screen = MENU_LOAD; st.cursor = 0; break;
        case 3: st.prevScreen = MENU_MAIN; st.screen = MENU_SETTINGS; break;
        case 4: openDialog(DLG_QUIT_GAME, -1); break;
        }
    }
    if (I.dialog == DLG_NONE && n.back) openDialog(DLG_QUIT_GAME, -1);
    // footer
    TextStyle fs = style(FONT_BODY, 16.f * sc, withAlpha(kTextMute, a));
    text(L.left, L.H - 58.f * sc, "NEON TIDE  -  A story of Porto Sol, Palmera.  All characters and places are fictional.", fs);
    PromptItem pi[] = {{"ENTER", "A", "Select"}, {"ESC", "B", "Quit"}};
    footer(L, pi, 2, n.pad, a);
    return act;
}

// ------------------------------------------------------------------------------------------------------------------
// Pause header + tabs
enum PauseTab { PT_MAP = 0, PT_BRIEF, PT_STATS, PT_SETTINGS, PT_SAVE, PT_QUIT, PT_COUNT };
const char* kTabNames[PT_COUNT] = {"Map", "Brief", "Stats", "Settings", "Save", "Quit"};
const int kTabIcons[PT_COUNT] = {ICO_MAP, ICO_BRIEF, ICO_STATS, ICO_GEAR, ICO_SAVE, ICO_POWER};

MenuScreen tabScreen(int tab) {
    switch (tab) {
    case PT_MAP: return MENU_MAP;
    case PT_BRIEF: return MENU_BRIEF;
    case PT_STATS: return MENU_STATS;
    case PT_SETTINGS: return MENU_SETTINGS;
    case PT_SAVE: return MENU_SAVE;
    default: return MENU_PAUSE;
    }
}

void visibleTabs(const MenuState& st, std::vector<int>& tabs) {
    tabs.clear();
    for (int t = 0; t < PT_COUNT; t++) {
        if (t == PT_SAVE && !st.canSave) continue;
        tabs.push_back(t);
    }
}

// Returns the new tab if clicked / switched.
int drawPauseHeader(MenuState& st, const Layout& L, const Nav& n, int tab, float a, float t) {
    float sc = L.s;
    gradientRect(0, 0, L.W, 230.f * sc, C(0.01f, 0.015f, 0.05f, 0.92f * a), C(0.01f, 0.015f, 0.05f, 0.f));
    drawLogo(L.left, L.top - 6.f * sc, 46.f * sc, a, t, false);
    // right: time, day, money
    TextStyle rs = style(FONT_HEADING, 26.f * sc, withAlpha(kWhite, a), ALIGN_RIGHT);
    rs.shadow = 1.5f * sc;
    std::string line1 = StrFormat("DAY %d   %s", st.day, fmtClock(st.timeOfDay).c_str());
    if (!st.playerName.empty()) line1 = upper(st.playerName) + "   " + line1;
    text(L.right, L.top - 4.f * sc, line1.c_str(), rs);
    TextStyle ms = style(FONT_HEADING, 30.f * sc, withAlpha(kGreenMoney, a), ALIGN_RIGHT);
    ms.shadow = 1.5f * sc;
    std::string money = fmtMoney(st.money);
    text(L.right, L.top + 26.f * sc, money.c_str(), ms);
    // tabs
    std::vector<int> tabs;
    visibleTabs(st, tabs);
    float ty = L.top + 78.f * sc, th = 46.f * sc;
    float x = L.left;
    int newTab = tab;
    bool pad = n.pad;
    float promptH = 26.f * sc;
    float lbW = promptWidth(pad ? "LB" : "Q", pad, promptH);
    drawPrompt(x, ty + (th - promptH) * 0.5f, pad ? "LB" : "Q", pad, promptH, a);
    x += lbW + 14.f * sc;
    // tab rects first: backgrounds, then the highlight pill gliding to the active tab, then icons and labels
    float dt = I.lastHeaderT < 0.f ? 1.f : Clamp(t - I.lastHeaderT, 0.f, 1.f);
    bool snap = dt > 0.25f || I.tabHlX < 0.f;
    I.lastHeaderT = t;
    struct TabRect { float x, w; };
    TabRect rects[PT_COUNT];
    TextStyle ts = style(FONT_HEADING, 25.f * sc, kWhite, ALIGN_LEFT);
    ts.tracking = 0.08f;
    for (int i = 0; i < (int)tabs.size(); i++) {
        std::string lab = upper(kTabNames[tabs[i]]);
        rects[i].x = x;
        rects[i].w = textWidth(lab.c_str(), ts) + 70.f * sc;
        x += rects[i].w + 10.f * sc;
    }
    for (int i = 0; i < (int)tabs.size(); i++) {
        if (tabs[i] == tab) {
            if (snap) {
                I.tabHlX = rects[i].x;
                I.tabHlW = rects[i].w;
            } else {
                I.tabHlX = approachExp(I.tabHlX, rects[i].x, 16.f, dt);
                I.tabHlW = approachExp(I.tabHlW, rects[i].w, 16.f, dt);
            }
            continue;
        }
        bool hov = inRect(n.mouse, rects[i].x, ty, rects[i].w, th);
        roundRect(rects[i].x, ty, rects[i].w, th, 8.f * sc, C(0.06f, 0.08f, 0.18f, (hov ? 0.9f : 0.7f) * a), 1.f * sc,
                  withAlpha(kWhite, (hov ? 0.25f : 0.10f) * a));
    }
    roundRectGradient(I.tabHlX, ty, I.tabHlW, th, 8.f * sc, C(1.f, 0.24f, 0.60f, 0.95f * a), C(0.75f, 0.10f, 0.50f, 0.95f * a));
    setAdditive(true);
    gradientRectH(I.tabHlX, ty, I.tabHlW * 0.6f, th, C(1.f, 0.7f, 0.9f, 0.16f * a), C(1.f, 0.7f, 0.9f, 0.f));
    setAdditive(false);
    for (int i = 0; i < (int)tabs.size(); i++) {
        int tb = tabs[i];
        std::string lab = upper(kTabNames[tb]);
        bool active = tb == tab;
        bool hov = inRect(n.mouse, rects[i].x, ty, rects[i].w, th);
        // the label brightens as the pill arrives
        float cover = Saturate(1.f - fabsf(I.tabHlX - rects[i].x) / Max(rects[i].w * 0.6f, 1.f));
        u32 lc = lerpColor(kTextDim, kWhite, active ? Max(cover, 0.6f) : cover * 0.8f);
        drawIcon(kTabIcons[tb], rects[i].x + 24.f * sc, ty + th * 0.5f, 24.f * sc, withAlpha(lc, a));
        ts.color = withAlpha(lc, a);
        text(rects[i].x + 44.f * sc, ty + th * 0.5f - ts.size * 0.56f, lab.c_str(), ts);
        if (hov && n.click && I.dialog == DLG_NONE) newTab = tb;
    }
    drawPrompt(x + 4.f * sc, ty + (th - promptH) * 0.5f, pad ? "RB" : "E", pad, promptH, a);
    if (I.dialog == DLG_NONE) {
        int idx = 0;
        for (int i = 0; i < (int)tabs.size(); i++)
            if (tabs[i] == tab) idx = i;
        if (n.tabL) newTab = tabs[(idx + (int)tabs.size() - 1) % tabs.size()];
        if (n.tabR) newTab = tabs[(idx + 1) % tabs.size()];
    }
    return newTab;
}

// ------------------------------------------------------------------------------------------------------------------
// Full-screen map
float mapMinMpp() { return 0.9f; }
float mapMaxMpp(const Layout& L) {
    vec2 mn, mx;
    mapLandBounds(mn, mx);
    float spanX = (mx.x - mn.x) / (L.W * 0.92f), spanY = (mx.y - mn.y) / (L.H * 0.78f);
    return Max(spanX, spanY);
}

MenuAction drawMap(MenuState& st, const Layout& L, const Nav& n, float dt, float t, bool interactive) {
    MenuAction act;
    float sc = L.s;
    if (!I.mapOpen) {
        I.mapOpen = true;
        I.mapCenter = st.playerPos;
        I.mapMpp = I.mapMppTarget = Min(5.5f / sc, mapMaxMpp(L));
        I.cursor = vec2(L.W * 0.5f, L.H * 0.5f);
        I.dragging = false;
        I.mapFade = 0.f;
    }
    I.mapFade = Min(1.f, I.mapFade + dt * 5.f);
    float a = easeOutCubic(I.mapFade);
    rect(0, 0, L.W, L.H, C(0.022f, 0.066f, 0.165f, a));
    if (!mapReady()) {
        TextStyle ts = style(FONT_HEADING, 30.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(L.W * 0.5f, L.H * 0.5f, "MAP DATA UNAVAILABLE", ts);
        return act;
    }
    float maxMpp = mapMaxMpp(L);
    // ---- input
    bool usePadCursor = n.pad || !I.mouseMode;
    if (interactive && I.dialog == DLG_NONE) {
        vec2 center(L.W * 0.5f, L.H * 0.5f);
        if (!usePadCursor) I.cursor = n.mouse;
        else I.cursor = center;
        MapView v0;
        v0.center = I.mapCenter;
        v0.mpp = I.mapMpp;
        v0.screenCenter = center;
        // zoom (wheel around the cursor, triggers / keys around the center)
        float zoomSteps = n.wheel;
        const InputState& in = *n.in;
        if (in.down(KEY_PGUP) || in.down(0xBB) || in.down(0x6B)) zoomSteps += dt * 4.f;
        if (in.down(KEY_PGDN) || in.down(0xBD) || in.down(0x6D)) zoomSteps -= dt * 4.f;
        zoomSteps += (n.rt - n.lt) * dt * 5.f;
        if (zoomSteps != 0.f) I.mapMppTarget = Clamp(I.mapMppTarget * powf(0.78f, zoomSteps), mapMinMpp(), maxMpp);
        // zoom about the cursor: keep the world point under the cursor fixed
        float newMpp = approachExp(I.mapMpp, I.mapMppTarget, 14.f, dt);
        if (fabsf(newMpp - I.mapMpp) > 1e-5f) {
            vec2 anchor = v0.toWorld(I.cursor);
            vec2 off = (I.cursor - center);
            I.mapMpp = newMpp;
            I.mapCenter = anchor - vec2(off.x, -off.y) * I.mapMpp;
        }
        // pan: drag / stick / keys
        if (n.click && !usePadCursor) {
            I.dragging = false;
            I.dragStart = n.mouse;
            I.dragLast = n.mouse;
        }
        if (n.mouseDown && !usePadCursor) {
            if (!I.dragging && length(n.mouse - I.dragStart) > 5.f) I.dragging = true;
            if (I.dragging) {
                vec2 d = n.mouse - I.dragLast;
                I.mapCenter += vec2(-d.x, d.y) * I.mapMpp;
            }
            I.dragLast = n.mouse;
        }
        vec2 pan = n.stick;
        if (in.down(KEY_W) || in.down(KEY_UP)) pan.y += 1.f;
        if (in.down(KEY_S) || in.down(KEY_DOWN)) pan.y -= 1.f;
        if (in.down(KEY_A) || in.down(KEY_LEFT)) pan.x -= 1.f;
        if (in.down(KEY_D) || in.down(KEY_RIGHT)) pan.x += 1.f;
        if (length(pan) > 0.05f) I.mapCenter += pan * (I.mapMpp * 900.f * sc * dt);
        vec2 mn, mx;
        mapLandBounds(mn, mx);
        I.mapCenter = vmax(mn, vmin(mx, I.mapCenter));
        if (n.btnY) {
            I.mapCenter = st.playerPos;
        }
        if (in.pressed(KEY_L) || in.pad.pressed(PAD_BACK)) I.legend = !I.legend;
    }
    MapView v;
    v.center = I.mapCenter;
    v.mpp = I.mapMpp;
    v.rot = 0.f;
    v.screenCenter = vec2(L.W * 0.5f, L.H * 0.5f);
    MapDrawOpts o;
    o.style = MAPSTYLE_FULL;
    o.alpha = a;
    o.extentMin = vec2(0, 0);
    o.extentMax = vec2(L.W, L.H);
    o.buildings = true;
    drawMapBase(v, o);
    // grid overlay (subtle)
    {
        float gridM = 1000.f;
        u32 gc = C(1.f, 1.f, 1.f, 0.035f * a);
        vec2 w0 = v.toWorld(vec2(0, L.H)), w1 = v.toWorld(vec2(L.W, 0));
        for (float gx = floorf(w0.x / gridM) * gridM; gx <= w1.x; gx += gridM) {
            float sx = v.toScreen(vec2(gx, 0)).x;
            rect(sx, 0, 1.f, L.H, gc);
        }
        for (float gy = floorf(w0.y / gridM) * gridM; gy <= w1.y; gy += gridM) {
            float sy = v.toScreen(vec2(0, gy)).y;
            rect(0, sy, L.W, 1.f, gc);
        }
    }
    // labels: most important first, skipped when they would overlap a placed label or a UI panel
    {
        struct Placed { float x0, y0, x1, y1; };
        static std::vector<Placed> placed;
        placed.clear();
        placed.push_back({0.f, 0.f, L.W, 190.f * sc});                                   // header / tabs
        if (I.legend) placed.push_back({L.right - 340.f * sc, 190.f * sc, L.W, L.H * 0.8f});  // legend panel
        placed.push_back({0.f, L.H - 170.f * sc, L.W * 0.4f, L.H});                      // location info
        placed.push_back({L.W * 0.4f, L.H - 90.f * sc, L.W, L.H});                       // prompts
        placed.push_back({L.right - 60.f * sc, L.H - 220.f * sc, L.W, L.H - 120.f * sc});   // compass
        // blip footprints: labels prefer positions that keep blips, the player and the waypoint readable
        static std::vector<Placed> blipRects;
        blipRects.clear();
        auto addBlipRect = [&](vec2 bp, float r) {
            if (bp.x < -r || bp.x > L.W + r || bp.y < -r || bp.y > L.H + r) return;
            blipRects.push_back({bp.x - r, bp.y - r, bp.x + r, bp.y + r});
        };
        for (const Blip& b : st.mapBlips) addBlipRect(v.toScreen(b.pos), 15.f * sc);
        if (st.hasWaypoint) addBlipRect(v.toScreen(st.waypoint), 17.f * sc);
        addBlipRect(v.toScreen(st.playerPos), 19.f * sc);
        static std::vector<const MapLabel*> order;
        order.clear();
        for (const MapLabel& lb : mapLabels()) order.push_back(&lb);
        std::stable_sort(order.begin(), order.end(), [](const MapLabel* x, const MapLabel* y) { return x->importance > y->importance; });
        for (const MapLabel* lbp : order) {
            const MapLabel& lb = *lbp;
            float vis;
            if (lb.importance >= 4.f) vis = SmoothStep(5.5f, 8.f, v.mpp);
            else if (lb.importance >= 3.f) vis = SmoothStep(2.2f, 3.5f, v.mpp);
            else if (lb.importance >= 2.f) vis = 1.f - SmoothStep(14.f, 20.f, v.mpp);
            else vis = 1.f - SmoothStep(8.f, 11.f, v.mpp);
            if (vis <= 0.01f) continue;
            vec2 p = v.toScreen(lb.pos);
            if (p.x < -300 || p.x > L.W + 300 || p.y < -100 || p.y > L.H + 100) continue;
            float size = (lb.importance >= 4.f ? 44.f : lb.importance >= 3.f ? 30.f : lb.importance >= 2.f ? 22.f : 20.f) * sc;
            TextStyle ls = style(lb.importance >= 4.f ? FONT_TITLE : FONT_HEADING, size,
                                 withAlpha(lb.water ? C(0.55f, 0.82f, 0.95f) : kWhite, 0.82f * vis * a), ALIGN_CENTER);
            ls.tracking = lb.importance >= 3.f ? 0.28f : 0.12f;
            ls.outline = 2.f * sc;
            ls.outlineColor = C(0.01f, 0.02f, 0.06f, 0.75f * vis * a);
            ls.skew = lb.water ? 0.2f : 0.f;
            std::string name = upper(lb.name);
            float w = textWidth(name.c_str(), ls);
            // Candidate positions: the anchor, then vertical steps (in label heights), then sideways (in label widths).
            // Never over another label or a UI panel; among the rest the one covering the least blip area wins
            // (ties go to the candidate closest to the anchor).
            const vec2 offs[11] = {{0.f, 0.f}, {0.f, 1.3f}, {0.f, -1.3f}, {0.f, 2.4f}, {0.f, -2.4f}, {0.f, 3.6f}, {0.f, -3.6f},
                                   {0.62f, 0.f}, {-0.62f, 0.f}, {0.62f, 1.3f}, {-0.62f, -1.3f}};
            int bestOi = -1;
            float bestCost = 1e30f;
            Placed bestR = {};
            for (int oi = 0; oi < 11; oi++) {
                float px = p.x + offs[oi].x * w, py = p.y + offs[oi].y * size;
                Placed r = {px - w * 0.5f - 6.f * sc, py - size * 0.6f, px + w * 0.5f + 6.f * sc, py + size * 0.6f};
                bool hit = false;
                for (const Placed& q : placed)
                    if (r.x0 < q.x1 && r.x1 > q.x0 && r.y0 < q.y1 && r.y1 > q.y0) { hit = true; break; }
                if (hit) continue;
                float area = 0.f;
                for (const Placed& q : blipRects) {
                    float ix = Min(r.x1, q.x1) - Max(r.x0, q.x0), iy = Min(r.y1, q.y1) - Max(r.y0, q.y0);
                    if (ix > 0.f && iy > 0.f) area += ix * iy;
                }
                float cost = area + oi * 40.f * sc * sc;
                if (cost < bestCost) { bestCost = cost; bestOi = oi; bestR = r; }
                if (area <= 0.f) break;   // later candidates are farther from the anchor
            }
            if (bestOi >= 0) {
                placed.push_back(bestR);
                text((bestR.x0 + bestR.x1) * 0.5f, (bestR.y0 + bestR.y1) * 0.5f - size * 0.55f, name.c_str(), ls);
            }
        }
        // street names along the roads when zoomed in
        float streetA = Saturate((2.3f - v.mpp * sc) / 0.6f) * a;
        if (streetA > 0.01f) {
            static std::vector<vec4> occupied;
            occupied.clear();
            for (const Placed& q : placed) occupied.push_back(vec4(q.x0, q.y0, q.x1, q.y1));
            for (const Placed& q : blipRects) occupied.push_back(vec4(q.x0, q.y0, q.x1, q.y1));
            drawStreetNames(v, vec2(0.f, 0.f), vec2(L.W, L.H), streetA, sc, occupied);
        }
    }
    // route
    if (!st.gpsRoute.empty()) drawMapRoute(v, st.gpsRoute, C(1.f, 0.4f, 0.8f), Max(3.f, 4.f * sc), a, vec2(0, 0), vec2(L.W, L.H));
    // blips
    const Blip* hover = nullptr;
    float hoverD = 18.f * sc;
    bool hasWpBlip = false;
    for (const Blip& b : st.mapBlips) {
        if (b.icon == BLIP_PLAYER) continue;
        if (b.icon == BLIP_WAYPOINT) hasWpBlip = true;
        vec2 p = v.toScreen(b.pos);
        if (p.x < -30 || p.x > L.W + 30 || p.y < -30 || p.y > L.H + 30) continue;
        drawBlipGlyph(b, p, 30.f * sc, a, t, false, false);
        float d = length(p - I.cursor);
        if (d < hoverD) { hoverD = d; hover = &b; }
    }
    if (st.hasWaypoint && !hasWpBlip) {
        Blip wb;
        wb.pos = st.waypoint;
        wb.icon = BLIP_WAYPOINT;
        drawBlipGlyph(wb, v.toScreen(st.waypoint), 32.f * sc, a, t, false, false);
    }
    // player
    {
        vec2 p = v.toScreen(st.playerPos);
        float pulse = fmodf(t * 0.8f, 1.f);
        circle(p.x, p.y, (14.f + 26.f * pulse) * sc, withAlpha(kWhite, 0.35f * (1.f - pulse) * a), 2.f * sc);
        drawIconGlow(BLIP_PLAYER, p.x, p.y, 34.f * sc, C(0.f, 0.f, 0.f, 0.5f * a), 3.f * sc);
        drawIcon(BLIP_PLAYER, p.x, p.y, 34.f * sc, withAlpha(kWhite, a), 1.8f * sc, C(0.02f, 0.02f, 0.06f, a), v.screenAngle(st.playerHeading));
    }
    // cursor + waypoint set / clear
    vec2 cw = v.toWorld(I.cursor);
    if (interactive && I.dialog == DLG_NONE) {
        bool overLegend = I.legend && inRect(n.mouse, I.legendRect.x, I.legendRect.y, I.legendRect.z, I.legendRect.w);
        bool clickSet = (!usePadCursor && n.release && !I.dragging && !overLegend && inRect(n.mouse, 0, 190.f * sc, L.W, L.H - 190.f * sc)) ||
                        (usePadCursor && n.confirm) || (!usePadCursor && n.confirm);
        bool clear = n.rclick || n.btnX;
        if (clickSet) {
            if (st.hasWaypoint && length(v.toScreen(st.waypoint) - I.cursor) < 22.f * sc) clear = true;
            else {
                st.hasWaypoint = true;
                st.waypoint = cw;
                act.type = MA_SET_WAYPOINT;
                act.pos = cw;
            }
        }
        if (clear && st.hasWaypoint) {
            st.hasWaypoint = false;
            act.type = MA_CLEAR_WAYPOINT;
            act.pos = st.waypoint;
        }
        if (n.release) I.dragging = false;
    }
    {
        vec2 c = I.cursor;
        u32 cc = withAlpha(kWhite, 0.9f * a), sh = C(0.f, 0.f, 0.f, 0.5f * a);
        float r = 16.f * sc;
        circle(c.x, c.y, r, sh, 4.f * sc);
        circle(c.x, c.y, r, cc, 2.f * sc);
        for (int k = 0; k < 4; k++) {
            vec2 d = k == 0 ? vec2(1, 0) : k == 1 ? vec2(-1, 0) : k == 2 ? vec2(0, 1) : vec2(0, -1);
            vec2 p0 = c + d * (r + 3.f * sc), p1 = c + d * (r + 11.f * sc);
            capsule(p0.x, p0.y, p1.x, p1.y, 2.2f * sc, cc);
        }
        circle(c.x, c.y, 2.f * sc, cc);
        if (hover) {
            std::string lab = hover->label ? hover->label : blipDefaultName(hover->icon);
            TextStyle hs = style(FONT_HEADING, 22.f * sc, withAlpha(kWhite, a));
            float w = textWidth(lab.c_str(), hs) + 28.f * sc;
            float bx = c.x + 26.f * sc, by = c.y - 44.f * sc;
            roundRect(bx, by, w, 36.f * sc, 8.f * sc, C(0.03f, 0.04f, 0.11f, 0.92f * a), 1.f * sc, withAlpha(kPink, 0.6f * a));
            text(bx + 14.f * sc, by + 18.f * sc - hs.size * 0.56f, lab.c_str(), hs);
        }
    }
    // location info (bottom-left)
    {
        std::string district = mapDistrictAt(cw);
        std::string street = I.mapMpp < 6.f ? mapStreetAt(cw, 40.f) : std::string();
        float bx = L.left, by = L.H - 150.f * sc;
        TextStyle ds = style(FONT_HEADING, 34.f * sc, withAlpha(kWhite, a));
        ds.shadow = 2.f * sc;
        ds.outline = 1.5f * sc;
        ds.outlineColor = C(0.f, 0.f, 0.04f, 0.7f * a);
        std::string dn = upper(district);
        text(bx, by, dn.c_str(), ds);
        if (!street.empty()) {
            TextStyle ss = style(FONT_BODY, 22.f * sc, withAlpha(kTextDim, a));
            ss.shadow = 1.5f * sc;
            text(bx, by + 40.f * sc, street.c_str(), ss);
        }
        // scale bar
        float targetPx = 140.f * sc;
        float meters = targetPx * I.mapMpp;
        const float nice[] = {50, 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000};
        float m = nice[0];
        for (float nv : nice)
            if (nv <= meters) m = nv;
        float px = m / I.mapMpp;
        float sy = L.H - 72.f * sc;
        rect(bx, sy, px, 3.f * sc, withAlpha(kWhite, 0.9f * a));
        rect(bx, sy - 6.f * sc, 2.f * sc, 9.f * sc, withAlpha(kWhite, 0.9f * a));
        rect(bx + px - 2.f * sc, sy - 6.f * sc, 2.f * sc, 9.f * sc, withAlpha(kWhite, 0.9f * a));
        TextStyle ks = style(FONT_HEADING, 18.f * sc, withAlpha(kTextDim, a));
        std::string sl = m >= 1000.f ? StrFormat("%g KM", m / 1000.f) : StrFormat("%d M", (int)m);
        text(bx + px + 10.f * sc, sy - 10.f * sc, sl.c_str(), ks);
    }
    // legend (right)
    if (I.legend) {
        struct LegendEntry { BlipIcon icon; std::string name; u32 color; };
        std::vector<LegendEntry> entries;
        entries.push_back({BLIP_PLAYER, "You", kWhite});
        if (st.hasWaypoint || hasWpBlip) entries.push_back({BLIP_WAYPOINT, "Waypoint", kPink});
        for (const Blip& b : st.mapBlips) {
            if (b.icon == BLIP_PLAYER || b.icon == BLIP_WAYPOINT) continue;
            std::string nm = b.label ? b.label : blipDefaultName(b.icon);
            bool dup = false;
            for (auto& e : entries)
                if (e.icon == b.icon && e.name == nm) dup = true;
            if (dup || entries.size() >= 14) continue;
            bool semantic = b.icon == BLIP_ENEMY || b.icon == BLIP_FRIEND || b.icon == BLIP_POLICE || b.icon == BLIP_OBJECTIVE || b.icon == BLIP_MISSION;
            u32 col = (b.color == 0 || (semantic && b.color == 0xffffffffu)) ? blipDefaultColor(b.icon) : b.color;
            entries.push_back({b.icon, nm, col});
        }
        float lw = 330.f * sc, rowH = 38.f * sc;
        float lh = 64.f * sc + rowH * entries.size();
        float lx = L.right - lw, ly = 200.f * sc;
        I.legendRect = vec4(lx, ly, lw, lh);
        panel(lx, ly, lw, lh, a, 12.f);
        TextStyle hs = style(FONT_HEADING, 22.f * sc, withAlpha(kPink, a));
        hs.tracking = 0.2f;
        text(lx + 20.f * sc, ly + 16.f * sc, "LEGEND", hs);
        for (size_t i = 0; i < entries.size(); i++) {
            float ry = ly + 54.f * sc + i * rowH;
            Blip b;
            b.icon = entries[i].icon;
            b.color = entries[i].color;
            if (b.icon == BLIP_PLAYER) drawIcon(BLIP_PLAYER, lx + 34.f * sc, ry + rowH * 0.5f, 26.f * sc, withAlpha(kWhite, a), 1.5f * sc, C(0, 0, 0, a));
            else drawBlipGlyph(b, vec2(lx + 34.f * sc, ry + rowH * 0.5f + (b.icon == BLIP_WAYPOINT ? 6.f * sc : 0.f)), 26.f * sc, a, t, false, false);
            TextStyle es = style(FONT_BODY, 19.f * sc, withAlpha(kText, a));
            text(lx + 62.f * sc, ry + rowH * 0.5f - es.size * 0.6f, entries[i].name.c_str(), es);
        }
    }
    // compass
    {
        vec2 c(L.right - 26.f * sc, L.H - 170.f * sc);
        if (I.legend) c.y = L.H - 170.f * sc;
        circle(c.x, c.y, 22.f * sc, C(0.03f, 0.04f, 0.11f, 0.85f * a));
        circle(c.x, c.y, 22.f * sc, withAlpha(kWhite, 0.25f * a), 1.2f * sc);
        triangle(vec2(c.x, c.y - 17.f * sc), vec2(c.x + 7.f * sc, c.y), vec2(c.x - 7.f * sc, c.y), withAlpha(kPink, a));
        triangle(vec2(c.x, c.y + 17.f * sc), vec2(c.x - 7.f * sc, c.y), vec2(c.x + 7.f * sc, c.y), withAlpha(kWhite, 0.6f * a));
        TextStyle ns = style(FONT_HEADING, 16.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
        text(c.x, c.y - 44.f * sc, "N", ns);
    }
    return act;
}

// ------------------------------------------------------------------------------------------------------------------
// Settings
enum SetType { ST_TOGGLE, ST_SLIDER, ST_OPTIONS, ST_ACTION, ST_HEADER, ST_BIND };
struct SetItem {
    const char* label;
    const char* desc;
    SetType type;
    bool* b = nullptr;
    float* f = nullptr;
    int* i = nullptr;
    float mn = 0, mx = 1, step = 0.05f;
    std::vector<std::string> opts;
    int optBase = 0;          // value of opts[0] (resolution list starts at -1)
    std::vector<int> ivals;   // explicit int values per option (frame rate limit)
    std::vector<float> fvals; // explicit float values per option (upscaling presets on renderScale)
    const char* fmt = "%d%%";
    float fmtScale = 100.f;
    const char* onLabel = "On";
    const char* offLabel = "Off";
    int action = -1;          // ST_BIND: InputAction
};

enum SetCategory { SC_DISPLAY = 0, SC_AUDIO, SC_CAMERA, SC_CONTROLS, SC_BINDINGS, SC_ACCESS, SC_GAMEPLAY, SC_COUNT };
const char* kCatNames[SC_COUNT] = {"Display & Graphics", "Audio", "Camera", "Controls", "Key Bindings", "Accessibility", "Gameplay"};
const int kCatIcons[SC_COUNT] = {ICO_MONITOR, ICO_SPEAKER, ICO_CAMERA, ICO_GAMEPAD, ICO_KEYBOARD, ICO_ACCESS, BLIP_VIGILANTE};

const char* const kPadLayoutDesc[3] = {
    "Standard: A sprint, X jump, B reload, Y enter vehicle, LB weapon wheel, RB cover, LT aim, RT shoot. Left stick moves, right stick looks.",
    "Alternate: A jump, X sprint; everything else as Standard.",
    "Southpaw: the right stick moves and the left stick looks; buttons as Standard."};

void buildItems(GameSettings& gs, int cat, std::vector<SetItem>& items) {
    items.clear();
    auto toggle = [&](const char* l, const char* d, bool* b, const char* on = "On", const char* off = "Off") {
        SetItem it;
        it.label = l; it.desc = d; it.type = ST_TOGGLE; it.b = b; it.onLabel = on; it.offLabel = off;
        items.push_back(it);
    };
    auto slider = [&](const char* l, const char* d, float* f, float mn, float mx, float step, const char* fmt, float scale) {
        SetItem it;
        it.label = l; it.desc = d; it.type = ST_SLIDER; it.f = f; it.mn = mn; it.mx = mx; it.step = step; it.fmt = fmt; it.fmtScale = scale;
        items.push_back(it);
    };
    auto options = [&](const char* l, const char* d, int* i, const std::vector<std::string>& o, int base) {
        SetItem it;
        it.label = l; it.desc = d; it.type = ST_OPTIONS; it.i = i; it.opts = o; it.optBase = base;
        items.push_back(it);
    };
    auto ioptions = [&](const char* l, const char* d, int* i, const std::vector<std::string>& o, const std::vector<int>& v) {
        SetItem it;
        it.label = l; it.desc = d; it.type = ST_OPTIONS; it.i = i; it.opts = o; it.ivals = v;
        items.push_back(it);
    };
    auto foptions = [&](const char* l, const char* d, float* f, const std::vector<std::string>& o, const std::vector<float>& v) {
        SetItem it;
        it.label = l; it.desc = d; it.type = ST_OPTIONS; it.f = f; it.opts = o; it.fvals = v;
        items.push_back(it);
    };
    auto header = [&](const char* l) {
        SetItem it;
        it.label = l; it.desc = ""; it.type = ST_HEADER;
        items.push_back(it);
    };
    auto bind = [&](int a) {
        SetItem it;
        it.label = inputActionName((InputAction)a);
        it.desc = "Enter / A to rebind, Delete clears the selected key. Left / right picks the primary or secondary key.";
        it.type = ST_BIND;
        it.action = a;
        items.push_back(it);
    };
    auto action = [&](const char* l, const char* d) {
        SetItem it;
        it.label = l; it.desc = d; it.type = ST_ACTION;
        items.push_back(it);
    };
    switch (cat) {
    case SC_DISPLAY: {
        std::vector<std::string> res;
        res.push_back("Native (Desktop)");
        for (const auto& m : Menus::displayModes()) res.push_back(StrFormat("%d x %d", m.width, m.height));
        options("Resolution", "Output resolution. Native matches the desktop and gives the sharpest image.", &gs.resolutionIndex, res, -1);
        toggle("Window Mode", "Borderless fullscreen or a regular desktop window.", &gs.fullscreen, "Fullscreen", "Windowed");
        toggle("V-Sync", "Locks the frame rate to the display refresh rate to prevent screen tearing.", &gs.vsync);
        ioptions("Frame Rate Limit", "Caps the frame rate to save power and keep frame pacing even.", &gs.frameRateCap,
                 {"30 FPS", "60 FPS", "120 FPS", "Unlimited"}, {30, 60, 120, 0});
        foptions("Upscaling", "Internal rendering resolution, upscaled with temporal AA. Lower presets run faster.", &gs.renderScale,
                 {"Native (100%)", "Ultra Quality (90%)", "Quality (77%)", "Balanced (67%)", "Performance (58%)", "Ultra Performance (50%)"},
                 {1.f, 0.9f, 0.77f, 0.67f, 0.58f, 0.5f});
        options("Graphics Quality", "Overall detail level: shadows, draw distance, clouds, vegetation and post effects.", &gs.quality,
                {"Low", "Medium", "High", "Ultra"}, 0);
        toggle("Motion Blur", "Camera and object motion blur.", &gs.motionBlur);
        slider("Brightness", "Exposure bias applied on top of the automatic exposure.", &gs.brightness, -1.f, 1.f, 0.05f, "%+d", 100.f);
        break;
    }
    case SC_AUDIO:
        slider("Master Volume", "Overall output volume.", &gs.masterVolume, 0.f, 1.f, 0.05f, "%d%%", 100.f);
        slider("Sound Effects", "Weapons, vehicles, impacts and the city ambience.", &gs.sfxVolume, 0.f, 1.f, 0.05f, "%d%%", 100.f);
        slider("Music", "Score and mission music.", &gs.musicVolume, 0.f, 1.f, 0.05f, "%d%%", 100.f);
        slider("Radio", "In-vehicle radio stations.", &gs.radioVolume, 0.f, 1.f, 0.05f, "%d%%", 100.f);
        slider("Dialogue", "Voices of characters and pedestrians.", &gs.dialogueVolume, 0.f, 1.f, 0.05f, "%d%%", 100.f);
        break;
    case SC_CAMERA:
        toggle("On-Foot View", "Camera used on foot when the game starts. The camera button still switches views in play.",
               &gs.firstPersonOnFoot, "First Person", "Third Person");
        toggle("Vehicle View", "Camera used when you get into a vehicle. The camera button still cycles views while driving.",
               &gs.firstPersonVehicle, "First Person", "Third Person");
        slider("Field of View", "Vertical field of view of the third-person camera.", &gs.fov, 50.f, 90.f, 1.f, "%d DEG", 1.f);
        slider("First Person Field of View", "Vertical field of view in first person, on foot and in vehicles.", &gs.fovFirstPerson, 55.f, 100.f,
               1.f, "%d DEG", 1.f);
        slider("Camera Shake", "Strength of camera shake from impacts, explosions, gunfire and speed.", &gs.cameraShake, 0.f, 1.f, 0.05f, "%d%%",
               100.f);
        toggle("Vehicle Camera Auto-Centre", "The vehicle camera swings back behind the car after you stop looking around.",
               &gs.vehicleAutoCenter);
        toggle("First Person Head Bob", "Head movement while walking and running in first person. Turn off if it causes motion sickness.",
               &gs.headBob);
        break;
    case SC_CONTROLS:
        slider("Mouse Sensitivity (Horizontal)", "Camera turn speed with the mouse.", &gs.mouseSensitivity, 0.1f, 3.f, 0.05f, "%.2fx", 1.f);
        slider("Mouse Sensitivity (Vertical)", "Camera pitch speed with the mouse.", &gs.mouseSensitivityY, 0.1f, 3.f, 0.05f, "%.2fx", 1.f);
        slider("Controller Sensitivity (Horizontal)", "Camera turn speed with the look stick.", &gs.padSensitivity, 0.1f, 3.f, 0.05f, "%.2fx", 1.f);
        slider("Controller Sensitivity (Vertical)", "Camera pitch speed with the look stick.", &gs.padSensitivityY, 0.1f, 3.f, 0.05f, "%.2fx", 1.f);
        toggle("Invert Look", "Inverts the vertical camera axis.", &gs.invertY);
        toggle("Aim", "Hold the aim button, or press once to aim and again to stop.", &gs.aimToggle, "Toggle", "Hold");
        toggle("Sprint", "Hold the sprint button, or press once to keep running.", &gs.sprintToggle, "Toggle", "Hold");
        toggle("Crouch", "Hold the crouch button, or press once to stay crouched.", &gs.crouchToggle, "Toggle", "Hold");
        options("Controller Layout", kPadLayoutDesc[Clamp(gs.padLayout, 0, 2)], &gs.padLayout, {"Standard", "Alternate", "Southpaw"}, 0);
        toggle("Vibration", "Controller rumble for impacts, gunfire and engines.", &gs.vibration);
        toggle("Aim Assist", "Slows the reticle over targets when aiming with a controller.", &gs.aimAssist);
        break;
    case SC_BINDINGS:
        header("On Foot");
        for (int k = IA_MOVE_FORWARD; k <= IA_COVER; k++) bind(k);
        header("On Foot and in Vehicles");
        for (int k = IA_ENTER_VEHICLE; k <= IA_WEAPON_WHEEL; k++) bind(k);
        header("Vehicles");
        for (int k = IA_ACCELERATE; k <= IA_RADIO_PREV; k++) bind(k);
        header("General");
        for (int k = IA_CAMERA_VIEW; k < IA_COUNT; k++) bind(k);
        break;
    case SC_ACCESS:
        toggle("Subtitles", "Shows dialogue as text at the bottom of the screen.", &gs.subtitles);
        options("Subtitle Size", "Text size of subtitles.", &gs.subtitleSize, {"Small", "Medium", "Large", "Extra Large"}, 0);
        slider("Subtitle Background", "Opacity of the box behind subtitles.", &gs.subtitleBackground, 0.f, 1.f, 0.1f, "%d%%", 100.f);
        toggle("Speaker Name Colours", "Shows each speaker's name in their own colour, or all names in white.", &gs.speakerColors);
        slider("HUD Scale", "Size of the heads-up display.", &gs.hudScale, 0.75f, 1.25f, 0.05f, "%d%%", 100.f);
        toggle("High-Contrast Reticle", "A larger, yellow aiming reticle with a black outline.", &gs.highContrastReticle);
        options("Colour-Blind Mode", "Shifts colours so they stay distinguishable with the chosen type of colour blindness.", &gs.colorblindMode,
                {"Off", "Protanopia", "Deuteranopia", "Tritanopia"}, 0);
        toggle("Reduce Flashing", "Dampens lightning, strobes, muzzle flashes and flashing HUD elements.", &gs.reduceFlashing);
        slider("Music Ducking", "How much the radio and music quieten while characters speak.", &gs.musicDucking, 0.f, 1.f, 0.05f, "%d%%", 100.f);
        break;
    default:
        toggle("Show Radar", "Shows the radar and the health and armor bars.", &gs.showRadar);
        toggle("Show HUD", "Shows the rest of the heads-up display.", &gs.showHud);
        toggle("Units", "Speed and distance units.", &gs.metricUnits, "Metric", "Imperial");
        break;
    }
    action("Restore Defaults", "Resets every option on this page to its default value.");
}

void restoreDefaults(GameSettings& gs, int cat) {
    GameSettings d;
    switch (cat) {
    case SC_DISPLAY:
        gs.resolutionIndex = d.resolutionIndex; gs.fullscreen = d.fullscreen; gs.vsync = d.vsync; gs.quality = d.quality;
        gs.renderScale = d.renderScale; gs.motionBlur = d.motionBlur; gs.brightness = d.brightness; gs.frameRateCap = d.frameRateCap;
        break;
    case SC_AUDIO:
        gs.masterVolume = d.masterVolume; gs.sfxVolume = d.sfxVolume; gs.musicVolume = d.musicVolume; gs.radioVolume = d.radioVolume;
        gs.dialogueVolume = d.dialogueVolume;
        break;
    case SC_CAMERA:
        gs.firstPersonOnFoot = d.firstPersonOnFoot; gs.firstPersonVehicle = d.firstPersonVehicle; gs.fov = d.fov;
        gs.fovFirstPerson = d.fovFirstPerson; gs.cameraShake = d.cameraShake; gs.vehicleAutoCenter = d.vehicleAutoCenter;
        gs.headBob = d.headBob;
        break;
    case SC_CONTROLS:
        gs.mouseSensitivity = d.mouseSensitivity; gs.padSensitivity = d.padSensitivity; gs.mouseSensitivityY = d.mouseSensitivityY;
        gs.padSensitivityY = d.padSensitivityY; gs.invertY = d.invertY; gs.aimToggle = d.aimToggle; gs.sprintToggle = d.sprintToggle;
        gs.crouchToggle = d.crouchToggle; gs.padLayout = d.padLayout; gs.vibration = d.vibration; gs.aimAssist = d.aimAssist;
        break;
    case SC_BINDINGS: memcpy(gs.keyBinds, d.keyBinds, sizeof(gs.keyBinds)); break;
    case SC_ACCESS:
        gs.subtitles = d.subtitles; gs.subtitleSize = d.subtitleSize; gs.subtitleBackground = d.subtitleBackground;
        gs.speakerColors = d.speakerColors; gs.hudScale = d.hudScale; gs.highContrastReticle = d.highContrastReticle;
        gs.colorblindMode = d.colorblindMode; gs.reduceFlashing = d.reduceFlashing; gs.musicDucking = d.musicDucking;
        break;
    default:
        gs.showRadar = d.showRadar; gs.showHud = d.showHud; gs.metricUnits = d.metricUnits;
        break;
    }
}

// Current option index of an ST_OPTIONS item (explicit value lists pick the nearest value)
int optionIndex(const SetItem& it) {
    int n = (int)it.opts.size();
    if (!it.ivals.empty()) {
        for (int k = 0; k < (int)it.ivals.size(); k++)
            if (it.ivals[k] == *it.i) return k;
        return n - 1;
    }
    if (!it.fvals.empty()) {
        int best = 0;
        for (int k = 1; k < (int)it.fvals.size(); k++)
            if (fabsf(it.fvals[k] - *it.f) < fabsf(it.fvals[best] - *it.f)) best = k;
        return best;
    }
    int idx = *it.i - it.optBase;
    return (idx < 0 || idx >= n) ? 0 : idx;
}

std::string valueText(const SetItem& it) {
    switch (it.type) {
    case ST_TOGGLE: return *it.b ? it.onLabel : it.offLabel;
    case ST_SLIDER: {
        float v = *it.f * it.fmtScale;
        if (strchr(it.fmt, 'f')) return StrFormat(it.fmt, v);
        return StrFormat(it.fmt, (int)lrintf(v));
    }
    case ST_OPTIONS: return it.opts.empty() ? std::string() : it.opts[optionIndex(it)];
    default: return "";
    }
}

// Changes an item by `dir` steps (0 = activate). Returns true when a value changed.
bool changeItem(SetItem& it, int dir, GameSettings& gs, int cat) {
    switch (it.type) {
    case ST_TOGGLE:
        *it.b = !*it.b;
        return true;
    case ST_SLIDER: {
        if (dir == 0) return false;
        float nv = Clamp(*it.f + it.step * dir, it.mn, it.mx);
        nv = it.mn + roundf((nv - it.mn) / it.step) * it.step;
        nv = Clamp(nv, it.mn, it.mx);
        if (nv != *it.f) { *it.f = nv; return true; }
        return false;
    }
    case ST_OPTIONS: {
        int n = (int)it.opts.size();
        if (n == 0) return false;
        int idx = ((optionIndex(it) + (dir == 0 ? 1 : dir)) % n + n) % n;
        if (!it.ivals.empty()) *it.i = it.ivals[idx];
        else if (!it.fvals.empty()) *it.f = it.fvals[idx];
        else *it.i = idx + it.optBase;
        return true;
    }
    case ST_ACTION:
        if (dir != 0) return false;
        restoreDefaults(gs, cat);
        return true;
    default: return false;
    }
}

bool contextsClash(int a, int b) {
    InputContext ca = inputActionContext((InputAction)a), cb = inputActionContext((InputAction)b);
    return ca == cb || ca == ICTX_ANY || cb == ICTX_ANY;
}

// Another action in a clashing context that uses `key` (returns the action, slot in *slotOut), or -1
int bindingConflict(const GameSettings& gs, int action, int key, int* slotOut) {
    if (key <= 0) return -1;
    for (int b = 0; b < IA_COUNT; b++) {
        if (b == action || !contextsClash(action, b)) continue;
        for (int k = 0; k < 2; k++)
            if (gs.keyBinds[b][k] == key) {
                if (slotOut) *slotOut = k;
                return b;
            }
    }
    return -1;
}

// Newly pressed key for the capture: generic modifier codes win over left / right variants; Escape cancels (-1),
// Delete clears (0 is returned through *clear)
int capturedKey(const InputState& in, bool* cancel, bool* clear) {
    *cancel = in.pressed(KEY_ESCAPE) || in.pad.pressed(PAD_B);
    *clear = in.pressed(KEY_DELETE) || in.pad.pressed(PAD_X);
    if (*cancel || *clear) return 0;
    int found = 0;
    for (int vk = 1; vk < KEY_COUNT; vk++) {
        if (!in.pressed(vk)) continue;
        if (vk >= 0xA0 && vk <= 0xA5) {
            int generic = vk <= 0xA1 ? KEY_SHIFT : vk <= 0xA3 ? KEY_CONTROL : KEY_ALT;
            if (in.down(generic)) continue;
        }
        found = vk;
        break;
    }
    return found;
}

bool drawSettings(MenuState& st, const Layout& L, const Nav& n, float x, float y, float w, float h, float a, float dt, bool& exit) {
    float sc = L.s;
    bool changed = false;
    GameSettings& gs = st.settings;
    // categories column
    float cw = 390.f * sc, rowH = 58.f * sc;
    panel(x, y, cw, h, a);
    float catY0 = y + 18.f * sc;
    bool dialogFree = I.dialog == DLG_NONE;
    bool capturing = I.bindCapture;
    for (int c = 0; c < SC_COUNT; c++) {
        float ry = catY0 + c * (rowH + 6.f * sc);
        if (dialogFree && !capturing && inRect(n.mouse, x + 10.f * sc, ry, cw - 20.f * sc, rowH) && n.click) {
            I.setCat = c;
            I.setItemsFocus = false;
            I.setCursor = 0;
            I.setScroll = 0.f;
        }
    }
    bool consumed = false;   // the key that moved focus must not also edit a value
    if (capturing) consumed = true;
    else if (dialogFree && !I.setItemsFocus) {
        if (n.up) { I.setCat = (I.setCat + SC_COUNT - 1) % SC_COUNT; I.setCursor = 0; I.setScroll = 0.f; }
        if (n.down) { I.setCat = (I.setCat + 1) % SC_COUNT; I.setCursor = 0; I.setScroll = 0.f; }
        if (n.right || n.confirm) { I.setItemsFocus = true; I.setCursor = 0; consumed = true; }
        else if (n.back) exit = true;
    } else if (dialogFree && n.back) {
        I.setItemsFocus = false;
        consumed = true;
    }
    float catTarget = catY0 + I.setCat * (rowH + 6.f * sc);
    if (I.setCatHl < 0.f) I.setCatHl = catTarget;
    I.setCatHl = approachExp(I.setCatHl, catTarget, 18.f, dt);
    if (!I.setItemsFocus) selectionBar(x + 10.f * sc, I.setCatHl, cw - 20.f * sc, rowH, a);
    else roundRect(x + 10.f * sc, I.setCatHl, cw - 20.f * sc, rowH, 6.f * sc, C(1.f, 1.f, 1.f, 0.08f * a), 1.f * sc, withAlpha(kPink, 0.6f * a));
    for (int c = 0; c < SC_COUNT; c++) {
        float ry = catY0 + c * (rowH + 6.f * sc);
        bool sel = c == I.setCat;
        drawIcon(kCatIcons[c], x + 42.f * sc, ry + rowH * 0.5f, 28.f * sc, withAlpha(sel ? kWhite : kTextDim, a));
        TextStyle ts = style(FONT_HEADING, 25.f * sc, withAlpha(sel ? kWhite : kTextDim, a));
        ts.tracking = 0.05f;
        std::string lab = upper(kCatNames[c]);
        float avail = cw - 70.f * sc - 22.f * sc, tw = textWidth(lab.c_str(), ts);
        if (tw > avail) ts.size *= avail / tw;
        text(x + 70.f * sc, ry + rowH * 0.5f - ts.size * 0.56f, lab.c_str(), ts);
    }
    // items
    static std::vector<SetItem> items;
    buildItems(gs, I.setCat, items);
    int count = (int)items.size();
    bool bindings = I.setCat == SC_BINDINGS;
    float ix = x + cw + 20.f * sc, iw = w - cw - 20.f * sc;
    panel(ix, y, iw, h, a);
    TextStyle hs = style(FONT_HEADING, 30.f * sc, withAlpha(kWhite, a));
    hs.tracking = 0.06f;
    std::string title = upper(kCatNames[I.setCat]);
    text(ix + 30.f * sc, y + 22.f * sc, title.c_str(), hs);
    if (bindings) {
        TextStyle cs = style(FONT_HEADING, 17.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
        cs.tracking = 0.12f;
        float colW = 190.f * sc, c1 = ix + iw - 30.f * sc - colW * 0.5f, c0 = c1 - colW - 14.f * sc;
        text(c0, y + 36.f * sc, "PRIMARY", cs);
        text(c1, y + 36.f * sc, "SECONDARY", cs);
    }
    rect(ix + 30.f * sc, y + 66.f * sc, iw - 60.f * sc, 1.f * sc, withAlpha(kWhite, 0.1f * a));
    float listY = y + 82.f * sc, ih = 54.f * sc, rowStep = ih + 4.f * sc;
    float listH = h - 82.f * sc - 96.f * sc;
    auto selectable = [&](int k) { return items[k].type != ST_HEADER; };
    bool follow = consumed && !capturing;   // focus just moved into the list
    if (dialogFree && I.setItemsFocus && !consumed && count > 0) {
        follow = n.up || n.down;
        if (n.up)
            for (int t = 0; t < count; t++) {
                I.setCursor = (I.setCursor + count - 1) % count;
                if (selectable(I.setCursor)) break;
            }
        if (n.down)
            for (int t = 0; t < count; t++) {
                I.setCursor = (I.setCursor + 1) % count;
                if (selectable(I.setCursor)) break;
            }
    }
    I.setCursor = Clamp(I.setCursor, 0, count - 1);
    while (I.setCursor < count - 1 && !selectable(I.setCursor)) I.setCursor++;
    // scroll the cursor row into view after keyboard / pad navigation; the mouse wheel scrolls freely
    if (follow) {
        float curTop = I.setCursor * rowStep, curBottom = curTop + ih;
        if (I.setCursor > 0 && !selectable(I.setCursor - 1)) curTop -= rowStep;   // keep its section title visible
        if (curTop - I.setScroll < 0.f) I.setScroll = curTop;
        if (curBottom - I.setScroll > listH) I.setScroll = curBottom - listH;
    }
    float maxScroll = Max(0.f, count * rowStep - 4.f * sc - listH);
    if (n.wheel != 0.f && !capturing && inRect(n.mouse, ix, listY, iw, listH)) I.setScroll -= n.wheel * rowStep * 1.5f;
    I.setScroll = Clamp(I.setScroll, 0.f, maxScroll);
    I.setScrollShown = approachExp(I.setScrollShown, I.setScroll, 18.f, dt);
    float off = I.setScrollShown;
    float valW = 420.f * sc;
    float valX = ix + iw - 30.f * sc - valW;
    if (!n.mouseDown) I.dragSlider = -1;
    for (int k = 0; k < count; k++) {
        float ry = listY + k * rowStep - off;
        if (ry + ih < listY || ry > listY + listH) continue;
        if (dialogFree && !capturing && selectable(k) && inRect(n.mouse, ix + 14.f * sc, ry, iw - 28.f * sc, ih) && (n.mouseMoved || n.click)) {
            I.setItemsFocus = true;
            I.setCursor = k;
        }
    }
    ClipState pc = getClip();
    setClipRect(ix, listY - 2.f * sc, iw, listH + 4.f * sc);
    float rowTarget = listY + I.setCursor * rowStep - off;
    if (I.setRowHl < 0.f) I.setRowHl = rowTarget;
    I.setRowHl = approachExp(I.setRowHl, rowTarget, 20.f, dt);
    if (I.setItemsFocus && !bindings) selectionBar(ix + 14.f * sc, I.setRowHl, iw - 28.f * sc, ih, a);
    if (I.setItemsFocus && bindings) roundRect(ix + 14.f * sc, I.setRowHl, iw - 28.f * sc, ih, 6.f * sc, C(1.f, 1.f, 1.f, 0.07f * a), 1.f * sc,
                                               withAlpha(kPink, 0.55f * a));
    std::string conflictNote;
    for (int k = 0; k < count; k++) {
        SetItem& it = items[k];
        float ry = listY + k * rowStep - off;
        if (ry + ih < listY - 2.f * sc || ry > listY + listH) continue;
        bool sel = I.setItemsFocus && k == I.setCursor;
        float cy = ry + ih * 0.5f;
        if (it.type == ST_HEADER) {
            TextStyle hts = style(FONT_HEADING, 18.f * sc, withAlpha(kPink, a));
            hts.tracking = 0.2f;
            std::string hl = upper(it.label);
            float tw = text(ix + 34.f * sc, cy - hts.size * 0.2f, hl.c_str(), hts);
            rect(ix + 44.f * sc + tw, cy + hts.size * 0.35f, iw - 78.f * sc - tw, 1.f * sc, withAlpha(kPink, 0.35f * a));
            continue;
        }
        TextStyle ls = style(FONT_HEADING, 24.f * sc, withAlpha(it.type == ST_ACTION ? (sel && !bindings ? kWhite : kPink) : (sel && !bindings ? kWhite : kText), a));
        ls.tracking = 0.03f;
        std::string lab = upper(it.label);
        float labMax = (it.type == ST_BIND ? iw - 480.f * sc : valX - ix - 50.f * sc);
        float lw = textWidth(lab.c_str(), ls);
        if (lw > labMax) ls.size *= labMax / lw;
        text(ix + 34.f * sc, cy - ls.size * 0.56f, lab.c_str(), ls);
        if (it.type == ST_ACTION) {
            if (dialogFree && !capturing && sel && n.confirm && !consumed) changed |= changeItem(it, 0, gs, I.setCat);
            if (dialogFree && !capturing && n.click && inRect(n.mouse, ix + 14.f * sc, ry, iw - 28.f * sc, ih)) changed |= changeItem(it, 0, gs, I.setCat);
            continue;
        }
        if (it.type == ST_BIND) {
            float colW = 190.f * sc, bh = 38.f * sc;
            float c1 = ix + iw - 30.f * sc - colW, c0 = c1 - colW - 14.f * sc;
            for (int slot = 0; slot < 2; slot++) {
                float bx = slot == 0 ? c0 : c1, by = cy - bh * 0.5f;
                int key = gs.keyBinds[it.action][slot];
                bool cellSel = sel && I.bindCol == slot;
                bool waiting = capturing && I.bindAction == it.action && I.bindSlot == slot;
                int otherSlot = 0;
                int other = bindingConflict(gs, it.action, key, &otherSlot);
                u32 frame = waiting ? kPink : cellSel ? kWhite : other >= 0 ? kRed : C(1.f, 1.f, 1.f, 0.14f);
                if (cellSel || waiting) selectionBar(bx, by, colW, bh, a);
                else roundRect(bx, by, colW, bh, 6.f * sc, C(0.03f, 0.04f, 0.10f, 0.55f * a), 1.2f * sc, withAlpha(frame, a));
                if (waiting) {
                    float blink = uiOptions().reduceFlashing ? 1.f : 0.55f + 0.45f * sinf(uiTime() * 7.f);
                    TextStyle ws = style(FONT_HEADING, 17.f * sc, withAlpha(kWhite, blink * a), ALIGN_CENTER);
                    ws.tracking = 0.1f;
                    text(bx + colW * 0.5f, cy - ws.size * 0.56f, "PRESS A KEY", ws);
                } else if (key > 0) {
                    std::string kn = keyName(key);
                    float ph = 28.f * sc;
                    float pw = promptWidth(kn.c_str(), false, ph);
                    if (pw > colW - 16.f * sc) {
                        TextStyle ks = style(FONT_HEADING, 18.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
                        text(bx + colW * 0.5f, cy - ks.size * 0.56f, kn.c_str(), ks);
                    } else drawPrompt(bx + colW * 0.5f - pw * 0.5f, cy - ph * 0.5f, kn.c_str(), false, ph, a);
                    if (other >= 0) {
                        drawIcon(ICO_BOLT, bx + colW - 14.f * sc, cy, 18.f * sc, withAlpha(kRed, a));
                        if (sel) conflictNote = StrFormat("%s is also bound to %s.", kn.c_str(), inputActionName((InputAction)other));
                    }
                } else {
                    TextStyle es = style(FONT_HEADING, 18.f * sc, withAlpha(kTextMute, a), ALIGN_CENTER);
                    text(bx + colW * 0.5f, cy - es.size * 0.56f, "-", es);
                }
                if (dialogFree && !capturing && n.click && inRect(n.mouse, bx, by, colW, bh)) {
                    I.setItemsFocus = true;
                    I.setCursor = k;
                    I.bindCol = slot;
                    I.bindCapture = true;
                    I.bindCaptureFresh = true;
                    I.bindAction = it.action;
                    I.bindSlot = slot;
                }
            }
            if (dialogFree && sel && !capturing && !consumed) {
                if (n.left) I.bindCol = 0;
                if (n.right) I.bindCol = 1;
                if (n.confirm) {
                    I.bindCapture = true;
                    I.bindCaptureFresh = true;
                    I.bindAction = it.action;
                    I.bindSlot = I.bindCol;
                }
                bool clearKey = n.in && (n.in->pressed(KEY_DELETE) || n.in->pad.pressed(PAD_X));
                if (clearKey && gs.keyBinds[it.action][I.bindCol] != 0) {
                    gs.keyBinds[it.action][I.bindCol] = 0;
                    changed = true;
                }
            }
            continue;
        }
        std::string val = valueText(it);
        TextStyle vs = style(FONT_HEADING, 23.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
        if (it.type == ST_TOGGLE) {
            float pw = 62.f * sc, ph = 30.f * sc;
            float px = valX + valW - pw, py = cy - ph * 0.5f;
            bool on = *it.b;
            roundRect(px, py, pw, ph, ph * 0.5f, on ? withAlpha(sel ? kWhite : kPink, 0.95f * a) : C(1.f, 1.f, 1.f, 0.12f * a), 1.f * sc,
                      withAlpha(kWhite, 0.25f * a));
            float kx = on ? px + pw - ph * 0.5f : px + ph * 0.5f;
            circle(kx, cy, ph * 0.5f - 4.f * sc, on ? withAlpha(sel ? kPinkHot : kWhite, a) : withAlpha(kTextDim, a));
            vs.align = ALIGN_RIGHT;
            text(px - 16.f * sc, cy - vs.size * 0.56f, upper(val).c_str(), vs);
            if (dialogFree && !capturing && n.click && inRect(n.mouse, ix + 14.f * sc, ry, iw - 28.f * sc, ih)) changed |= changeItem(it, 0, gs, I.setCat);
        } else if (it.type == ST_SLIDER) {
            float bw = 270.f * sc, bh = 8.f * sc;
            float bx = valX + 10.f * sc, by = cy - bh * 0.5f;
            float f = (*it.f - it.mn) / (it.mx - it.mn);
            roundRect(bx, by, bw, bh, bh * 0.5f, C(1.f, 1.f, 1.f, 0.14f * a));
            roundRect(bx, by, Max(bh, bw * f), bh, bh * 0.5f, withAlpha(sel ? kWhite : kPink, a));
            circle(bx + bw * f, cy, 10.f * sc, withAlpha(sel ? kWhite : kText, a));
            circle(bx + bw * f, cy, 10.f * sc, C(0.f, 0.f, 0.f, 0.25f * a), 1.5f * sc);
            vs.align = ALIGN_RIGHT;
            text(valX + valW, cy - vs.size * 0.56f, val.c_str(), vs);
            if (dialogFree && !capturing && n.click && inRect(n.mouse, bx - 12.f * sc, ry, bw + 24.f * sc, ih)) I.dragSlider = k;
            if (I.dragSlider == k && n.mouseDown) {
                float nf = Saturate((n.mouse.x - bx) / bw);
                float nv = it.mn + roundf(nf * (it.mx - it.mn) / it.step) * it.step;
                nv = Clamp(nv, it.mn, it.mx);
                if (nv != *it.f) { *it.f = nv; changed = true; }
            }
        } else if (it.type == ST_OPTIONS) {
            float bx = valX + 10.f * sc, bw = valW - 10.f * sc;
            vs.align = ALIGN_CENTER;
            std::string uv = upper(val);
            float tw = textWidth(uv.c_str(), vs);
            if (tw > bw - 70.f * sc) vs.size *= (bw - 70.f * sc) / tw;
            text(bx + bw * 0.5f, cy - vs.size * 0.56f, uv.c_str(), vs);
            float ax0 = bx + 14.f * sc, ax1 = bx + bw - 14.f * sc;
            drawIcon(ICO_CHEVRON, ax0, cy, 22.f * sc, withAlpha(sel ? kWhite : kTextDim, a), 0.f, 0, kPi);
            drawIcon(ICO_CHEVRON, ax1, cy, 22.f * sc, withAlpha(sel ? kWhite : kTextDim, a));
            if (dialogFree && !capturing && n.click && inRect(n.mouse, bx - 10.f * sc, ry, 50.f * sc, ih)) changed |= changeItem(it, -1, gs, I.setCat);
            if (dialogFree && !capturing && n.click && inRect(n.mouse, bx + bw - 40.f * sc, ry, 50.f * sc, ih)) changed |= changeItem(it, 1, gs, I.setCat);
        }
        if (dialogFree && sel && !consumed) {
            if (n.left) changed |= changeItem(it, -1, gs, I.setCat);
            if (n.right) changed |= changeItem(it, 1, gs, I.setCat);
            if (n.confirm && it.type != ST_SLIDER) changed |= changeItem(it, 0, gs, I.setCat);
        }
    }
    setClip(pc);
    // scroll bar
    if (maxScroll > 0.f) {
        float trackH = listH, thumbH = Max(40.f * sc, trackH * listH / (listH + maxScroll));
        float ty = listY + (trackH - thumbH) * (off / maxScroll);
        roundRect(ix + iw - 16.f * sc, listY, 4.f * sc, trackH, 2.f * sc, withAlpha(kWhite, 0.08f * a));
        roundRect(ix + iw - 16.f * sc, ty, 4.f * sc, thumbH, 2.f * sc, withAlpha(kPink, 0.9f * a));
    }
    // key capture (after drawing so the waiting cell shows this frame)
    if (capturing && dialogFree) {
        if (I.bindCaptureFresh) I.bindCaptureFresh = false;
        else if (n.in) {
            bool cancel = false, clear = false;
            int key = capturedKey(*n.in, &cancel, &clear);
            if (cancel) I.bindCapture = false;
            else if (clear) {
                gs.keyBinds[I.bindAction][I.bindSlot] = 0;
                I.bindCapture = false;
                changed = true;
            } else if (key > 0) {
                I.bindCapture = false;
                int otherSlot = 0;
                int other = bindingConflict(gs, I.bindAction, key, &otherSlot);
                if (other >= 0) {
                    I.bindPendingKey = key;
                    I.bindOtherAction = other;
                    I.bindOtherSlot = otherSlot;
                    openDialog(DLG_BIND_CONFLICT, -1, 0);
                } else if (gs.keyBinds[I.bindAction][I.bindSlot] != key) {
                    gs.keyBinds[I.bindAction][I.bindSlot] = (u16)key;
                    if (gs.keyBinds[I.bindAction][1 - I.bindSlot] == key) gs.keyBinds[I.bindAction][1 - I.bindSlot] = 0;
                    changed = true;
                }
            }
        }
    }
    // description
    if (I.setItemsFocus && I.setCursor < count) {
        TextStyle ds = style(FONT_BODY, 20.f * sc, withAlpha(kTextDim, a));
        rect(ix + 30.f * sc, y + h - 78.f * sc, iw - 60.f * sc, 1.f * sc, withAlpha(kWhite, 0.1f * a));
        std::string desc = items[I.setCursor].desc;
        if (capturing) desc = "Press a key or mouse button to bind it. Escape cancels, Delete clears the key.";
        else if (!conflictNote.empty()) {
            ds.color = withAlpha(C(1.f, 0.55f, 0.55f), a);
            desc = conflictNote + " Rebind one of them to resolve the conflict.";
        }
        textWrapped(ix + 30.f * sc, y + h - 62.f * sc, iw - 60.f * sc, desc.c_str(), ds);
    }
    return changed;
}

// Footer prompts of the settings page (fills up to 3 items)
int settingsPrompts(PromptItem* pi, const char* backLabel) {
    if (I.bindCapture) {
        pi[0] = {"DEL", "X", "Clear"};
        pi[1] = {"ESC", "B", "Cancel"};
        return 2;
    }
    if (I.setCat == SC_BINDINGS && I.setItemsFocus) {
        pi[0] = {"ENTER", "A", "Rebind"};
        pi[1] = {"DEL", "X", "Clear"};
        pi[2] = {"ESC", "B", backLabel};
        return 3;
    }
    pi[0] = {"ENTER", "A", "Select"};
    pi[1] = {"LEFTRIGHT", "DPADLR", "Change"};
    pi[2] = {"ESC", "B", backLabel};
    return 3;
}

// ------------------------------------------------------------------------------------------------------------------
// Save / load slot list inside [x, y, w, h]
MenuAction drawSlots(MenuState& st, const Layout& L, const Nav& n, float x, float y, float w, float h, float a, bool save, bool inGame,
                     bool& exit, float dt) {
    MenuAction act;
    float sc = L.s;
    panel(x, y, w, h, a);
    TextStyle hs = style(FONT_HEADING, 30.f * sc, withAlpha(kWhite, a));
    hs.tracking = 0.06f;
    text(x + 30.f * sc, y + 22.f * sc, save ? "SAVE GAME" : (inGame ? "LOAD GAME" : "SAVED GAMES"), hs);
    TextStyle sub = style(FONT_BODY, 19.f * sc, withAlpha(kTextDim, a), ALIGN_RIGHT);
    text(x + w - 30.f * sc, y + 30.f * sc, save ? "Choose a slot to save your progress" : "Choose a saved game to continue", sub);
    rect(x + 30.f * sc, y + 66.f * sc, w - 60.f * sc, 1.f * sc, withAlpha(kWhite, 0.1f * a));
    int count = (int)st.slots.size();
    if (count == 0) {
        TextStyle es = style(FONT_HEADING, 26.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(x + w * 0.5f, y + h * 0.5f - 13.f * sc, "NO SAVE SLOTS AVAILABLE", es);
        if (n.back && I.dialog == DLG_NONE) exit = true;
        return act;
    }
    bool dialogFree = I.dialog == DLG_NONE;
    float rowH = 84.f * sc, listY = y + 84.f * sc;
    int visible = Max(1, (int)((h - 100.f * sc) / (rowH + 6.f * sc)));
    if (dialogFree) {
        if (n.up) st.cursor = (st.cursor + count - 1) % count;
        if (n.down) st.cursor = (st.cursor + 1) % count;
        if (n.wheel != 0.f) I.scrollTarget -= n.wheel;
    }
    st.cursor = Clamp(st.cursor, 0, count - 1);
    if (!I.mouseMode) {
        if (st.cursor < (int)I.scrollTarget) I.scrollTarget = (float)st.cursor;
        if (st.cursor >= (int)I.scrollTarget + visible) I.scrollTarget = (float)(st.cursor - visible + 1);
    }
    I.scrollTarget = Clamp(I.scrollTarget, 0.f, (float)Max(0, count - visible));
    I.scroll = approachExp(I.scroll, I.scrollTarget, 16.f, dt);
    ClipState pc = getClip();
    setClipRect(x, listY - 4.f * sc, w, visible * (rowH + 6.f * sc) + 4.f * sc);
    int activate = -1;
    {
        // the selection bar glides between slots
        if (I.slotHl < 0.f || fabsf(I.slotHl - st.cursor) > 6.f) I.slotHl = (float)st.cursor;
        I.slotHl = approachExp(I.slotHl, (float)st.cursor, 18.f, dt);
        float hy = listY + (I.slotHl - I.scroll) * (rowH + 6.f * sc);
        float hshake = I.denyT < 0.35f ? sinf(I.denyT * 60.f) * 6.f * sc * (1.f - I.denyT / 0.35f) : 0.f;
        selectionBar(x + 14.f * sc + hshake, hy, w - 28.f * sc, rowH, a);
    }
    for (int k = 0; k < count; k++) {
        float ry = listY + (k - I.scroll) * (rowH + 6.f * sc);
        if (ry < listY - rowH || ry > y + h) continue;
        const SaveSlotInfo& si = st.slots[k];
        bool hov = dialogFree && inRect(n.mouse, x + 14.f * sc, ry, w - 28.f * sc, rowH) && n.mouse.y >= listY;
        if (hov && n.mouseMoved) st.cursor = k;
        if (hov && n.click) activate = k;
        bool sel = k == st.cursor;
        float shake = (sel && I.denyT < 0.35f) ? sinf(I.denyT * 60.f) * 6.f * sc * (1.f - I.denyT / 0.35f) : 0.f;
        float rx = x + 14.f * sc + shake;
        if (!sel) roundRect(rx, ry, w - 28.f * sc, rowH, 6.f * sc, C(1.f, 1.f, 1.f, 0.04f * a));
        TextStyle ns = style(FONT_HEADING, 18.f * sc, withAlpha(sel ? kWhite : kPink, a));
        ns.tracking = 0.14f;
        text(rx + 24.f * sc, ry + 12.f * sc, StrFormat("SLOT %d", k + 1).c_str(), ns);
        TextStyle ts = style(FONT_HEADING, 28.f * sc, withAlpha(si.used ? kWhite : kTextMute, a));
        text(rx + 24.f * sc, ry + 34.f * sc, si.used ? si.title.c_str() : "EMPTY SLOT", ts);
        if (si.used) {
            TextStyle ds = style(FONT_BODY, 18.f * sc, withAlpha(sel ? kWhite : kTextDim, a), ALIGN_RIGHT);
            text(rx + w - 28.f * sc - 24.f * sc, ry + 18.f * sc, si.detail.c_str(), ds);
            text(rx + w - 28.f * sc - 24.f * sc, ry + 48.f * sc, si.timestamp.c_str(), ds);
        }
    }
    setClip(pc);
    if (count > visible) {
        float tr = h - 110.f * sc;
        float th = tr * visible / count;
        float ty = listY + (tr - th) * (I.scroll / Max(1.f, (float)(count - visible)));
        roundRect(x + w - 10.f * sc, listY, 4.f * sc, tr, 2.f * sc, C(1.f, 1.f, 1.f, 0.1f * a));
        roundRect(x + w - 10.f * sc, ty, 4.f * sc, th, 2.f * sc, withAlpha(kPink, a));
    }
    if (dialogFree && n.confirm) activate = st.cursor;
    I.denyT += dt;
    if (activate >= 0 && dialogFree) {
        st.cursor = activate;
        const SaveSlotInfo& si = st.slots[activate];
        if (save) {
            if (si.used) openDialog(DLG_OVERWRITE, activate);
            else { act.type = MA_SAVE_SLOT; act.slot = activate; }
        } else {
            if (!si.used) I.denyT = 0.f;
            else if (inGame) openDialog(DLG_LOAD, activate);
            else { act.type = MA_LOAD_SLOT; act.slot = activate; }
        }
    }
    if (dialogFree && n.back) exit = true;
    return act;
}

// ------------------------------------------------------------------------------------------------------------------
// Stats / brief / quit tab contents
void drawStats(MenuState& st, const Layout& L, const Nav& n, float x, float y, float w, float h, float a, float dt) {
    float sc = L.s;
    panel(x, y, w, h, a);
    TextStyle hs = style(FONT_HEADING, 30.f * sc, withAlpha(kWhite, a));
    hs.tracking = 0.06f;
    text(x + 30.f * sc, y + 22.f * sc, "STATISTICS", hs);
    rect(x + 30.f * sc, y + 66.f * sc, w - 60.f * sc, 1.f * sc, withAlpha(kWhite, 0.1f * a));
    int count = (int)st.stats.size();
    if (count == 0) {
        TextStyle es = style(FONT_HEADING, 24.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(x + w * 0.5f, y + h * 0.5f, "NO STATISTICS RECORDED YET", es);
        return;
    }
    float rowH = 44.f * sc;
    int cols = 2;
    float colW = (w - 60.f * sc - 30.f * sc) / cols;
    int rowsVisible = Max(1, (int)((h - 100.f * sc) / rowH));
    int rows = (count + cols - 1) / cols;
    if (I.dialog == DLG_NONE) {
        if (n.down || n.pageDown) I.scrollTarget += n.pageDown ? rowsVisible : 1;
        if (n.up || n.pageUp) I.scrollTarget -= n.pageUp ? rowsVisible : 1;
        I.scrollTarget -= n.wheel * 2.f;
    }
    I.scrollTarget = Clamp(I.scrollTarget, 0.f, (float)Max(0, rows - rowsVisible));
    I.scroll = approachExp(I.scroll, I.scrollTarget, 14.f, dt);
    ClipState pc = getClip();
    float listY = y + 80.f * sc;
    setClipRect(x, listY, w, rowsVisible * rowH);
    for (int i = 0; i < count; i++) {
        int col = i / rows, row = i % rows;
        if (rows > rowsVisible) { col = i % cols; row = i / cols; }
        float rx = x + 30.f * sc + col * (colW + 30.f * sc);
        float ry = listY + (row - I.scroll) * rowH;
        if (ry < listY - rowH || ry > listY + rowsVisible * rowH) continue;
        if (row % 2 == 0) rect(rx, ry, colW, rowH, C(1.f, 1.f, 1.f, 0.035f * a));
        TextStyle ns = style(FONT_BODY, 21.f * sc, withAlpha(kTextDim, a));
        text(rx + 14.f * sc, ry + rowH * 0.5f - ns.size * 0.6f, st.stats[i].name.c_str(), ns);
        TextStyle vs = style(FONT_HEADING, 23.f * sc, withAlpha(kWhite, a), ALIGN_RIGHT);
        text(rx + colW - 14.f * sc, ry + rowH * 0.5f - vs.size * 0.56f, st.stats[i].value.c_str(), vs);
    }
    setClip(pc);
}

void drawBrief(MenuState& st, const Layout& L, const Nav& n, float x, float y, float w, float h, float a, float t, float dt) {
    float sc = L.s;
    bool pad = n.pad;
    bool hasTarget = st.hasWaypoint || !st.gpsRoute.empty();
    float mapW = hasTarget && mapReady() ? Min(w * 0.4f, 700.f * sc) : 0.f;
    float tw = w - (mapW > 0.f ? mapW + 20.f * sc : 0.f);
    panel(x, y, tw, h, a);
    roundRect(x, y + 30.f * sc, 5.f * sc, 60.f * sc, 2.5f * sc, withAlpha(kPink, a));
    TextStyle ks = style(FONT_HEADING, 19.f * sc, withAlpha(kPink, a));
    ks.tracking = 0.2f;
    text(x + 36.f * sc, y + 26.f * sc, "CURRENT MISSION", ks);
    TextStyle hs = style(FONT_HEADING, 44.f * sc, withAlpha(kWhite, a));
    std::string title = st.briefTitle.empty() ? std::string("FREE ROAM") : upper(st.briefTitle);
    text(x + 36.f * sc, y + 50.f * sc, title.c_str(), hs);
    rect(x + 36.f * sc, y + 112.f * sc, tw - 72.f * sc, 1.f * sc, withAlpha(kWhite, 0.1f * a));
    TextStyle bs = style(FONT_BODY, 24.f * sc, withAlpha(kText, a));
    RichOpts ro;
    ro.alpha = a;
    ro.pad = pad;
    ro.lineSpacing = 1.45f;
    std::string body = st.briefText.empty()
                           ? std::string("No active mission. Explore Porto Sol, check the ~p~map~s~ for contacts marked with their initials, or take on side jobs around Palmera.")
                           : st.briefText;
    // long mission logs scroll (arrows / D-pad / right stick / wheel) inside the panel
    float bodyW = Min(tw - 72.f * sc, 1100.f * sc), bodyTop = y + 136.f * sc, bodyH = h - 136.f * sc - 28.f * sc;
    float textH = richMeasure(body.c_str(), bs, bodyW, ro).y;
    float maxScroll = Max(0.f, textH - bodyH);
    I.briefScrollable = maxScroll > 0.f;
    if (st.briefText != I.briefSeen) {
        I.briefSeen = st.briefText;
        I.briefScroll = I.briefScrollShown = 0.f;
    }
    float step = 70.f * sc;
    if (n.down) I.briefScroll += step;
    if (n.up) I.briefScroll -= step;
    I.briefScroll -= n.wheel * step;
    if (n.in) I.briefScroll -= n.in->pad.rightStick.y * 900.f * sc * dt;
    I.briefScroll = Clamp(I.briefScroll, 0.f, maxScroll);
    I.briefScrollShown = approachExp(I.briefScrollShown, I.briefScroll, 14.f, dt);
    ClipState bc = getClip();
    setClipRect(x, bodyTop - 4.f * sc, tw, bodyH + 8.f * sc);
    richDraw(x + 36.f * sc, bodyTop - I.briefScrollShown, body.c_str(), bs, bodyW, ro);
    setClip(bc);
    if (maxScroll > 0.f) {
        float trackH = bodyH, thumbH = Max(40.f * sc, trackH * bodyH / textH);
        float ty = bodyTop + (trackH - thumbH) * (I.briefScrollShown / maxScroll);
        roundRect(x + tw - 18.f * sc, bodyTop, 4.f * sc, trackH, 2.f * sc, withAlpha(kWhite, 0.08f * a));
        roundRect(x + tw - 18.f * sc, ty, 4.f * sc, thumbH, 2.f * sc, withAlpha(kPink, 0.9f * a));
    }
    if (mapW <= 0.f) return;
    // destination inset: small map framed on the route end / waypoint, with the route and the destination pin
    float mx = x + tw + 20.f * sc, my = y, mh = h;
    panel(mx, my, mapW, mh, a);
    vec2 dest = st.hasWaypoint ? st.waypoint : st.gpsRoute.back();
    float ix = mx + 16.f * sc, iy = my + 60.f * sc, iw = mapW - 32.f * sc, ih = mh - 136.f * sc;
    TextStyle ts = style(FONT_HEADING, 19.f * sc, withAlpha(kPink, a));
    ts.tracking = 0.2f;
    text(mx + 24.f * sc, my + 22.f * sc, "DESTINATION", ts);
    vec2 span = dest - st.playerPos;
    float dist = length(span);
    vec2 center = (dest + st.playerPos) * 0.5f;
    float mpp = Clamp(Max(fabsf(span.x) / (iw * 0.75f), fabsf(span.y) / (ih * 0.75f)), 1.2f / sc, 14.f / sc);
    ClipState pc = getClip();
    setClipRoundRect(ix, iy, iw, ih, 10.f * sc);
    MapView v;
    v.center = center;
    v.mpp = mpp;
    v.screenCenter = vec2(ix + iw * 0.5f, iy + ih * 0.5f);
    MapDrawOpts o;
    o.style = MAPSTYLE_FULL;
    o.alpha = a;
    o.extentMin = vec2(ix, iy);
    o.extentMax = vec2(ix + iw, iy + ih);
    drawMapBase(v, o);
    if (!st.gpsRoute.empty()) drawMapRoute(v, st.gpsRoute, C(1.f, 0.4f, 0.8f), Max(3.f, 4.f * sc), a, vec2(ix, iy), vec2(ix + iw, iy + ih));
    Blip wb;
    wb.pos = dest;
    wb.icon = BLIP_WAYPOINT;
    drawBlipGlyph(wb, v.toScreen(dest), 34.f * sc, a, t, false, false);
    vec2 pp = v.toScreen(st.playerPos);
    drawIcon(BLIP_PLAYER, pp.x, pp.y, 30.f * sc, withAlpha(kWhite, a), 1.6f * sc, C(0.02f, 0.02f, 0.06f, a), v.screenAngle(st.playerHeading));
    setClip(pc);
    roundRect(ix, iy, iw, ih, 10.f * sc, 0, 1.f * sc, withAlpha(kWhite, 0.12f * a));
    TextStyle ds = style(FONT_HEADING, 26.f * sc, withAlpha(kWhite, a));
    std::string dn = upper(mapDistrictAt(dest));
    text(mx + 24.f * sc, my + mh - 64.f * sc, dn.c_str(), ds);
    TextStyle ms = style(FONT_BODY, 19.f * sc, withAlpha(kTextDim, a), ALIGN_RIGHT);
    std::string dl = dist >= 1000.f ? StrFormat("%.1f km", dist / 1000.f) : StrFormat("%d m", (int)dist);
    text(mx + mapW - 24.f * sc, my + mh - 58.f * sc, dl.c_str(), ms);
}

MenuAction drawQuitTab(MenuState& st, const Layout& L, const Nav& n, float x, float y, float w, float h, float a, float dt) {
    MenuAction act;
    float sc = L.s;
    (void)h;
    (void)dt;
    struct Opt { const char* title; const char* desc; int icon; DialogKind dlg; };
    Opt opts[2] = {{"Quit to Main Menu", "Return to the title screen. Progress since your last save will be lost.", ICO_MAP, DLG_QUIT_MENU},
                   {"Quit to Desktop", "Close NEON TIDE. Progress since your last save will be lost.", ICO_POWER, DLG_QUIT_GAME}};
    bool dialogFree = I.dialog == DLG_NONE;
    if (dialogFree) {
        if (n.up || n.left) st.cursor = (st.cursor + 1) % 2;
        if (n.down || n.right) st.cursor = (st.cursor + 1) % 2;
    }
    st.cursor = Clamp(st.cursor, 0, 1);
    float bw = Min(560.f * sc, (w - 30.f * sc) * 0.5f), bh = 220.f * sc;
    for (int i = 0; i < 2; i++) {
        float bx = x + i * (bw + 30.f * sc), by = y;
        bool hov = dialogFree && inRect(n.mouse, bx, by, bw, bh);
        if (hov && n.mouseMoved) st.cursor = i;
        bool sel = st.cursor == i;
        panel(bx, by, bw, bh, a);
        if (sel) {
            roundRect(bx, by, bw, bh, 14.f * sc, 0, 2.f * sc, withAlpha(kPink, a));
            roundRect(bx, by, 6.f * sc, bh, 3.f * sc, withAlpha(kPink, a));
        }
        drawIcon(opts[i].icon, bx + 60.f * sc, by + 64.f * sc, 56.f * sc, withAlpha(sel ? kPink : kTextDim, a));
        TextStyle ts = style(FONT_HEADING, 32.f * sc, withAlpha(sel ? kWhite : kText, a));
        ts.tracking = 0.05f;
        text(bx + 104.f * sc, by + 44.f * sc, upper(opts[i].title).c_str(), ts);
        TextStyle ds = style(FONT_BODY, 20.f * sc, withAlpha(kTextDim, a));
        textWrapped(bx + 36.f * sc, by + 120.f * sc, bw - 72.f * sc, opts[i].desc, ds);
        if (hov && n.click) openDialog(opts[i].dlg, -1);
    }
    if (dialogFree && n.confirm) openDialog(opts[st.cursor].dlg, -1);
    return act;
}

// ------------------------------------------------------------------------------------------------------------------
// Pause container
int tabFromScreen(MenuScreen s, int cur) {
    switch (s) {
    case MENU_MAP: return PT_MAP;
    case MENU_BRIEF: return PT_BRIEF;
    case MENU_STATS: return PT_STATS;
    case MENU_SETTINGS: return PT_SETTINGS;
    case MENU_SAVE: return PT_SAVE;
    default: return cur;
    }
}

MenuAction updatePause(MenuState& st, const Layout& L, const Nav& n, float dt, float t, bool& changed) {
    MenuAction act;
    float sc = L.s;
    int tab = Clamp(st.tab, 0, PT_COUNT - 1);
    if (tab == PT_SAVE && !st.canSave) tab = PT_MAP;
    float a = easeOutCubic(I.openT / 0.3f);
    float ca = easeOutCubic(I.tabT / 0.3f);
    if (tab != PT_MAP) {
        fullBackdrop(L, a, 0.5f, 0.5f);
        vignette(L, a);
    }
    float cx = L.left, cy = 200.f * sc, cw = L.right - L.left, ch = L.H - cy - 110.f * sc;
    float slide = (1.f - ca) * 24.f * sc;
    bool exit = false;
    switch (tab) {
    case PT_MAP: {
        MenuAction m = drawMap(st, L, n, dt, t, true);
        if (m.type != MA_NONE) act = m;
        if (I.dialog == DLG_NONE && n.back) exit = true;
        break;
    }
    case PT_BRIEF:
        drawBrief(st, L, n, cx, cy + slide, cw, ch, a * ca, t, dt);
        if (I.dialog == DLG_NONE && n.back) exit = true;
        break;
    case PT_STATS:
        drawStats(st, L, n, cx, cy + slide, cw, ch, a * ca, dt);
        if (I.dialog == DLG_NONE && n.back) exit = true;
        break;
    case PT_SETTINGS:
        changed |= drawSettings(st, L, n, cx, cy + slide, cw, ch, a * ca, dt, exit);
        break;
    case PT_SAVE: {
        MenuAction m = drawSlots(st, L, n, cx, cy + slide, cw, ch, a * ca, true, true, exit, dt);
        if (m.type != MA_NONE) act = m;
        break;
    }
    case PT_QUIT: {
        MenuAction m = drawQuitTab(st, L, n, cx, cy + slide + 40.f * sc, cw, ch, a * ca, dt);
        if (m.type != MA_NONE) act = m;
        if (I.dialog == DLG_NONE && n.back) exit = true;
        break;
    }
    }
    int newTab = drawPauseHeader(st, L, n, tab, a, t);
    if (newTab != tab) {
        st.tab = newTab;
        st.screen = tabScreen(newTab);
        st.cursor = 0;
        I.tabT = 0.f;
        I.scroll = I.scrollTarget = 0.f;
        I.setItemsFocus = false;
        I.setRowHl = I.setCatHl = -1.f;
        I.mapOpen = false;
    } else {
        st.tab = tab;
        MenuScreen want = tabScreen(tab);
        if (st.screen != want) st.screen = want;
    }
    // footer prompts
    if (tab == PT_MAP) {
        PromptItem pi[] = {{"LMB", "A", st.hasWaypoint ? "Waypoint" : "Set Waypoint"}, {"RMB", "X", "Clear"}, {"WHEEL", "RT", "Zoom"},
                           {"C", "Y", "Center"}, {"L", "BACK", "Legend"}, {"ESC", "B", "Resume"}};
        footer(L, pi, 6, n.pad, a);
    } else if (tab == PT_SETTINGS) {
        PromptItem pi[3];
        int np = settingsPrompts(pi, I.setItemsFocus ? "Back" : "Resume");
        footer(L, pi, np, n.pad, a);
    } else if (tab == PT_SAVE) {
        PromptItem pi[] = {{"ENTER", "A", "Save"}, {"ESC", "B", "Resume"}};
        footer(L, pi, 2, n.pad, a);
    } else if (tab == PT_STATS) {
        PromptItem pi[] = {{"UPDOWN", "DPADUD", "Scroll"}, {"ESC", "B", "Resume"}};
        footer(L, pi, 2, n.pad, a);
    } else if (tab == PT_QUIT) {
        PromptItem pi[] = {{"ENTER", "A", "Select"}, {"ESC", "B", "Resume"}};
        footer(L, pi, 2, n.pad, a);
    } else if (tab == PT_BRIEF && I.briefScrollable) {
        PromptItem pi[] = {{"UPDOWN", "RS", "Scroll"}, {"ESC", "B", "Resume"}};
        footer(L, pi, 2, n.pad, a);
    } else {
        PromptItem pi[] = {{"ESC", "B", "Resume"}};
        footer(L, pi, 1, n.pad, a);
    }
    if (n.start && I.dialog == DLG_NONE && I.openT > 0.2f) exit = true;   // not the press that opened the menu
    if (exit && act.type == MA_NONE) {
        act.type = MA_RESUME;
        st.screen = MENU_NONE;
    }
    return act;
}

// Front-end page frame (settings / load from the main menu)
void frontPageFrame(const Layout& L, const char* title, float a, float t) {
    float sc = L.s;
    fullBackdrop(L, 1.f, 0.45f, 0.55f);
    vignette(L, 1.f);
    gradientRect(0, 0, L.W, 200.f * sc, C(0.01f, 0.015f, 0.05f, 0.9f), C(0.01f, 0.015f, 0.05f, 0.f));
    drawLogo(L.left, L.top - 6.f * sc, 46.f * sc, a, t, false);
    TextStyle ts = style(FONT_HEADING, 54.f * sc, withAlpha(kWhite, a));
    ts.tracking = 0.08f;
    text(L.left, L.top + 74.f * sc, title, ts);
}

}  // namespace menus_ui

// ------------------------------------------------------------------------------------------------------------------
namespace Menus {

const std::vector<DisplayMode>& displayModes() {
    static std::vector<DisplayMode> modes;
    static bool built = false;
    if (built) return modes;
    built = true;
    DEVMODEA dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); i++) {
        int w = (int)dm.dmPelsWidth, h = (int)dm.dmPelsHeight;
        if (w < 1024 || h < 600 || dm.dmBitsPerPel < 24) continue;
        bool dup = false;
        for (auto& m : modes)
            if (m.width == w && m.height == h) dup = true;
        if (!dup) modes.push_back({w, h});
    }
    std::sort(modes.begin(), modes.end(), [](const DisplayMode& a, const DisplayMode& b) {
        return a.width * a.height > b.width * b.height || (a.width * a.height == b.width * b.height && a.width > b.width);
    });
    if (modes.empty()) modes = {{2560, 1440}, {1920, 1080}, {1600, 900}, {1280, 720}};
    return modes;
}

MenuAction update(MenuState& st, const InputState& in, float dt) {
    using namespace menus_ui;
    MenuAction act;
    uix::ensureIcons();
    uix::advanceTime(dt);
    dt = Clamp(dt, 0.f, 0.1f);
    Layout L = layout();
    float t = uiTime();
    if (st.screen == MENU_NONE) {
        I.lastScreen = MENU_NONE;
        I.dialog = DLG_NONE;
        return act;
    }
    // screen transitions
    if (st.screen != I.lastScreen) {
        MenuScreen prev = I.lastScreen;
        bool fromNone = prev == MENU_NONE || prev == MENU_LOADING;
        if (st.screen == MENU_MAIN) I.root = MENU_MAIN;
        else if (st.screen == MENU_PAUSE || st.screen == MENU_MAP || st.screen == MENU_BRIEF || st.screen == MENU_STATS || st.screen == MENU_SAVE)
            I.root = MENU_PAUSE;
        else if (st.screen == MENU_SETTINGS || st.screen == MENU_LOAD || st.screen == MENU_CONFIRM_QUIT) {
            if (prev == MENU_MAIN || st.prevScreen == MENU_MAIN) I.root = MENU_MAIN;
            else if (fromNone && st.prevScreen != MENU_MAIN) I.root = MENU_PAUSE;
        }
        if (fromNone) {
            I.openT = 0.f;
            I.dialog = DLG_NONE;
            I.mapOpen = false;
        }
        bool tabSwitch = I.root == MENU_PAUSE && !fromNone && st.screen != MENU_CONFIRM_QUIT && st.screen != MENU_LOAD;
        if (!tabSwitch) {
            I.screenT = 0.f;
            I.hlY = -1.f;
        }
        if (I.root == MENU_PAUSE) {
            int nt = tabFromScreen(st.screen, fromNone ? (st.screen == MENU_PAUSE ? 0 : st.tab) : st.tab);
            if (nt != st.tab || fromNone) {
                st.tab = nt;
                I.tabT = 0.f;
                I.scroll = I.scrollTarget = 0.f;
                if (fromNone) st.cursor = 0;
            }
        }
        if (st.screen == MENU_CONFIRM_QUIT) openDialog(I.root == MENU_MAIN ? DLG_QUIT_GAME : DLG_QUIT_MENU, -1, 1);
        if (st.screen == MENU_SETTINGS) { I.setItemsFocus = false; I.setCatHl = I.setRowHl = -1.f; }
        I.lastScreen = st.screen;
    }
    I.screenT += dt;
    I.openT += dt;
    I.tabT += dt;
    bool wasdNav = !(I.root == MENU_PAUSE && st.tab == PT_MAP && st.screen != MENU_CONFIRM_QUIT);
    Nav n = readInput(in, dt, wasdNav);
    Nav blocked = n;
    if (I.dialog != DLG_NONE || I.bindCapture) {
        // the dialog (or a key-binding capture, which reads raw input) owns keyboard/pad input; pages still draw and
        // receive the mouse position
        blocked.up = blocked.down = blocked.left = blocked.right = false;
        blocked.confirm = blocked.back = blocked.tabL = blocked.tabR = blocked.btnX = blocked.btnY = false;
        blocked.start = blocked.pageUp = blocked.pageDown = false;
        blocked.click = blocked.release = blocked.rclick = false;
        blocked.wheel = 0.f;
        blocked.stick = vec2(0.f);
        blocked.lt = blocked.rt = 0.f;
    }
    bool changed = false;
    switch (st.screen) {
    case MENU_LOADING:
        drawLoading(st, L, dt, t);
        return act;
    case MENU_MAIN:
        act = drawMain(st, L, blocked, dt, t);
        break;
    case MENU_SETTINGS:
        if (I.root == MENU_MAIN) {
            float a = easeOutCubic(I.screenT / 0.35f);
            frontPageFrame(L, "SETTINGS", a, t);
            bool exit = false;
            float cy = 200.f * L.s;
            changed |= drawSettings(st, L, blocked, L.left, cy + (1.f - a) * 20.f * L.s, L.right - L.left, L.H - cy - 110.f * L.s, a, dt, exit);
            PromptItem pi[3];
            int np = settingsPrompts(pi, "Back");
            footer(L, pi, np, n.pad, a);
            if (exit) {
                st.screen = MENU_MAIN;
                st.cursor = st.canContinue ? 3 : 2;
            }
        } else {
            act = updatePause(st, L, blocked, dt, t, changed);
        }
        break;
    case MENU_LOAD: {
        float a = easeOutCubic(I.screenT / 0.35f);
        if (I.root == MENU_MAIN) frontPageFrame(L, "LOAD GAME", a, t);
        else { fullBackdrop(L, 1.f, 0.45f, 0.55f); vignette(L, 1.f); }
        bool exit = false;
        float cy = 200.f * L.s;
        MenuAction m = drawSlots(st, L, blocked, L.left, cy + (1.f - a) * 20.f * L.s, L.right - L.left, L.H - cy - 110.f * L.s, a, false,
                                 I.root == MENU_PAUSE, exit, dt);
        if (m.type != MA_NONE) act = m;
        PromptItem pi[] = {{"ENTER", "A", "Load"}, {"ESC", "B", "Back"}};
        footer(L, pi, 2, n.pad, a);
        if (exit) {
            if (I.root == MENU_MAIN) { st.screen = MENU_MAIN; st.cursor = st.canContinue ? 2 : 1; }
            else { st.screen = st.prevScreen != MENU_NONE && st.prevScreen != MENU_LOAD ? st.prevScreen : MENU_PAUSE; }
        }
        break;
    }
    case MENU_CONFIRM_QUIT:
        if (I.root == MENU_MAIN) drawMain(st, L, blocked, dt, t);
        else { fullBackdrop(L, 1.f, 0.45f, 0.55f); vignette(L, 1.f); }
        break;
    default:
        act = updatePause(st, L, blocked, dt, t, changed);
        break;
    }
    // modal dialog on top
    if (I.dialog != DLG_NONE) {
        const char* title = "";
        std::string msg;
        const char* yes = "Yes";
        const char* no = "No";
        switch (I.dialog) {
        case DLG_OVERWRITE:
            title = "OVERWRITE SAVE?";
            msg = StrFormat("Slot %d already contains ~p~%s~s~. Replace it with your current progress?", I.dialogSlot + 1,
                            I.dialogSlot >= 0 && I.dialogSlot < (int)st.slots.size() ? st.slots[I.dialogSlot].title.c_str() : "a saved game");
            yes = "Overwrite";
            no = "Cancel";
            break;
        case DLG_LOAD:
            title = "LOAD GAME?";
            msg = "Any progress since your last save will be lost.";
            yes = "Load";
            no = "Cancel";
            break;
        case DLG_QUIT_MENU:
            title = "QUIT TO MAIN MENU?";
            msg = "Any progress since your last save will be lost.";
            yes = "Quit";
            no = "Cancel";
            break;
        case DLG_QUIT_GAME:
            title = "QUIT NEON TIDE?";
            msg = I.root == MENU_MAIN ? "Are you sure you want to exit to the desktop?" : "Any progress since your last save will be lost.";
            yes = "Quit";
            no = "Cancel";
            break;
        case DLG_NEW_GAME:
            title = "START A NEW GAME?";
            msg = "Any progress since your last save will be lost.";
            break;
        case DLG_BIND_CONFLICT: {
            title = "KEY ALREADY IN USE";
            int others = 0;
            for (int b = 0; b < IA_COUNT; b++)
                if (b != I.bindAction && contextsClash(I.bindAction, b) &&
                    (st.settings.keyBinds[b][0] == I.bindPendingKey || st.settings.keyBinds[b][1] == I.bindPendingKey))
                    others++;
            int old = I.bindAction >= 0 ? st.settings.keyBinds[I.bindAction][I.bindSlot] : 0;
            msg = StrFormat("~p~%s~s~ is already bound to ~p~%s~s~%s. %s", keyName(I.bindPendingKey).c_str(),
                            inputActionName((InputAction)I.bindOtherAction), others > 1 ? StrFormat(" and %d more", others - 1).c_str() : "",
                            old > 0 ? StrFormat("Swap it so %s uses ~p~%s~s~?", inputActionName((InputAction)I.bindOtherAction), keyName(old).c_str()).c_str()
                                    : "Move it here and leave the other action unbound?");
            yes = old > 0 ? "Swap" : "Move";
            no = "Cancel";
            break;
        }
        default: break;
        }
        DialogResult r = drawDialog(L, n, title, msg.c_str(), yes, no, dt);
        if (r.yes) {
            switch (I.dialog) {
            case DLG_OVERWRITE: act.type = MA_SAVE_SLOT; act.slot = I.dialogSlot; break;
            case DLG_LOAD: act.type = MA_LOAD_SLOT; act.slot = I.dialogSlot; break;
            case DLG_QUIT_MENU: act.type = MA_QUIT_TO_MENU; break;
            case DLG_QUIT_GAME: act.type = MA_QUIT_GAME; break;
            case DLG_NEW_GAME: act.type = MA_NEW_GAME; break;
            case DLG_BIND_CONFLICT:
                if (I.bindAction >= 0 && I.bindAction < IA_COUNT) {
                    GameSettings& gs = st.settings;
                    u16 key = (u16)I.bindPendingKey, old = gs.keyBinds[I.bindAction][I.bindSlot];
                    for (int b = 0; b < IA_COUNT; b++) {
                        if (b == I.bindAction || !contextsClash(I.bindAction, b)) continue;
                        for (int k = 0; k < 2; k++)
                            if (gs.keyBinds[b][k] == key) gs.keyBinds[b][k] = gs.keyBinds[b][1 - k] == old ? 0 : old;
                    }
                    gs.keyBinds[I.bindAction][I.bindSlot] = key;
                    if (gs.keyBinds[I.bindAction][1 - I.bindSlot] == key) gs.keyBinds[I.bindAction][1 - I.bindSlot] = 0;
                    changed = true;
                }
                break;
            default: break;
            }
            I.dialog = DLG_NONE;
        } else if (r.no) {
            I.dialog = DLG_NONE;
            if (st.screen == MENU_CONFIRM_QUIT) {
                MenuScreen back = st.prevScreen;
                if (back == MENU_CONFIRM_QUIT || back == MENU_NONE) {
                    if (I.root == MENU_MAIN) back = MENU_MAIN;
                    else {
                        st.screen = MENU_NONE;
                        act.type = MA_RESUME;
                        back = MENU_NONE;
                    }
                }
                st.screen = back;
            }
        }
    }
    if (act.type == MA_NONE && changed) act.type = MA_SETTINGS_CHANGED;
    if (changed) applyUiSettings(st.settings);   // HUD-side options preview live; the app applies the rest on MA_SETTINGS_CHANGED
    st.anim = I.screenT;
    return act;
}

}  // namespace Menus
}  // namespace UI
