// Batched 2D drawing for HUD, menus and debug overlays. Coordinates in pixels, origin top-left.
#pragma once
#include "../gfx/gfx.h"

namespace UI {

enum FontId { FONT_BODY = 0, FONT_HEADING, FONT_TITLE, FONT_COUNT };

enum Align { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

struct TextStyle {
    FontId font = FONT_BODY;
    float size = 20.f;         // pixel height (em)
    u32 color = 0xffffffff;    // RGBA8 (r in low byte)
    float outline = 0.f;       // outline width in pixels
    u32 outlineColor = 0xff000000;
    float shadow = 0.f;        // drop shadow offset in pixels
    Align align = ALIGN_LEFT;
    float tracking = 0.f;      // extra spacing in em units
    // Extensions (defaults keep the original look):
    u32 colorBottom = 0;       // != 0: vertical gradient from color (top of the line) to colorBottom (baseline)
    float skew = 0.f;          // italic slant (x shift per pixel of height above the baseline, e.g. 0.2)
    float glow = 0.f;          // soft outer glow radius in pixels (0 = none), drawn under the text
    u32 glowColor = 0;         // glow color (alpha = strength)
    float shadowSoft = 0.f;    // > 0 blurs the drop shadow (0..1)
    float angle = 0.f;         // rotation in radians (clockwise on screen) around the anchor (x, y + size * 0.55)
};

bool init();
void shutdown();
void beginFrame(int screenW, int screenH);
void endFrame();  // flushes to the currently bound back buffer

inline u32 rgba(float r, float g, float b, float a = 1.f) { return packRGBA8(r, g, b, a); }
inline u32 withAlpha(u32 c, float a) { return (c & 0x00ffffffu) | ((u32)(Saturate(a) * ((c >> 24) & 255)) << 24); }
// Linear blend of two RGBA8 colors.
u32 lerpColor(u32 a, u32 b, float t);

void rect(float x, float y, float w, float h, u32 color);
void roundRect(float x, float y, float w, float h, float radius, u32 color, float border = 0.f, u32 borderColor = 0);
void gradientRect(float x, float y, float w, float h, u32 top, u32 bottom);
void circle(float cx, float cy, float r, u32 color, float thickness = 0.f);  // thickness 0 = filled
void line(float x0, float y0, float x1, float y1, float width, u32 color);
void image(gfx::SRV  srv, float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1,
           float v1 = 1, u32 tint = 0xffffffff);
// Rotated textured quad around center (for minimap)
void imageRotated(gfx::SRV  srv, float cx, float cy, float w, float h, float angle, float u0, float v0,
                  float u1, float v1, u32 tint = 0xffffffff);
void setClipCircle(float cx, float cy, float r);  // clip following draws to a circle (minimap); r<=0 disables
void setClipRect(float x, float y, float w, float h);  // w<=0 disables

float text(float x, float y, const char* str, const TextStyle& style);  // returns width
float textWidth(const char* str, const TextStyle& style);
// Word-wraps into lines no wider than maxW; returns the total height used (lines * size * lineSpacing).
float textWrapped(float x, float y, float maxW, const char* str, const TextStyle& style, float lineSpacing = 1.25f);
int screenWidth();
int screenHeight();

// ------------------------------------------------------------------------------------------------------------------
// Extensions (all anti-aliased analytically in the pixel shader)
// Horizontal gradient.
void gradientRectH(float x, float y, float w, float h, u32 left, u32 right);
// Four-corner gradient (top-left, top-right, bottom-right, bottom-left).
void gradientRect4(float x, float y, float w, float h, u32 tl, u32 tr, u32 br, u32 bl);
// Rounded rect with a vertical gradient fill.
void roundRectGradient(float x, float y, float w, float h, float radius, u32 top, u32 bottom, float border = 0.f,
                       u32 borderColor = 0);
// Rotated rounded rectangle: center, unit axis of the local x direction, half extents.
void roundRectRotated(vec2 center, vec2 axis, float hw, float hh, float radius, u32 color);
// Soft-edged circle: the edge fades over `feather` pixels centered on radius r (glow: r = R/2, feather = R).
void circleSoft(float cx, float cy, float r, float feather, u32 color);
// Ring outline with soft edges.
void ringSoft(float cx, float cy, float r, float thickness, float feather, u32 color);
// Round-capped line (capsule) of the given total width; feather > 0 softens the edge (glow lines).
void capsule(float x0, float y0, float x1, float y1, float width, u32 color, float feather = 0.f);
// Polyline made of capsules (round joins).
void polyline(const vec2* pts, int n, float width, u32 color, bool closed = false, float feather = 0.f);
// Ring segment centered on angle `midAngle` (radians, 0 = up, clockwise positive) spanning `halfAngle` each side.
// `gap` (pixels, halfAngle < 90 deg only) trims both ends by gap/2 with parallel cuts (segmented rings).
void arc(float cx, float cy, float radius, float thickness, float midAngle, float halfAngle, u32 color, float gap = 0.f);
// Filled anti-aliased triangle / convex quad / arbitrary simple polygon (ear clipping, any winding).
void triangle(vec2 a, vec2 b, vec2 c, u32 color);
void triangle3(vec2 a, vec2 b, vec2 c, u32 ca, u32 cb, u32 cc);   // per-vertex colors
void quad(vec2 a, vec2 b, vec2 c, vec2 d, u32 color);
void quad4(vec2 a, vec2 b, vec2 c, vec2 d, u32 ca, u32 cb, u32 cc, u32 cd);
void polygon(const vec2* pts, int n, u32 color);
// SDF icon from the atlas registered with setIconAtlas(): uv rect in the atlas, pxRange = screen pixels per SDF unit
// (distance field value 0..1 spans `spreadPx` atlas pixels; callers normally use the helpers in the HUD code).
void setIconAtlas(gfx::SRV  srv);
void iconSdf(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color, float pxRange,
             float outline = 0.f, u32 outlineColor = 0, float soft = 0.f, float angle = 0.f);
// World map texture (RGB land color, A = encoded water depth) rendered with crisp analytic coastlines. Corners are
// given in screen space with their texture coordinates (p0..p3 clockwise). landTint/waterTint multiply the colors.
void mapQuad(gfx::SRV  srv, const vec2 p[4], const vec2 uv[4], u32 landTint, u32 waterTint,
             float coastLine = 1.f);
// Frosted glass: blurred copy of what was rendered before the UI (the 3D frame), tinted, desaturated and mixed with
// `overlay` (alpha = mix amount), clipped to a rounded rect. Enables backdrop capture for this frame.
void backdrop(float x, float y, float w, float h, float radius, u32 tint, u32 overlay, float saturation = 1.f);
// Photo grading of the 3D frame captured before the UI (photo mode, phone camera): draws the screen region
// (u0, v0)-(u1, v1) (0..1 of the screen) into the rect with depth of field, exposure, white balance, a filter,
// contrast, saturation, vignette and grain. Opaque. One parameter set per frame (the last call wins).
struct PhotoFx {
    int filter = 0;              // 0 natural, 1 neon nights, 2 golden hour, 3 noir, 4 vintage, 5 chrome, 6 vapor,
                                 // 7 sepia, 8 tropic, 9 pixel
    float strength = 1.f;        // filter mix 0..1
    float exposure = 0.f;        // EV
    float contrast = 1.f, saturation = 1.f;
    float temperature = 0.f;     // -1 cool .. +1 warm
    float vignette = 0.f, grain = 0.f;
    bool dof = false;
    float focusDistance = 10.f;  // meters (screen-space focus band around the center when no depth is set)
    float aperture = 2.8f;       // f-stop
    float fovY = 0.87f;          // radians (narrow lenses blur more)
    float time = 0.f;            // seconds (grain animation)
};
void photoEffect(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const PhotoFx& fx);
// Depth buffer of the 3D frame for photoEffect's depth of field: reversed-Z infinite projection (value = nearZ /
// view distance, 0 at infinity), any resolution. Call every frame before endFrame; nullptr = none.
void setSceneDepth(gfx::SRV  depthSrv, float nearZ);
// Colour-blind correction applied to everything the UI draws (row-major 3x3 in linear RGB; nullptr = off). The frame
// underneath is corrected by the renderer's post-process, so captured scene pixels (backdrop blur, photo grading,
// snapshots) are left untouched.
void setColorMatrix(const float* m9);
// Snapshots: at the end of this frame's endFrame the finished frame is copied into a small persistent texture
// (quarter resolution). Returns its id; the 8 most recent snapshots are kept.
int requestSnapshot();
gfx::SRV  snapshotSrv(int id);   // nullptr when unknown, not captured yet or overwritten
// Clip following draws to an anti-aliased rounded rectangle (w <= 0 disables).
void setClipRoundRect(float x, float y, float w, float h, float radius);
// Current clip state save/restore.
struct ClipState { vec4 clip; int mode; float radius; };
ClipState getClip();
void setClip(const ClipState& c);
// Additive blending for following draws (glows, light sweeps): output alpha is zeroed so colors add up.
void setAdditive(bool additive);
// Ignore every further draw call until endFrame (test overlays use this to keep debug text off screenshots).
void discardDrawsUntilEndFrame();
// Number of vertices submitted this frame (profiling).
int vertexCount();
// Incremented by every beginFrame (lets animation clocks advance once per frame).
int frameIndex();

}  // namespace UI
