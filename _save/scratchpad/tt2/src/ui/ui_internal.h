// Shared internals of the game UI (HUD, map, menus): palette, layout scale, procedural icon atlas, rich text with
// inline color codes and input prompts, map rendering. Included only by the UI implementation files.
#pragma once
#include "hud.h"

namespace UI {
namespace uix {

// ------------------------------------------------------------------------------------------------------------------
// NEON TIDE palette (RGBA8, r in the low byte: use rgba()).
inline u32 C(float r, float g, float b, float a = 1.f) { return rgba(r, g, b, a); }
extern const u32 kNavy, kNavyDeep, kNavyPanel, kInk, kPink, kPinkHot, kCyan, kCyanDeep, kWhite, kText, kTextDim, kTextMute,
    kGold, kYellow, kRed, kRedDeep, kGreen, kGreenMoney, kBlue, kOrange, kPurple, kHealth, kHealthLow, kArmor, kFocus;

// Layout: design at 1080p, scale by height, anchor to a centered 16:9 safe area.
struct Layout {
    float W = 1920, H = 1080, s = 1;     // screen size, scale (H / 1080)
    float x0 = 0, x1 = 1920;             // 16:9 region
    float left = 0, right = 0, top = 0, bottom = 0;  // safe margins applied (HUD anchors)
    float px(float v) const { return v * s; }
};
Layout layout();

// Easing
inline float easeOutCubic(float t) { t = Saturate(t); float u = 1.f - t; return 1.f - u * u * u; }
inline float easeInCubic(float t) { t = Saturate(t); return t * t * t; }
inline float easeInOutCubic(float t) { t = Saturate(t); return t < 0.5f ? 4.f * t * t * t : 1.f - powf(-2.f * t + 2.f, 3.f) * 0.5f; }
inline float easeOutBack(float t, float k = 1.70158f) { t = Saturate(t); float u = t - 1.f; return 1.f + (k + 1.f) * u * u * u + k * u * u; }
inline float easeOutQuint(float t) { t = Saturate(t); float u = 1.f - t; return 1.f - u * u * u * u * u; }
// Moves `v` toward `target` with a frame-rate independent exponential.
inline float approachExp(float v, float target, float rate, float dt) { return v + (target - v) * (1.f - expf(-rate * dt)); }

// ------------------------------------------------------------------------------------------------------------------
// Procedural SDF icon atlas
enum IconId : int {
    ICO_BLIP0 = 0,                    // + BlipIcon
    ICO_STAR = BLIP_COUNT, ICO_STAR_OUTLINE, ICO_HEART, ICO_SHIELD, ICO_BUBBLES, ICO_BOLT, ICO_MESSAGE, ICO_PHONE, ICO_MUSIC,
    ICO_CLOCK, ICO_CHECK, ICO_CROSS, ICO_GEAR, ICO_SAVE, ICO_MAP, ICO_STATS, ICO_BRIEF, ICO_POWER, ICO_MONITOR, ICO_SPEAKER,
    ICO_GAMEPAD, ICO_USER, ICO_MOUSE, ICO_MOUSE_L, ICO_MOUSE_R, ICO_MOUSE_WHEEL, ICO_DPAD, ICO_ARROW_UP, ICO_CHEVRON,
    ICO_PLAYER_RING, ICO_PIN_DOT, ICO_LOAD, ICO_SUN, ICO_PALM, ICO_WAVE, ICO_COUNT
};
constexpr int kWeaponIconCount = 13;

void ensureIcons();
bool iconsReady();
// Draw an icon centered at (cx, cy); `size` is the full cell size in pixels (content fills ~72%).
void drawIcon(int id, float cx, float cy, float size, u32 color, float outlinePx = 0.f, u32 outlineColor = 0,
              float angle = 0.f, float soft = 0.f);
// Soft shadow / glow under an icon (blurred SDF).
void drawIconGlow(int id, float cx, float cy, float size, u32 color, float spreadPx);
// Weapon silhouette (index = HudState::weaponIcon) centered at (cx, cy) with the given width (height = width * 0.375).
void drawWeapon(int weapon, float cx, float cy, float width, u32 color, float outlinePx = 0.f, u32 outlineColor = 0,
                float soft = 0.f);
u32 blipDefaultColor(BlipIcon icon);
const char* blipDefaultName(BlipIcon icon);
bool blipIsRound(BlipIcon icon);   // plain dot-like blips (drawn as circles, turn into up/down arrows with height)

// ------------------------------------------------------------------------------------------------------------------
// Input prompts (keyboard keycaps, mouse buttons, procedural gamepad glyphs)
// Pad names: A B X Y LB RB LT RT LS RS START BACK UP DOWN LEFT RIGHT DPAD LSTICK RSTICK
float promptWidth(const char* key, bool pad, float h);
float drawPrompt(float x, float y, const char* key, bool pad, float h, float alpha = 1.f);   // returns width
// A row of "glyph + label" pairs right-aligned at (xRight, y). items: {kbKey, padKey, label}
struct PromptItem { const char* kb; const char* pad; const char* label; };
float drawPromptBar(float xRight, float y, const PromptItem* items, int n, bool pad, float h, float alpha = 1.f);

// ------------------------------------------------------------------------------------------------------------------
// Rich text: color codes ~r~ ~g~ ~b~ ~y~ ~o~ ~p~ ~c~ ~m~ ~w~ ~l~ (grey) ~s~ (reset), ~n~ (new line),
// prompts ~k:KEY~ (keyboard), ~p:BTN~ (gamepad), ~i:KEY|BTN~ (adapts to the last input device).
u32 codeColor(char code, u32 def);
struct RichOpts {
    bool pad = false;          // gamepad prompts for ~i:~ tokens
    float lineSpacing = 1.3f;
    float alpha = 1.f;
};
// Measures (maxW <= 0: no wrapping). Returns size (w, h).
vec2 richMeasure(const char* str, const TextStyle& st, float maxW, const RichOpts& o = RichOpts());
// Draws with st.align applied per line relative to x (left: x is the left edge, center: x is the center,
// right: x is the right edge). Returns the size.
vec2 richDraw(float x, float y, const char* str, const TextStyle& st, float maxW, const RichOpts& o = RichOpts());
std::string stripCodes(const std::string& s);

// ------------------------------------------------------------------------------------------------------------------
// Formatting
std::string fmtMoney(long long v, bool sign = false);   // "$148,200" / "+$500"
std::string fmtTime(float seconds);                     // "2:05" / "1:02:05"
std::string fmtClock(float hours);                      // "14:25"
std::string upper(const std::string& s);

// ------------------------------------------------------------------------------------------------------------------
// Map data + vector map rendering (shared by the radar and the full-screen map)
struct MapView {
    vec2 center;          // world position at `screenCenter`
    float mpp = 2.f;      // meters per pixel
    float rot = 0.f;      // radians; world direction `rot` (heading, CCW from north) points up on screen
    vec2 screenCenter;
    // cached sin/cos of rot (recomputed when rot changes)
    mutable float cachedRot = 1e30f, cr = 1.f, sr = 0.f;
    void sync() const {
        if (rot != cachedRot) { cachedRot = rot; cr = cosf(rot); sr = sinf(rot); }
    }
    vec2 toScreen(vec2 w) const {
        sync();
        float inv = 1.f / mpp;
        vec2 d = (w - center) * inv;
        // rotate by -rot, flip y for screen space
        return screenCenter + vec2(cr * d.x + sr * d.y, -(-sr * d.x + cr * d.y));
    }
    vec2 toWorld(vec2 sp) const {
        sync();
        vec2 r(sp.x - screenCenter.x, -(sp.y - screenCenter.y));
        vec2 d(cr * r.x - sr * r.y, sr * r.x + cr * r.y);
        return center + d * mpp;
    }
    // Screen-space rotation to apply to an upright glyph that should point along world heading h.
    float screenAngle(float heading) const { return -(heading - rot); }
};

enum MapStyle { MAPSTYLE_RADAR = 0, MAPSTYLE_FULL = 1 };
struct MapDrawOpts {
    MapStyle style = MAPSTYLE_RADAR;
    float alpha = 1.f;
    float extentPx = 300.f;       // half-size of the visible screen region around screenCenter (for culling)
    vec2 extentMin, extentMax;    // optional screen rect (full map), used when extentMax.x > extentMin.x
    bool buildings = true;
    bool dim = false;             // interior / disabled look
};
bool mapReady();
void mapInit();
void drawMapBase(const MapView& v, const MapDrawOpts& o);
void drawMapRoute(const MapView& v, const std::vector<vec2>& route, u32 color, float widthPx, float alpha, vec2 cullMin,
                  vec2 cullMax);
// District / water labels for the full map
struct MapLabel { vec2 pos; std::string name; float importance; bool water; };
const std::vector<MapLabel>& mapLabels();
// Name of the district / nearest street at a world position (for the map cursor)
std::string mapDistrictAt(vec2 p);
std::string mapStreetAt(vec2 p, float maxDist);
// World bounds of the land (for the full-map framing)
void mapLandBounds(vec2& mn, vec2& mx);

// Blip rendering shared by radar and map: draws at screen position p with the pixel size, height indicator.
void drawBlipGlyph(const Blip& b, vec2 p, float sizePx, float alpha, float time, bool showHeight, bool onEdge);

float uiTime();   // seconds (monotonic, advanced by drawHud / Menus::update)
void advanceTime(float dt);

}  // namespace uix
}  // namespace UI
