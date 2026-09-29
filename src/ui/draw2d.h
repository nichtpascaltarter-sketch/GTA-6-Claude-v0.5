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
};

bool init();
void shutdown();
void beginFrame(int screenW, int screenH);
void endFrame();  // flushes to the currently bound back buffer

inline u32 rgba(float r, float g, float b, float a = 1.f) { return packRGBA8(r, g, b, a); }
inline u32 withAlpha(u32 c, float a) { return (c & 0x00ffffffu) | ((u32)(Saturate(a) * ((c >> 24) & 255)) << 24); }

void rect(float x, float y, float w, float h, u32 color);
void roundRect(float x, float y, float w, float h, float radius, u32 color, float border = 0.f, u32 borderColor = 0);
void gradientRect(float x, float y, float w, float h, u32 top, u32 bottom);
void circle(float cx, float cy, float r, u32 color, float thickness = 0.f);  // thickness 0 = filled
void line(float x0, float y0, float x1, float y1, float width, u32 color);
void image(ID3D11ShaderResourceView* srv, float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1,
           float v1 = 1, u32 tint = 0xffffffff);
// Rotated textured quad around center (for minimap)
void imageRotated(ID3D11ShaderResourceView* srv, float cx, float cy, float w, float h, float angle, float u0, float v0,
                  float u1, float v1, u32 tint = 0xffffffff);
void setClipCircle(float cx, float cy, float r);  // clip following draws to a circle (minimap); r<=0 disables
void setClipRect(float x, float y, float w, float h);  // w<=0 disables

float text(float x, float y, const char* str, const TextStyle& style);  // returns width
float textWidth(const char* str, const TextStyle& style);
void textWrapped(float x, float y, float maxW, const char* str, const TextStyle& style, float lineSpacing = 1.25f);
int screenWidth();
int screenHeight();

}  // namespace UI
