// Drawing/world-data doubles for menu INPUT tests. No window, GPU or save files.
// Production menus.cpp, settings.cpp and ui_common.cpp run unchanged; pixels and
// text metrics are not under test. Keep these stubs separate from gameplay code.
#include "../../src/ui/ui_internal.h"
#include <cstdarg>

std::string StrFormat(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list count;
    va_copy(count, args);
    int size = std::vsnprintf(nullptr, 0, fmt, count);
    va_end(count);
    if (size < 0) { va_end(args); return {}; }
    std::vector<char> buffer(static_cast<size_t>(size) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);
    return std::string(buffer.data(), static_cast<size_t>(size));
}

namespace UI {
static int testFrame = 0;
bool init() { return true; }
void shutdown() {}
void beginFrame(int, int) { ++testFrame; }
void endFrame() {}
u32 lerpColor(u32 a, u32 b, float t) { return t < 0.5f ? a : b; }
void rect(float, float, float, float, u32) {}
void roundRect(float, float, float, float, float, u32, float, u32) {}
void gradientRect(float, float, float, float, u32, u32) {}
void circle(float, float, float, u32, float) {}
void line(float, float, float, float, float, u32) {}
void image(gfx::SRV, float, float, float, float, float, float, float, float, u32) {}
void imageRotated(gfx::SRV, float, float, float, float, float, float, float, float, float, u32) {}
void setClipCircle(float, float, float) {}
void setClipRect(float, float, float, float) {}
float textWidth(const char* str, const TextStyle& style) {
    return str ? static_cast<float>(std::strlen(str)) * style.size * 0.5f : 0.f;
}
float text(float, float, const char* str, const TextStyle& style) { return textWidth(str, style); }
float textWrapped(float, float, float, const char* str, const TextStyle& style, float lineSpacing) {
    return textWidth(str, style) > 0.f ? style.size * lineSpacing : 0.f;
}
int screenWidth() { return 1920; }
int screenHeight() { return 1080; }
void gradientRectH(float, float, float, float, u32, u32) {}
void gradientRect4(float, float, float, float, u32, u32, u32, u32) {}
void roundRectGradient(float, float, float, float, float, u32, u32, float, u32) {}
void roundRectRotated(vec2, vec2, float, float, float, u32) {}
void circleSoft(float, float, float, float, u32) {}
void ringSoft(float, float, float, float, float, u32) {}
void capsule(float, float, float, float, float, u32, float) {}
void polyline(const vec2*, int, float, u32, bool, float) {}
void arc(float, float, float, float, float, float, u32, float) {}
void triangle(vec2, vec2, vec2, u32) {}
void triangle3(vec2, vec2, vec2, u32, u32, u32) {}
void quad(vec2, vec2, vec2, vec2, u32) {}
void quad4(vec2, vec2, vec2, vec2, u32, u32, u32, u32) {}
void polygon(const vec2*, int, u32) {}
void setIconAtlas(gfx::SRV) {}
void iconSdf(float, float, float, float, float, float, float, float, u32, float, float, u32, float, float) {}
void mapQuad(gfx::SRV, const vec2[4], const vec2[4], u32, u32, float) {}
void backdrop(float, float, float, float, float, u32, u32, float) {}
void photoEffect(float, float, float, float, float, float, float, float, const PhotoFx&) {}
void setSceneDepth(gfx::SRV, float) {}
void setColorMatrix(const float*) {}
int requestSnapshot() { return 0; }
gfx::SRV snapshotSrv(int) { return nullptr; }
void setClipRoundRect(float, float, float, float, float) {}
ClipState getClip() { return {}; }
void setClip(const ClipState&) {}
void setAdditive(bool) {}
void discardDrawsUntilEndFrame() {}
int vertexCount() { return 0; }
int frameIndex() { return testFrame; }

namespace uix {
void ensureIcons() {}
bool iconsReady() { return true; }
void drawIcon(int, float, float, float, u32, float, u32, float, float) {}
void drawIconGlow(int, float, float, float, u32, float) {}
void drawWeapon(int, float, float, float, u32, float, u32, float) {}
u32 blipDefaultColor(BlipIcon) { return 0xffffffff; }
const char* blipDefaultName(BlipIcon) { return "Test blip"; }
bool blipIsRound(BlipIcon) { return true; }
bool mapReady() { return true; }
void mapInit() {}
void drawMapBase(const MapView&, const MapDrawOpts&) {}
void drawMapRoute(const MapView&, const std::vector<vec2>&, u32, float, float, vec2, vec2) {}
const std::vector<MapLine>& mapLines() { static const std::vector<MapLine> lines; return lines; }
void drawMapLines(const MapView&, float, float, vec2, vec2, bool) {}
const std::vector<MapLabel>& mapLabels() { static const std::vector<MapLabel> labels; return labels; }
void drawStreetNames(const MapView&, vec2, vec2, float, float, std::vector<vec4>&) {}
std::string mapDistrictAt(vec2) { return "Test district"; }
std::string mapStreetAt(vec2, float) { return {}; }
void mapLandBounds(vec2& mn, vec2& mx) { mn = vec2(-10000.f); mx = vec2(10000.f); }
void drawBlipGlyph(const Blip&, vec2, float, float, float, bool, bool) {}
float drawSubtitleBlock(float, float bottom, float, float, const std::string&, u32, const std::string&, bool, float) { return bottom; }
void drawReticleShape(vec2, float, float, int, float) {}
} // namespace uix
} // namespace UI
