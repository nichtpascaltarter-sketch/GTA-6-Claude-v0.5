// In-game HUD: radar with vector map, status bars, wanted level, money, weapon, weapon wheel, reticle, damage
// indicators, help / objective / subtitles, big messages, notifications, radio, vehicle and location names,
// speedometer / altimeter, mission timer and counters. All animation state lives here; HudState stays const.
#include "ui_internal.h"

namespace UI {
namespace hud_ui {

using namespace uix;

struct Popup {
    long long amount;
    float t;
};
struct DamageInd {
    float ang;
    float age;
    float fade;   // 1 while present in HudState, decays after removal
    bool seen;
};

struct State {
    // money
    bool moneyInit = false;
    double moneyShown = 0;
    long long lastMoney = 0, lastDelta = 0;
    std::vector<Popup> popups;
    float popupSpace = 0.f;
    float moneyPulse = 0;
    // wanted
    float wantedAlpha = 0;
    int lastWanted = 0;
    float wantedChangeT = 10.f;
    float wantedLevelShown = 0;
    // weapon
    float weaponAlpha = 0;
    int lastWeaponIcon = -2;
    float weaponChangeT = 10.f;
    std::string lastWeaponName;
    // wheel
    float wheelAnim = 0;
    int wheelSelShown = -1;
    float wheelSelT = 0;
    std::vector<std::string> wheelSlots;
    std::vector<int> wheelIcons;
    // reticle / markers
    float reticleAlpha = 0;
    float spreadShown = 0;
    float hitIntensity = 0, hitPop = 0, killT = 10.f;
    float lastHit = 0;
    // damage
    std::vector<DamageInd> dmg;
    float lastHealth = 1.f;
    float hurtFlash = 0;
    // lock-on marker
    float lockA = 0.f;          // visibility 0..1
    float lockAcquire = 1.f;    // time since the current target was acquired
    vec2 lockPos;               // last drawn head position
    float lockHealthShown = 1.f, lockHealthLag = 1.f, lockPulse = 0.f, lockBarA = 0.f;
    bool lockHostile = false, lockMelee = false;
    // texts
    std::string help;
    float helpAlpha = 0, helpT = 0;
    std::string objective;
    float objAlpha = 0, objT = 0;
    Subtitle sub;
    float subAlpha = 0;
    std::string big, bigSub;
    u32 bigColor = 0;
    float bigAlpha = 0, bigTime = 0;
    std::string note, noteTitle;
    float noteAlpha = 0, noteT = 0;
    std::string radio, track;
    float radioAlpha = 0, radioT = 0;
    std::string vehicle;
    float vehAlpha = 0;
    std::string zone, street;
    float locAlpha = 0;
    float timerAlpha = 0, counterAlpha = 0;
    std::string counterLabel;
    int counterVal = 0, counterMax = 0;
    float timerVal = 0;
    // radar
    float radarAlpha = 0;
    float zoomShown = 1.f;
    float healthShown = 1.f, healthLag = 1.f, armorShown = 0.f, armorLag = 0.f;
    float breathAlpha = 0, staminaAlpha = 0;
    float specialShown = 1.f, focusFx = 0.f;
    float speedAlpha = 0, speedShown = 0;
    float altAlpha = 0, altShown = 0;
    float lowHealth = 0;
    bool first = true;
};

State g;

float fadeTo(float v, bool on, float dt, float inRate = 8.f, float outRate = 5.f) {
    return on ? Min(1.f, v + dt * inRate) : Max(0.f, v - dt * outRate);
}

// Settings > Accessibility > Reduce Flashing: blinking and alternating effects become steady or slow blends
bool calm() { return uiOptions().reduceFlashing; }
// Police red / blue: alternating, or a slow even blend when flashing is reduced
u32 policeColor(float t) {
    if (calm()) return lerpColor(kRed, kBlue, 0.5f + 0.5f * sinf(t * 1.2f));
    return fmodf(t * 1.6f, 1.f) < 0.5f ? kRed : kBlue;
}
// 0..1 pulse wave, a constant mid level when flashing is reduced
float pulseWave(float t, float rate) { return calm() ? 0.5f : 0.5f + 0.5f * sinf(t * rate); }

// Signed distance to a rounded rectangle (center c, half size hs, radius r)
float sdRR(vec2 p, vec2 c, vec2 hs, float r) {
    vec2 q = vec2(fabsf(p.x - c.x), fabsf(p.y - c.y)) - hs + vec2(r);
    return length(vmax(q, vec2(0.f))) + Min(Max(q.x, q.y), 0.f) - r;
}

// Clamp point p toward origin o so it lies inside the rounded rect; returns true when clamping happened.
bool clampToRR(vec2 o, vec2& p, vec2 c, vec2 hs, float r) {
    if (sdRR(p, c, hs, r) <= 0.f) return false;
    float lo = 0.f, hi = 1.f;
    vec2 d = p - o;
    for (int i = 0; i < 22; i++) {
        float mid = (lo + hi) * 0.5f;
        if (sdRR(o + d * mid, c, hs, r) <= 0.f) lo = mid;
        else hi = mid;
    }
    p = o + d * lo;
    return true;
}

int blipPriority(BlipIcon ic) {
    switch (ic) {
    case BLIP_DOT: return 0;
    case BLIP_ENEMY: case BLIP_FRIEND: case BLIP_POLICE: return 1;
    case BLIP_POLICE_HELI: case BLIP_VEHICLE: return 3;
    case BLIP_MISSION: return 4;
    case BLIP_OBJECTIVE: return 5;
    case BLIP_WAYPOINT: return 6;
    default: return 2;
    }
}

TextStyle style(FontId f, float size, u32 color, Align al = ALIGN_LEFT) {
    TextStyle st;
    st.font = f;
    st.size = size;
    st.color = color;
    st.align = al;
    return st;
}

// ------------------------------------------------------------------------------------------------------------------
// Radar
struct RadarRect {
    float x, y, w, h, r;
    vec2 center() const { return vec2(x + w * 0.5f, y + h * 0.5f); }
};

RadarRect radarRect(const Layout& L) {
    RadarRect rr;
    rr.w = 304.f * L.s;
    rr.h = 216.f * L.s;
    rr.x = L.left;
    rr.y = L.bottom - 26.f * L.s - rr.h;
    rr.r = 16.f * L.s;
    return rr;
}

void drawRadar(const HudState& s, const Layout& L, float dt, float t) {
    float a = g.radarAlpha;
    if (a <= 0.001f) return;
    RadarRect rr = radarRect(L);
    float sc = L.s;
    // backing: soft shadow + frame
    for (int i = 3; i >= 1; i--) {
        float e = i * 2.5f * sc;
        roundRect(rr.x - e, rr.y - e + sc, rr.w + 2 * e, rr.h + 2 * e, rr.r + e, C(0.f, 0.f, 0.02f, 0.10f * a));
    }
    roundRect(rr.x, rr.y, rr.w, rr.h, rr.r, C(0.03f, 0.05f, 0.11f, 0.94f * a));
    ClipState prevClip = getClip();
    setClipRoundRect(rr.x, rr.y, rr.w, rr.h, rr.r);
    vec2 pc(rr.x + rr.w * 0.5f, rr.y + rr.h * 0.64f);
    g.zoomShown = approachExp(g.zoomShown, Clamp(s.radarZoom, 0.4f, 6.f), 3.f, dt);
    MapView v;
    v.center = s.playerPos;
    v.mpp = 1.75f * g.zoomShown / sc;
    v.rot = s.cameraHeading;
    v.screenCenter = pc;
    MapDrawOpts o;
    o.style = MAPSTYLE_RADAR;
    o.alpha = a;
    float ex = Max(fabsf(pc.x - rr.x), fabsf(rr.x + rr.w - pc.x)), ey = Max(fabsf(pc.y - rr.y), fabsf(rr.y + rr.h - pc.y));
    o.extentPx = sqrtf(ex * ex + ey * ey) + 4.f;
    o.buildings = true;
    o.dim = s.interior;
    if (mapReady()) drawMapBase(v, o);
    else rect(rr.x, rr.y, rr.w, rr.h, C(0.10f, 0.13f, 0.18f, a));
    vec2 cmin(rr.x, rr.y), cmax(rr.x + rr.w, rr.y + rr.h);
    if (mapReady()) drawMapLines(v, a, sc, cmin, cmax, true);
    // police search areas
    if (s.wanted > 0) {
        u32 policeCol = policeColor(t);
        for (size_t i = 0; i < s.searchAreaCenters.size() && i < s.searchAreaRadii.size(); i++) {
            vec2 c = v.toScreen(s.searchAreaCenters[i]);
            float r = s.searchAreaRadii[i] / v.mpp;
            u32 col = policeCol;
            circle(c.x, c.y, r, withAlpha(col, 0.13f * a));
            circle(c.x, c.y, r, withAlpha(col, 0.45f * a), 1.5f * sc);
        }
    }
    // GPS route
    if (!s.gpsRoute.empty()) drawMapRoute(v, s.gpsRoute, s.gpsColor, 5.5f * sc, a, cmin, cmax);
    // blips
    vec2 rc = rr.center();
    float inset = 11.f * sc;
    vec2 hs(rr.w * 0.5f - inset, rr.h * 0.5f - inset);
    float ir = Max(rr.r - inset, 2.f * sc);
    bool hasWaypointBlip = false;
    static std::vector<int> order;
    order.clear();
    for (int i = 0; i < (int)s.blips.size(); i++) {
        if (s.blips[i].icon == BLIP_WAYPOINT) hasWaypointBlip = true;
        if (s.blips[i].icon == BLIP_PLAYER) continue;
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return blipPriority(s.blips[x].icon) < blipPriority(s.blips[y].icon); });
    auto drawOne = [&](const Blip& b) {
        vec2 d = b.pos - s.playerPos;
        float dist = length(d);
        if (b.shortRange && dist > 250.f) return;
        vec2 p = v.toScreen(b.pos);
        bool edge = clampToRR(pc, p, rc, hs, ir);
        if (edge && !b.edge) return;
        float size = (blipIsRound(b.icon) ? 22.f : 27.f) * sc;
        drawBlipGlyph(b, p, size, a * (edge ? 0.92f : 1.f), t, true, edge);
    };
    for (int i : order) drawOne(s.blips[i]);
    if (s.hasWaypoint && !hasWaypointBlip) {
        Blip wb;
        wb.pos = s.waypoint;
        wb.icon = BLIP_WAYPOINT;
        drawOne(wb);
    }
    // north marker on the rim
    {
        vec2 nd = v.toScreen(s.playerPos + vec2(0.f, 10000.f));
        vec2 p = nd;
        clampToRR(pc, p, rc, vec2(rr.w * 0.5f - 12.f * sc, rr.h * 0.5f - 12.f * sc), Max(rr.r - 12.f * sc, 2.f));
        circle(p.x, p.y, 10.5f * sc, C(0.02f, 0.03f, 0.07f, 0.85f * a));
        circle(p.x, p.y, 10.5f * sc, withAlpha(kWhite, 0.25f * a), 1.f * sc);
        TextStyle st = style(FONT_HEADING, 15.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
        text(p.x, p.y - st.size * 0.56f, "N", st);
    }
    // player arrow
    float pa = v.screenAngle(s.playerHeading);
    drawIconGlow(BLIP_PLAYER, pc.x, pc.y, 30.f * sc, C(0.f, 0.f, 0.f, 0.45f * a), 3.f * sc);
    drawIcon(BLIP_PLAYER, pc.x, pc.y, 30.f * sc, withAlpha(kWhite, a), 1.6f * sc, C(0.02f, 0.02f, 0.06f, a), pa);
    // wanted: the radar rim glows red / blue
    if (s.wanted > 0) {
        float ph = fmodf(t * 1.6f, 1.f);
        u32 col = policeColor(t);
        float pulse = calm() ? 0.8f : 0.65f + 0.35f * sinf(ph * kTwoPi * 2.f);
        float e = 34.f * sc;
        u32 c0 = withAlpha(col, 0.42f * pulse * a), c1 = withAlpha(col, 0.f);
        gradientRect(rr.x, rr.y, rr.w, e, c0, c1);
        gradientRect(rr.x, rr.y + rr.h - e, rr.w, e, c1, c0);
        gradientRectH(rr.x, rr.y, e, rr.h, c0, c1);
        gradientRectH(rr.x + rr.w - e, rr.y, e, rr.h, c1, c0);
    }
    // inner depth vignette
    gradientRect(rr.x, rr.y, rr.w, 26.f * sc, C(0.f, 0.f, 0.03f, 0.40f * a), C(0.f, 0.f, 0.03f, 0.f));
    gradientRect(rr.x, rr.y + rr.h - 18.f * sc, rr.w, 18.f * sc, C(0.f, 0.f, 0.03f, 0.f), C(0.f, 0.f, 0.03f, 0.30f * a));
    setClip(prevClip);
    roundRect(rr.x, rr.y, rr.w, rr.h, rr.r, 0, 1.2f * sc, withAlpha(kWhite, 0.16f * a));
    if (s.wanted > 0) {
        roundRect(rr.x - 1.5f * sc, rr.y - 1.5f * sc, rr.w + 3 * sc, rr.h + 3 * sc, rr.r + 1.5f * sc, 0, 2.f * sc,
                  withAlpha(policeColor(t), 0.75f * a));
    }

    // ---------------------------------------------------------------- status bars (GTA-style: health | armor | Focus)
    float by = rr.y + rr.h + 8.f * sc, bh = 9.f * sc, gap = 5.f * sc;
    float bw = (rr.w - gap * 2.f) * 0.5f;           // health: half the width
    float bw2 = (rr.w - gap * 2.f) * 0.25f;         // armor and Focus: a quarter each
    g.healthShown = approachExp(g.healthShown, Saturate(s.health), 12.f, dt);
    g.healthLag = s.health < g.healthLag ? approachExp(g.healthLag, Saturate(s.health), 1.6f, dt) : Saturate(s.health);
    g.armorShown = approachExp(g.armorShown, Saturate(s.armor), 12.f, dt);
    g.armorLag = s.armor < g.armorLag ? approachExp(g.armorLag, Saturate(s.armor), 1.6f, dt) : Saturate(s.armor);
    auto bar = [&](float x, float w, float val, float lag, u32 col, u32 lagCol, bool pulse) {
        roundRect(x, by, w, bh, bh * 0.5f, C(0.02f, 0.03f, 0.07f, 0.72f * a));
        roundRect(x, by, w, bh, bh * 0.5f, 0, 1.f * sc, withAlpha(kWhite, 0.10f * a));
        float inner = 2.f * sc;
        float iw = w - inner * 2.f, ih = bh - inner * 2.f;
        if (lag > val + 0.002f) roundRect(x + inner, by + inner, Max(ih, iw * lag), ih, ih * 0.5f, withAlpha(lagCol, 0.85f * a));
        if (val > 0.002f) {
            u32 c = col;
            if (pulse && !calm()) c = lerpColor(col, kWhite, 0.35f * (0.5f + 0.5f * sinf(t * 9.f)));
            roundRect(x + inner, by + inner, Max(ih, iw * val), ih, ih * 0.5f, withAlpha(c, a));
            // highlight
            rect(x + inner + ih * 0.5f, by + inner, Max(0.f, iw * val - ih), ih * 0.35f, withAlpha(kWhite, 0.18f * a));
        }
    };
    bool lowHp = s.health < 0.25f && !s.dead;
    bar(rr.x, bw, g.healthShown, g.healthLag, lowHp ? kHealthLow : kHealth, C(1.f, 0.85f, 0.85f), lowHp);
    bar(rr.x + bw + gap, bw2, g.armorShown, g.armorLag, kArmor, C(0.85f, 0.92f, 1.f), false);
    {
        g.specialShown = approachExp(g.specialShown, Saturate(s.special), 10.f, dt);
        float sx = rr.x + bw + bw2 + gap * 2.f;
        bool low = s.special < 0.15f;
        u32 col = low ? lerpColor(kFocus, C(0.35f, 0.32f, 0.42f), 0.55f) : kFocus;
        bar(sx, bw2, g.specialShown, g.specialShown, col, col, s.specialActive);
        if (s.specialActive) {
            setAdditive(true);
            float pulse = pulseWave(t, 7.f);
            roundRect(sx - 2.f * sc, by - 2.f * sc, bw2 + 4.f * sc, bh + 4.f * sc, bh * 0.5f + 2.f * sc, withAlpha(kFocus, (0.25f + 0.25f * pulse) * a));
            setAdditive(false);
        }
    }
    // breath / stamina (thin, only while not full)
    g.breathAlpha = fadeTo(g.breathAlpha, s.breath < 0.999f, dt, 6.f, 2.f);
    g.staminaAlpha = fadeTo(g.staminaAlpha, s.stamina < 0.999f && s.breath >= 0.999f, dt, 6.f, 1.5f);
    float ty = by + bh + 5.f * sc, th = 5.f * sc;
    auto thin = [&](float val, u32 col, float al, int icon) {
        if (al <= 0.01f) return;
        float x = rr.x + 18.f * sc, w = rr.w - 18.f * sc;
        drawIcon(icon, rr.x + 7.f * sc, ty + th * 0.5f, 16.f * sc, withAlpha(col, al * a));
        roundRect(x, ty, w, th, th * 0.5f, C(0.02f, 0.03f, 0.07f, 0.6f * al * a));
        roundRect(x, ty, Max(th, w * Saturate(val)), th, th * 0.5f, withAlpha(col, al * a));
    };
    if (g.breathAlpha > 0.01f) thin(s.breath, kCyan, g.breathAlpha, ICO_BUBBLES);
    else thin(s.stamina, kYellow, g.staminaAlpha, ICO_BOLT);

    // ---------------------------------------------------------------- altimeter (aircraft)
    g.altAlpha = fadeTo(g.altAlpha, s.inVehicle && s.aircraft, dt, 5.f, 4.f);
    if (g.altAlpha > 0.01f) {
        float al = g.altAlpha * a;
        g.altShown = approachExp(g.altShown, Max(0.f, s.altitude), 6.f, dt);
        float x = rr.x + rr.w + 10.f * sc, y0 = rr.y + 6.f * sc, h = rr.h - 12.f * sc, w = 8.f * sc;
        roundRect(x, y0, w, h, w * 0.5f, C(0.02f, 0.03f, 0.07f, 0.72f * al));
        float maxAlt = 800.f;
        float f = Saturate(g.altShown / maxAlt);
        roundRect(x + 2 * sc, y0 + h - Max(w, h * f), w - 4 * sc, Max(w - 4 * sc, h * f - 2 * sc), (w - 4 * sc) * 0.5f, withAlpha(kCyan, al));
        for (int k = 1; k < 4; k++) rect(x - 3 * sc, y0 + h * k / 4.f, 3 * sc, 1.f * sc, withAlpha(kWhite, 0.4f * al));
        float my = y0 + h * (1.f - f);
        triangle(vec2(x + w + 2 * sc, my), vec2(x + w + 10 * sc, my - 5 * sc), vec2(x + w + 10 * sc, my + 5 * sc), withAlpha(kWhite, al));
        TextStyle st = style(FONT_HEADING, 20.f * sc, withAlpha(kWhite, al));
        st.shadow = 1.5f * sc;
        std::string alt = StrFormat("%d", (int)(s.metricUnits ? g.altShown : g.altShown * 3.28084f));
        text(x + w + 13 * sc, my - 11 * sc, alt.c_str(), st);
        st.size = 13.f * sc;
        st.color = withAlpha(kTextDim, al);
        text(x + w + 13 * sc, my + 8 * sc, s.metricUnits ? "M ALT" : "FT ALT", st);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Top right: wanted level, money, weapon
void drawTopRight(const HudState& s, const Layout& L, float dt, float t) {
    float sc = L.s;
    float xr = L.right;
    float y = L.top;
    // wanted stars
    if (s.wanted != g.lastWanted) {
        g.wantedChangeT = 0.f;
        g.lastWanted = s.wanted;
    }
    g.wantedChangeT += dt;
    g.wantedAlpha = fadeTo(g.wantedAlpha, s.wanted > 0 || g.wantedChangeT < 2.5f, dt, 6.f, 1.2f);
    g.wantedLevelShown = (float)s.wanted;
    if (g.wantedAlpha > 0.01f) {
        float a = g.wantedAlpha;
        float size = 40.f * sc, step = 37.f * sc;
        bool flashOff = s.wantedSearching && (calm() || fmodf(t * 2.2f, 1.f) > 0.55f);   // steady grey when calm
        for (int i = 0; i < 5; i++) {
            float cx = xr - size * 0.5f - (4 - i) * step;
            float cy = y + size * 0.5f;
            bool on = i < s.wanted;
            if (on) {
                float pop = 1.f;
                if (i == s.wanted - 1 && g.wantedChangeT < 0.5f) pop = 1.f + 0.45f * (1.f - easeOutBack(g.wantedChangeT / 0.5f, 2.2f));
                u32 col = flashOff ? C(0.55f, 0.58f, 0.66f, a) : withAlpha(kWhite, a);
                drawIconGlow(ICO_STAR, cx, cy, size * pop, C(0.f, 0.f, 0.f, 0.35f * a), 3.f * sc);
                drawIcon(ICO_STAR, cx, cy, size * pop, col, 1.6f * sc, C(0.02f, 0.02f, 0.06f, 0.9f * a));
            } else {
                drawIcon(ICO_STAR_OUTLINE, cx, cy, size, C(1.f, 1.f, 1.f, 0.30f * a), 1.2f * sc, C(0.f, 0.f, 0.f, 0.35f * a));
            }
        }
        if (s.wantedSearching && s.wanted > 0) {
            float w = 4 * step + size * 0.7f;
            float x0 = xr - w;
            float by = y + size + 2.f * sc;
            roundRect(x0, by, w, 4.f * sc, 2.f * sc, C(0.f, 0.f, 0.f, 0.45f * a));
            roundRect(x0, by, Max(4.f * sc, w * Saturate(s.wantedCooldown)), 4.f * sc, 2.f * sc, withAlpha(kWhite, 0.85f * a));
        }
        y += (size + 10.f * sc) * easeOutCubic(a);
    }
    // money
    if (!g.moneyInit) {
        g.moneyInit = true;
        g.moneyShown = (double)s.money;
        g.lastMoney = s.money;
    }
    {
        // popup when the balance changes (amount from moneyDelta when the game provides it) or when the game reports a
        // new delta without changing the balance in the same frame
        long long amount = 0;
        if (s.money != g.lastMoney) amount = s.moneyDelta != 0 ? s.moneyDelta : s.money - g.lastMoney;
        else if (s.moneyDelta != 0 && s.moneyDelta != g.lastDelta) amount = s.moneyDelta;
        if (amount != 0) {
            g.popups.push_back({amount, 0.f});
            if (g.popups.size() > 3) g.popups.erase(g.popups.begin());
            g.moneyPulse = 1.f;
        }
        g.lastMoney = s.money;
        g.lastDelta = s.moneyDelta;
    }
    double diff = (double)s.money - g.moneyShown;
    if (fabs(diff) < 1.0) g.moneyShown = (double)s.money;
    else g.moneyShown += diff * (1.0 - exp(-7.0 * dt)) + (diff > 0 ? 1.0 : -1.0);
    g.moneyPulse = Max(0.f, g.moneyPulse - dt * 1.5f);
    {
        TextStyle st = style(FONT_HEADING, 42.f * sc, kWhite, ALIGN_RIGHT);
        st.outline = 2.f * sc;
        st.outlineColor = C(0.f, 0.f, 0.04f, 0.85f);
        st.shadow = 2.f * sc;
        st.shadowSoft = 0.6f;
        long long shown = (long long)llround(g.moneyShown);
        std::string m = fmtMoney(shown).substr(1);
        if (g.moneyPulse > 0.f) st.color = lerpColor(kWhite, g.popups.empty() || g.popups.back().amount >= 0 ? kGreenMoney : kRed, g.moneyPulse);
        float w = text(xr, y, m.c_str(), st);
        TextStyle ds = st;
        ds.color = kGreenMoney;
        text(xr - w - 2.f * sc, y, "$", ds);
        y += 46.f * sc;
    }
    // delta popups
    for (size_t i = 0; i < g.popups.size();) {
        g.popups[i].t += dt;
        if (g.popups[i].t > 3.f) g.popups.erase(g.popups.begin() + i);
        else i++;
    }
    for (size_t i = 0; i < g.popups.size(); i++) {
        const Popup& p = g.popups[g.popups.size() - 1 - i];
        float in = easeOutBack(p.t / 0.35f);
        float out = 1.f - SmoothStep(2.2f, 3.f, p.t);
        float a = Saturate(p.t / 0.15f) * out;
        TextStyle st = style(FONT_HEADING, 30.f * sc, withAlpha(p.amount >= 0 ? kGreenMoney : kRed, a), ALIGN_RIGHT);
        st.outline = 1.6f * sc;
        st.outlineColor = C(0.f, 0.f, 0.04f, 0.8f * a);
        st.shadow = 1.5f * sc;
        std::string txt = fmtMoney(p.amount, true);
        text(xr + (1.f - in) * 30.f * sc, y + i * 30.f * sc, txt.c_str(), st);
    }
    g.popupSpace = approachExp(g.popupSpace, (float)g.popups.size(), 10.f, dt);
    y += (30.f * g.popupSpace + (g.popupSpace > 0.01f ? 4.f : 0.f)) * sc;
    // weapon panel
    if (s.weaponIcon != g.lastWeaponIcon || s.weaponName != g.lastWeaponName) {
        g.weaponChangeT = 0.f;
        g.lastWeaponIcon = s.weaponIcon;
        g.lastWeaponName = s.weaponName;
    }
    g.weaponChangeT += dt;
    g.weaponAlpha = fadeTo(g.weaponAlpha, s.showWeapon, dt, 7.f, 3.f);
    if (g.weaponAlpha > 0.01f) {
        float a = g.weaponAlpha;
        float iw = 158.f * sc, ih = iw * 0.375f;
        float slide = (1.f - easeOutCubic(Min(1.f, g.weaponChangeT / 0.3f))) * 24.f * sc;
        float cx = xr - iw * 0.5f + slide, cy = y + ih * 0.5f + 2.f * sc;
        drawWeapon(s.weaponIcon, cx + 2.f * sc, cy + 2.f * sc, iw, C(0.f, 0.f, 0.f, 0.45f * a), 0.f, 0, 0.f);
        drawWeapon(s.weaponIcon, cx, cy, iw, withAlpha(kWhite, a), 1.4f * sc, C(0.02f, 0.02f, 0.06f, 0.85f * a));
        bool hasAmmo = s.weaponIcon >= 3 && (s.ammoClip > 0 || s.ammoTotal > 0 || s.reloading);
        float tx = xr - iw - 14.f * sc;
        if (hasAmmo) {
            TextStyle big = style(FONT_HEADING, 36.f * sc, withAlpha(s.ammoClip == 0 ? kRed : kWhite, a), ALIGN_RIGHT);
            big.outline = 1.8f * sc;
            big.outlineColor = C(0.f, 0.f, 0.04f, 0.85f * a);
            big.shadow = 1.5f * sc;
            TextStyle small = big;
            small.size = 24.f * sc;
            small.color = withAlpha(kTextDim, a);
            if (s.reloading) {
                float pulse = calm() ? 1.f : 0.55f + 0.45f * sinf(t * 10.f);
                TextStyle rs = small;
                rs.color = withAlpha(kYellow, a * pulse);
                text(tx, cy - rs.size * 0.55f, "RELOADING", rs);
            } else {
                std::string tot = StrFormat("%d", s.ammoTotal);
                float w = text(tx, cy - small.size * 0.35f, tot.c_str(), small);
                std::string clip = StrFormat("%d", s.ammoClip);
                text(tx - w - 10.f * sc, cy - big.size * 0.56f, clip.c_str(), big);
            }
        }
        // weapon name for a moment after switching
        float nameA = a * (1.f - SmoothStep(2.2f, 2.8f, g.weaponChangeT));
        if (nameA > 0.01f && !s.weaponName.empty()) {
            TextStyle ns = style(FONT_HEADING, 20.f * sc, withAlpha(kText, nameA), ALIGN_RIGHT);
            ns.shadow = 1.5f * sc;
            ns.outline = 1.4f * sc;
            ns.outlineColor = C(0.f, 0.f, 0.04f, 0.8f * nameA);
            ns.tracking = 0.06f;
            std::string n = upper(s.weaponName);
            text(xr, cy + ih * 0.5f + 4.f * sc, n.c_str(), ns);
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Top left: help box + notification card
void drawTopLeft(const HudState& s, const Layout& L, float dt, float t) {
    float sc = L.s;
    float x = L.left, y = L.top;
    RichOpts ro;
    ro.pad = s.padPrompts;
    // help
    if (!s.helpText.empty() && s.helpText != g.help) {
        g.help = s.helpText;
        g.helpT = 0.f;
    }
    g.helpT += dt;
    g.helpAlpha = fadeTo(g.helpAlpha, !s.helpText.empty(), dt, 7.f, 5.f);
    if (g.helpAlpha > 0.01f && !g.help.empty()) {
        float a = g.helpAlpha;
        TextStyle st = style(FONT_BODY, 23.f * sc, kText);
        float pad = 18.f * sc, maxW = 430.f * sc;
        vec2 sz = richMeasure(g.help.c_str(), st, maxW, ro);
        float w = sz.x + pad * 2.f + 6.f * sc, h = sz.y + pad * 1.6f;
        float drop = (1.f - easeOutCubic(Min(1.f, g.helpT / 0.3f))) * -14.f * sc;
        float yy = y + drop;
        roundRect(x + 2.f * sc, yy + 3.f * sc, w, h, 10.f * sc, C(0.f, 0.f, 0.f, 0.28f * a));
        roundRectGradient(x, yy, w, h, 10.f * sc, C(0.05f, 0.07f, 0.17f, 0.90f * a), C(0.03f, 0.04f, 0.11f, 0.90f * a), 1.f * sc,
                          withAlpha(kWhite, 0.08f * a));
        roundRect(x, yy, 5.f * sc, h, 2.5f * sc, withAlpha(kPink, a));
        ro.alpha = a;
        st.color = withAlpha(kText, a);
        richDraw(x + pad + 6.f * sc, yy + pad * 0.8f, g.help.c_str(), st, maxW, ro);
        y += (h + 14.f * sc) * easeOutCubic(a);
    }
    // notification
    if (!s.notification.empty() && (s.notification != g.note || s.notificationTitle != g.noteTitle)) {
        g.note = s.notification;
        g.noteTitle = s.notificationTitle;
        g.noteT = 0.f;
    }
    g.noteT += dt;
    g.noteAlpha = fadeTo(g.noteAlpha, !s.notification.empty(), dt, 6.f, 4.f);
    if (g.noteAlpha > 0.01f && !g.note.empty()) {
        float a = g.noteAlpha;
        float w = 420.f * sc, pad = 16.f * sc;
        TextStyle body = style(FONT_BODY, 21.f * sc, withAlpha(kText, a));
        ro.alpha = a;
        vec2 bs = richMeasure(g.note.c_str(), body, w - pad * 2.f - 58.f * sc, ro);
        float h = Max(86.f * sc, bs.y + 58.f * sc);
        float slide = (1.f - easeOutBack(Min(1.f, g.noteT / 0.45f), 1.2f)) * -(w + 60.f * sc);
        float xx = x + slide;
        roundRect(xx + 2.f * sc, y + 4.f * sc, w, h, 14.f * sc, C(0.f, 0.f, 0.f, 0.30f * a));
        roundRectGradient(xx, y, w, h, 14.f * sc, C(0.07f, 0.09f, 0.20f, 0.93f * a), C(0.04f, 0.05f, 0.13f, 0.93f * a), 1.f * sc,
                          withAlpha(kWhite, 0.10f * a));
        // app icon
        float isz = 42.f * sc;
        float ix = xx + pad, iy = y + pad;
        roundRectGradient(ix, iy, isz, isz, 11.f * sc, withAlpha(kPink, a), withAlpha(C(0.62f, 0.18f, 0.95f), a));
        drawIcon(ICO_MESSAGE, ix + isz * 0.5f, iy + isz * 0.52f, isz * 0.8f, withAlpha(kWhite, a));
        TextStyle title = style(FONT_HEADING, 22.f * sc, withAlpha(kWhite, a));
        std::string tt = g.noteTitle.empty() ? std::string("NOTIFICATION") : upper(g.noteTitle);
        text(ix + isz + 14.f * sc, iy - 1.f * sc, tt.c_str(), title);
        TextStyle when = style(FONT_BODY, 16.f * sc, withAlpha(kTextMute, a), ALIGN_RIGHT);
        text(xx + w - pad, iy + 2.f * sc, "now", when);
        richDraw(ix + isz + 14.f * sc, iy + 26.f * sc, g.note.c_str(), body, w - pad * 2.f - 58.f * sc, ro);
    }
    (void)t;
}

// ------------------------------------------------------------------------------------------------------------------
// Top center: radio station banner
u32 stationColor(const std::string& name) {
    u32 h = hashString(name.c_str());
    float hue = hashToFloat(h);
    vec3 c = hsvToRgb(hue, 0.72f, 1.f);
    return C(c.x, c.y, c.z);
}

void drawRadio(const HudState& s, const Layout& L, float dt) {
    float sc = L.s;
    bool on = s.radioTimer > 0.f;
    if (on && (s.radioStation != g.radio || s.radioTrack != g.track || g.radioAlpha <= 0.f)) {
        if (s.radioStation != g.radio) g.radioT = 0.f;
        g.radio = s.radioStation;
        g.track = s.radioTrack;
    }
    g.radioT += dt;
    g.radioAlpha = fadeTo(g.radioAlpha, on, dt, 6.f, 3.f);
    if (g.radioAlpha <= 0.01f) return;
    float a = g.radioAlpha;
    float cx = L.W * 0.5f, y = L.top - 6.f * sc + (1.f - easeOutCubic(a)) * -20.f * sc;
    bool off = g.radio.empty() || upper(g.radio) == "RADIO OFF";
    std::string name = off ? std::string("RADIO OFF") : upper(g.radio);
    TextStyle ns = style(FONT_HEADING, 36.f * sc, withAlpha(kWhite, a), ALIGN_LEFT);
    ns.outline = 1.8f * sc;
    ns.outlineColor = C(0.f, 0.f, 0.04f, 0.8f * a);
    ns.shadow = 2.f * sc;
    ns.shadowSoft = 0.5f;
    ns.tracking = 0.03f;
    TextStyle ts = style(FONT_BODY, 20.f * sc, withAlpha(kTextDim, a), ALIGN_LEFT);
    ts.shadow = 1.5f * sc;
    float nw = textWidth(name.c_str(), ns), tw = off ? 0.f : textWidth(g.track.c_str(), ts);
    float er = 30.f * sc;
    float total = er * 2.f + 16.f * sc + Max(nw, tw);
    float x0 = cx - total * 0.5f;
    vec2 ec(x0 + er, y + er);
    u32 col = off ? C(0.4f, 0.42f, 0.5f) : stationColor(g.radio);
    circle(ec.x + 2 * sc, ec.y + 3 * sc, er, C(0.f, 0.f, 0.f, 0.35f * a));
    circle(ec.x, ec.y, er, withAlpha(C(0.05f, 0.06f, 0.14f), 0.95f * a));
    circle(ec.x, ec.y, er - 1.f, withAlpha(col, a), 3.f * sc);
    // rotating equalizer ticks
    if (!off) {
        for (int k = 0; k < 5; k++) {
            float hh = (0.35f + 0.65f * fabsf(sinf(g.radioT * (3.f + k * 1.3f) + k))) * er * 0.7f;
            float bx = ec.x - er * 0.44f + k * er * 0.22f;
            roundRect(bx - er * 0.07f, ec.y + er * 0.35f - hh, er * 0.14f, hh, er * 0.05f, withAlpha(col, a));
        }
    } else {
        drawIcon(ICO_MUSIC, ec.x, ec.y, er * 1.3f, withAlpha(kTextDim, a));
    }
    float tx = x0 + er * 2.f + 16.f * sc;
    text(tx, off ? y + er - ns.size * 0.55f : y + 2.f * sc, name.c_str(), ns);
    if (!off && !g.track.empty()) text(tx, y + 38.f * sc, g.track.c_str(), ts);
}

// ------------------------------------------------------------------------------------------------------------------
// Bottom right: speedometer, location, vehicle name, mission timer and counter
void drawBottomRight(const HudState& s, const Layout& L, float dt, float t) {
    float sc = L.s;
    float xr = L.right;
    float y = L.bottom;
    // speedometer
    g.speedAlpha = fadeTo(g.speedAlpha, s.inVehicle && s.showSpeedometer && !s.aircraft, dt, 5.f, 4.f);
    if (g.speedAlpha > 0.01f) {
        float a = g.speedAlpha;
        float unit = s.metricUnits ? 1.f : 0.621371f;
        g.speedShown = approachExp(g.speedShown, Max(0.f, s.speedKmh * unit), 10.f, dt);
        float r = 62.f * sc;
        vec2 c(xr - r, y - r * 0.92f);
        float span = kPi * 0.72f;   // half sweep
        float maxV = s.metricUnits ? 260.f : 160.f;
        float f = Saturate(g.speedShown / maxV);
        circleSoft(c.x, c.y + 2.f * sc, r + 6.f * sc, 14.f * sc, C(0.f, 0.f, 0.03f, 0.40f * a));
        circle(c.x, c.y, r + 5.f * sc, C(0.03f, 0.04f, 0.10f, 0.55f * a));
        arc(c.x, c.y, r, 7.f * sc, 0.f, span, C(1.f, 1.f, 1.f, 0.12f * a));
        if (f > 0.002f) {
            float half = span * f;
            float mid = -span + half;
            u32 col = lerpColor(kCyan, kPink, SmoothStep(0.45f, 0.95f, f));
            setAdditive(true);
            arc(c.x, c.y, r, 14.f * sc, mid, half, withAlpha(col, 0.18f * a));
            setAdditive(false);
            arc(c.x, c.y, r, 6.f * sc, mid, half, withAlpha(col, a));
        }
        for (int k = 0; k <= 8; k++) {
            float ang = -span + 2.f * span * k / 8.f;
            vec2 d(sinf(ang), -cosf(ang));
            vec2 p0 = c + d * (r - 10.f * sc), p1 = c + d * (r - (k % 2 ? 14.f : 18.f) * sc);
            capsule(p0.x, p0.y, p1.x, p1.y, 1.6f * sc, withAlpha(kWhite, 0.5f * a));
        }
        TextStyle st = style(FONT_HEADING, 54.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
        st.outline = 1.5f * sc;
        st.outlineColor = C(0.f, 0.f, 0.04f, 0.7f * a);
        st.shadow = 2.f * sc;
        std::string v = StrFormat("%d", (int)(g.speedShown + 0.5f));
        text(c.x, c.y - st.size * 0.62f, v.c_str(), st);
        TextStyle us = style(FONT_HEADING, 16.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
        us.tracking = 0.12f;
        text(c.x, c.y + 20.f * sc, s.metricUnits ? "KM/H" : "MPH", us);
        y -= (r * 1.95f + 8.f * sc) * easeOutCubic(a);
    }
    // location (street under district)
    if (s.locationTimer > 0.f && (s.zoneName != g.zone || s.streetName != g.street || g.locAlpha <= 0.f)) {
        g.zone = s.zoneName;
        g.street = s.streetName;
    }
    g.locAlpha = s.locationTimer > 0.f ? Min(1.f, Min(g.locAlpha + dt * 4.f, s.locationTimer / 0.6f)) : Max(0.f, g.locAlpha - dt * 3.f);
    if (g.locAlpha > 0.01f && !g.zone.empty()) {
        float a = g.locAlpha;
        float slide = (1.f - easeOutCubic(a)) * 24.f * sc;
        TextStyle ss = style(FONT_BODY, 23.f * sc, withAlpha(kTextDim, a), ALIGN_RIGHT);
        ss.shadow = 1.6f * sc;
        ss.shadowSoft = 0.5f;
        ss.outline = 1.2f * sc;
        ss.outlineColor = C(0.f, 0.f, 0.04f, 0.6f * a);
        if (!g.street.empty()) {
            y -= 28.f * sc;
            text(xr + slide, y, g.street.c_str(), ss);
        }
        TextStyle zs = style(FONT_HEADING, 40.f * sc, withAlpha(kWhite, a), ALIGN_RIGHT);
        zs.outline = 2.f * sc;
        zs.outlineColor = C(0.f, 0.f, 0.04f, 0.75f * a);
        zs.shadow = 2.f * sc;
        zs.shadowSoft = 0.6f;
        zs.skew = 0.12f;
        y -= 44.f * sc;
        text(xr + slide, y, g.zone.c_str(), zs);
        // accent underline
        float zw = textWidth(g.zone.c_str(), zs);
        gradientRectH(xr - zw * easeOutCubic(a), y + 44.f * sc, zw * easeOutCubic(a), 2.f * sc, withAlpha(kPink, 0.f), withAlpha(kPink, 0.9f * a));
        y -= 8.f * sc;
    }
    // vehicle name
    if (s.vehicleNameTimer > 0.f && !s.vehicleName.empty()) g.vehicle = s.vehicleName;
    g.vehAlpha = s.vehicleNameTimer > 0.f && !s.vehicleName.empty() ? Min(1.f, Min(g.vehAlpha + dt * 4.f, s.vehicleNameTimer / 0.6f))
                                                                     : Max(0.f, g.vehAlpha - dt * 3.f);
    if (g.vehAlpha > 0.01f && !g.vehicle.empty()) {
        float a = g.vehAlpha;
        float slide = (1.f - easeOutCubic(a)) * 30.f * sc;
        TextStyle vs = style(FONT_TITLE, 44.f * sc, withAlpha(kWhite, a), ALIGN_RIGHT);
        vs.skew = 0.18f;
        vs.outline = 2.2f * sc;
        vs.outlineColor = C(0.f, 0.f, 0.04f, 0.8f * a);
        vs.shadow = 2.5f * sc;
        vs.shadowSoft = 0.6f;
        vs.colorBottom = withAlpha(C(0.80f, 0.86f, 1.f), a);
        y -= 52.f * sc;
        text(xr + slide, y, g.vehicle.c_str(), vs);
        y -= 10.f * sc;
    }
    // mission counter / timer (GTA-style black bars)
    auto bar = [&](const char* label, const std::string& value, u32 valCol, float a) {
        float w = 330.f * sc, h = 42.f * sc;
        y -= h + 6.f * sc;
        gradientRectH(xr - w, y, w, h, C(0.f, 0.f, 0.02f, 0.f), C(0.f, 0.f, 0.02f, 0.72f * a));
        TextStyle ls = style(FONT_HEADING, 22.f * sc, withAlpha(kTextDim, a), ALIGN_RIGHT);
        ls.tracking = 0.08f;
        TextStyle vs = style(FONT_HEADING, 32.f * sc, withAlpha(valCol, a), ALIGN_RIGHT);
        vs.shadow = 1.5f * sc;
        float vw = text(xr - 12.f * sc, y + h * 0.5f - vs.size * 0.56f, value.c_str(), vs);
        text(xr - 26.f * sc - vw, y + h * 0.5f - ls.size * 0.52f, label, ls);
    };
    g.counterAlpha = fadeTo(g.counterAlpha, !s.missionCounterLabel.empty(), dt, 6.f, 3.f);
    if (!s.missionCounterLabel.empty()) {
        g.counterLabel = s.missionCounterLabel;
        g.counterVal = s.missionCounter;
        g.counterMax = s.missionCounterMax;
    }
    if (g.counterAlpha > 0.01f) {
        std::string val = g.counterMax > 0 ? StrFormat("%d/%d", g.counterVal, g.counterMax) : StrFormat("%d", g.counterVal);
        std::string lab = upper(g.counterLabel);
        bar(lab.c_str(), val, kWhite, g.counterAlpha);
    }
    g.timerAlpha = fadeTo(g.timerAlpha, s.missionTimer >= 0.f, dt, 6.f, 3.f);
    if (s.missionTimer >= 0.f) g.timerVal = s.missionTimer;
    if (g.timerAlpha > 0.01f) {
        bool urgent = g.timerVal < 10.f;
        u32 col = urgent ? lerpColor(kRed, kWhite, 0.3f * pulseWave(t, 12.f)) : kWhite;
        bar("TIME", fmtTime(g.timerVal), col, g.timerAlpha);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Bottom center: objective + subtitles
void drawBottomCenter(const HudState& s, const Layout& L, float dt) {
    float sc = L.s;
    float cx = L.W * 0.5f;
    float maxW = (L.x1 - L.x0) * 0.5f;
    float y = L.bottom - 8.f * sc;
    RichOpts ro;
    ro.pad = s.padPrompts;
    // subtitle
    bool subOn = !s.subtitle.text.empty();
    if (subOn) g.sub = s.subtitle;
    g.subAlpha = fadeTo(g.subAlpha, subOn, dt, 10.f, 6.f);
    if (g.subAlpha > 0.01f && !g.sub.text.empty()) {
        y = drawSubtitleBlock(cx, y, maxW, sc, g.sub.speaker, g.sub.speakerColor, g.sub.text, s.padPrompts, g.subAlpha);
        y -= (uiOptions().subtitleBackground > 0.01f ? 22.f : 12.f) * sc;
    } else if (g.subAlpha <= 0.01f) {
        g.sub = Subtitle();
    }
    // objective
    if (!s.objective.empty() && s.objective != g.objective) {
        g.objective = s.objective;
        g.objT = 0.f;
    }
    g.objT += dt;
    g.objAlpha = fadeTo(g.objAlpha, !s.objective.empty(), dt, 5.f, 4.f);
    if (g.objAlpha > 0.01f && !g.objective.empty()) {
        float a = g.objAlpha;
        TextStyle st = style(FONT_BODY, 28.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
        st.outline = 2.f * sc;
        st.outlineColor = C(0.f, 0.f, 0.03f, 0.9f * a);
        st.shadow = 2.f * sc;
        st.shadowSoft = 0.7f;
        ro.alpha = a;
        vec2 sz = richMeasure(g.objective.c_str(), st, maxW, ro);
        float rise = (1.f - easeOutCubic(Min(1.f, g.objT / 0.4f))) * 12.f * sc;
        y -= sz.y + (g.subAlpha > 0.01f ? 6.f * sc : 0.f);
        richDraw(cx, y + rise, g.objective.c_str(), st, maxW, ro);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Lock-on marker: a chevron above the target's head (red when hostile, white when neutral) with a thin health bar that
// appears once the target is hurt. It drops in on acquire, pulses on hits and fades out on release.
void drawLockOn(const HudState& s, const Layout& L, float dt, float t) {
    float sc = L.s;
    if (s.lockOn) {
        bool newTarget = g.lockA < 0.05f || length(s.lockScreen - g.lockPos) > 90.f * sc;
        if (newTarget) {
            g.lockAcquire = 0.f;
            g.lockHealthShown = g.lockHealthLag = Saturate(s.lockHealth);
            g.lockBarA = s.lockHealth < 0.995f ? 1.f : 0.f;
        }
        float hp = Saturate(s.lockHealth);
        if (hp < g.lockHealthShown - 0.005f) g.lockPulse = 1.f;   // took a hit
        g.lockHealthShown = hp;
        g.lockPos = s.lockScreen;
        g.lockHostile = s.lockHostile;
        g.lockMelee = s.lockMelee;
    }
    g.lockA = s.lockOn ? Min(1.f, g.lockA + dt * 10.f) : Max(0.f, g.lockA - dt * 7.f);
    g.lockAcquire += dt;
    g.lockPulse = Max(0.f, g.lockPulse - dt * 4.f);
    // the lag bar trails the health after a short hold, like the player's bar
    if (g.lockHealthLag > g.lockHealthShown) g.lockHealthLag = Max(g.lockHealthShown, g.lockHealthLag - dt * (g.lockPulse > 0.5f ? 0.f : 0.9f));
    else g.lockHealthLag = g.lockHealthShown;
    g.lockBarA = approachExp(g.lockBarA, (g.lockHealthShown < 0.995f || g.lockMelee) ? 1.f : 0.f, 10.f, dt);
    if (g.lockA <= 0.01f) return;
    float a = easeOutCubic(g.lockA);
    float acq = easeOutBack(Saturate(g.lockAcquire / 0.22f), 2.2f);
    float pulse = g.lockPulse * g.lockPulse;
    u32 base = g.lockHostile ? C(1.f, 0.22f, 0.24f) : C(0.96f, 0.97f, 1.f);
    u32 col = lerpColor(base, kWhite, pulse * 0.8f);
    // position above the head, kept on screen
    float size = (g.lockMelee ? 17.f : 14.f) * sc * (1.f + 0.6f * (1.f - acq) + 0.28f * pulse);
    float lift = (26.f + 14.f * (1.f - acq)) * sc + (g.lockMelee ? 2.f * sinf(t * 5.f) * sc : 0.f);
    float cx = Clamp(g.lockPos.x, L.left + 40.f * sc, L.right - 40.f * sc);
    float cy = Clamp(g.lockPos.y - lift, L.top + 30.f * sc, L.bottom - 30.f * sc);
    // chevron: dark underlay, colored fill, bright top edge
    vec2 p0(cx - size, cy - size * 0.72f), p1(cx + size, cy - size * 0.72f), p2(cx, cy + size * 0.62f);
    float o = 2.2f * sc;
    triangle(vec2(p0.x - o * 1.2f, p0.y - o), vec2(p1.x + o * 1.2f, p1.y - o), vec2(p2.x, p2.y + o * 1.6f), C(0.02f, 0.02f, 0.05f, 0.75f * a));
    if (g.lockHostile) circleSoft(cx, cy - size * 0.1f, size * 1.1f, size * 1.6f, withAlpha(C(1.f, 0.1f, 0.15f), (0.30f + 0.4f * pulse) * a));
    triangle(p0, p1, p2, withAlpha(col, a));
    capsule(p0.x + size * 0.18f, p0.y + 1.2f * sc, p1.x - size * 0.18f, p1.y + 1.2f * sc, 1.6f * sc, withAlpha(kWhite, (0.55f + 0.45f * pulse) * a));
    if (g.lockMelee) {
        // fighting stance: brackets either side of the chevron
        float bx = size * 1.55f, by = size * 0.55f;
        for (float side : {-1.f, 1.f}) {
            vec2 q0(cx + side * bx, cy - by - size * 0.2f), q1(cx + side * (bx + 5.f * sc), cy - size * 0.2f), q2(cx + side * bx, cy + by - size * 0.2f);
            capsule(q0.x, q0.y, q1.x, q1.y, 2.4f * sc, withAlpha(col, 0.85f * a));
            capsule(q1.x, q1.y, q2.x, q2.y, 2.4f * sc, withAlpha(col, 0.85f * a));
        }
    }
    // health bar above the chevron
    float ba = g.lockBarA * a;
    if (ba > 0.01f) {
        float bw = (g.lockMelee ? 58.f : 44.f) * sc, bh = 4.f * sc;
        float bx = cx - bw * 0.5f, by = cy - size * 0.72f - 9.f * sc - bh;
        roundRect(bx - 1.5f * sc, by - 1.5f * sc, bw + 3.f * sc, bh + 3.f * sc, 2.5f * sc, C(0.02f, 0.02f, 0.05f, 0.8f * ba));
        roundRect(bx, by, bw * g.lockHealthLag, bh, 1.5f * sc, withAlpha(C(1.f, 0.85f, 0.85f), 0.85f * ba));
        u32 hc = g.lockHealthShown > 0.35f ? (g.lockHostile ? C(1.f, 0.30f, 0.30f) : C(0.40f, 0.95f, 0.55f)) : C(1.f, 0.20f, 0.18f);
        roundRect(bx, by, Max(bw * g.lockHealthShown, 0.f), bh, 1.5f * sc, withAlpha(lerpColor(hc, kWhite, pulse * 0.6f), ba));
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Center: reticle, hit markers, damage indicators, low-health vignette
void drawCenter(const HudState& s, const Layout& L, float dt, float t) {
    float sc = L.s;
    vec2 c(L.W * 0.5f, L.H * 0.5f);
    // low health vignette
    g.lowHealth = approachExp(g.lowHealth, (!s.dead && s.health < 0.3f) ? 1.f - s.health / 0.3f : 0.f, 3.f, dt);
    if (s.health < g.lastHealth - 0.01f) g.hurtFlash = Min(1.f, g.hurtFlash + (g.lastHealth - s.health) * 4.f + 0.3f);
    g.lastHealth = s.health;
    g.hurtFlash = Max(0.f, g.hurtFlash - dt * 2.5f);
    float vig = g.lowHealth * (0.55f + 0.45f * (calm() ? 0.f : sinf(t * 5.f))) * 0.5f + g.hurtFlash * (calm() ? 0.15f : 0.35f);
    if (vig > 0.01f && !s.rendererScreenFx) {
        float e = L.H * 0.22f;
        u32 r0 = C(0.55f, 0.02f, 0.05f, Saturate(vig)), r1 = C(0.55f, 0.02f, 0.05f, 0.f);
        gradientRect(0, 0, L.W, e, r0, r1);
        gradientRect(0, L.H - e, L.W, e, r1, r0);
        gradientRectH(0, 0, e, L.H, r0, r1);
        gradientRectH(L.W - e, 0, e, L.H, r1, r0);
    }
    // Focus ability: soft magenta edge glow with slow breathing while active
    g.focusFx = approachExp(g.focusFx, s.specialActive ? 1.f : 0.f, 6.f, dt);
    if (g.focusFx > 0.01f) {
        float k = g.focusFx * (0.75f + 0.25f * sinf(t * 2.2f));
        float e = L.H * 0.16f;
        // the renderer's Focus grading already darkens the edges: keep only a faint tint of the signature color
        u32 f0 = withAlpha(kFocus, (s.rendererScreenFx ? 0.08f : 0.22f) * k), f1 = withAlpha(kFocus, 0.f);
        setAdditive(true);
        gradientRect(0, 0, L.W, e, f0, f1);
        gradientRect(0, L.H - e, L.W, e, f1, f0);
        gradientRectH(0, 0, e * 1.2f, L.H, f0, f1);
        gradientRectH(L.W - e * 1.2f, 0, e * 1.2f, L.H, f1, f0);
        setAdditive(false);
        // thin letterbox-like lines hinting at the slowed time
        rect(0, 0, L.W, 2.f * sc, withAlpha(kFocus, 0.5f * k));
        rect(0, L.H - 2.f * sc, L.W, 2.f * sc, withAlpha(kFocus, 0.5f * k));
    }
    // damage direction indicators (angles: 0 = ahead, counter-clockwise positive)
    for (auto& d : g.dmg) d.seen = false;
    for (float ang : s.damageDirections) {
        DamageInd* best = nullptr;
        float bd = 0.35f;
        for (auto& d : g.dmg) {
            float diff = fabsf(wrapAngle(d.ang - ang));
            if (!d.seen && d.fade > 0.f && diff < bd) { bd = diff; best = &d; }
        }
        if (best) { best->seen = true; best->ang = ang; best->fade = 1.f; }
        else g.dmg.push_back({ang, 0.f, 1.f, true});
    }
    for (size_t i = 0; i < g.dmg.size();) {
        DamageInd& d = g.dmg[i];
        d.age += dt;
        if (!d.seen) d.fade -= dt * 2.5f;
        if (d.fade <= 0.f || d.age > 4.f) g.dmg.erase(g.dmg.begin() + i);
        else i++;
    }
    for (auto& d : g.dmg) {
        float intensity = Saturate(d.fade) * (1.f - SmoothStep(0.6f, 2.0f, d.age)) + 0.25f * (1.f - Saturate(d.age / 0.2f));
        if (intensity <= 0.01f) continue;
        float sa = -d.ang;
        float R = 170.f * sc;
        float pop = 1.f + 0.15f * (1.f - Saturate(d.age / 0.25f));
        for (int k = 0; k < 3; k++) {
            float th = (14.f - k * 4.f) * sc * pop;
            float half = 0.30f - k * 0.07f;
            arc(c.x, c.y, R + k * 2.f * sc, th, sa, half, C(1.f, 0.10f, 0.14f, (0.22f + 0.28f * k) * Saturate(intensity)));
        }
        vec2 dir(sinf(sa), -cosf(sa));
        vec2 tip = c + dir * (R + 18.f * sc), bl = c + dir * (R + 6.f * sc) + perp(dir) * 9.f * sc, br = c + dir * (R + 6.f * sc) - perp(dir) * 9.f * sc;
        triangle(tip, bl, br, C(1.f, 0.25f, 0.25f, 0.85f * Saturate(intensity)));
    }
    // reticle
    g.reticleAlpha = fadeTo(g.reticleAlpha, s.aiming && !s.weaponWheelOpen && !s.dead, dt, 14.f, 10.f);
    g.spreadShown = approachExp(g.spreadShown, Max(0.f, s.reticleSpread), 18.f, dt);
    if (s.hitMarker > g.lastHit + 0.05f) g.hitPop = 1.f;
    g.lastHit = s.hitMarker;
    g.hitPop = Max(0.f, g.hitPop - dt * 6.f);
    g.hitIntensity = Max(s.hitMarker, g.hitIntensity - dt * 3.f);
    if (s.killMarker) g.killT = 0.f;
    g.killT += dt;
    float sightA = Max(Saturate(s.scopeView), Saturate(s.redDot));
    if (s.scopeView > 0.01f) drawScopeView(L, c, s.scopeKind, Saturate(s.scopeView), s.reticleOnEnemy);
    if (s.redDot > 0.01f) {
        float a = Saturate(s.redDot);
        circleSoft(c.x, c.y, 7.f * sc, 8.f * sc, C(1.f, 0.08f, 0.05f, 0.22f * a));   // glow on the glass
        circleSoft(c.x, c.y, 2.2f * sc, 1.2f * sc, C(1.f, 0.16f, 0.1f, 0.95f * a));
    }
    if (g.reticleAlpha > 0.01f && sightA < 0.99f)
        drawReticleShape(c, sc, g.spreadShown, s.reticleOnEnemy ? 1 : s.reticleOnFriendly ? 2 : 0, g.reticleAlpha * (1.f - sightA));
    float hitA = Saturate(g.hitIntensity);
    float killA = 1.f - Saturate(g.killT / 0.6f);
    if (hitA > 0.01f || killA > 0.01f) {
        bool kill = killA > 0.01f;
        float a = kill ? killA : hitA;
        float pop = 1.f + 0.35f * (kill ? (1.f - Saturate(g.killT / 0.15f)) : g.hitPop);
        float r0 = (kill ? 12.f : 10.f) * sc * pop, r1 = (kill ? 26.f : 20.f) * sc * pop;
        u32 col = kill ? kRed : kWhite;
        for (int k = 0; k < 4; k++) {
            float ang = kPi * 0.25f + k * kHalfPi;
            vec2 d(cosf(ang), sinf(ang));
            vec2 p0 = c + d * r0, p1 = c + d * r1;
            capsule(p0.x, p0.y, p1.x, p1.y, (kill ? 5.f : 4.f) * sc, C(0.f, 0.f, 0.f, 0.5f * a));
            capsule(p0.x, p0.y, p1.x, p1.y, (kill ? 3.2f : 2.4f) * sc, withAlpha(col, a));
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Big message: MISSION PASSED / WASTED / BUSTED
void drawBigMessage(const HudState& s, const Layout& L, float dt) {
    float sc = L.s;
    bool on = !s.bigMessage.empty();
    if (on) {
        g.big = s.bigMessage;
        g.bigSub = s.bigMessageSub;
        g.bigColor = s.bigMessageColor;
        g.bigTime = s.bigMessageTime;
    }
    g.bigAlpha = on ? Min(1.f, g.bigAlpha + dt * 10.f) : Max(0.f, g.bigAlpha - dt * 2.5f);
    if (g.bigAlpha <= 0.01f || g.big.empty()) return;
    float a = g.bigAlpha;
    float tt = g.bigTime;
    std::string up = upper(g.big);
    bool death = up == "WASTED", busted = up == "BUSTED";
    float cy = L.H * 0.42f;
    if (death || busted) {
        float k = Saturate(tt / 1.2f) * a;
        if (!s.rendererScreenFx)
            backdrop(0, 0, L.W, L.H, 0.f, C(0.85f, 0.85f, 0.85f, k), death ? C(0.10f, 0.0f, 0.02f, 0.35f) : C(0.0f, 0.03f, 0.12f, 0.35f), 0.08f);
        gradientRect(0, 0, L.W, L.H * 0.5f, C(0.f, 0.f, 0.f, 0.35f * k), C(0.f, 0.f, 0.f, 0.f));
        gradientRect(0, L.H * 0.5f, L.W, L.H * 0.5f, C(0.f, 0.f, 0.f, 0.f), C(0.f, 0.f, 0.f, 0.45f * k));
        float ta = Saturate((tt - 0.35f) / 0.5f) * a;
        float scale = 1.12f - 0.12f * easeOutCubic((tt - 0.35f) / 0.8f) + 0.02f * Max(0.f, tt - 1.2f);
        TextStyle st = style(FONT_TITLE, 150.f * sc * scale, withAlpha(death ? C(0.86f, 0.10f, 0.12f) : C(0.25f, 0.55f, 1.f), ta), ALIGN_CENTER);
        st.outline = 3.f * sc;
        st.outlineColor = C(0.f, 0.f, 0.f, 0.85f * ta);
        st.shadow = 5.f * sc;
        st.shadowSoft = 1.f;
        st.tracking = 0.02f;
        text(L.W * 0.5f, cy - st.size * 0.55f, up.c_str(), st);
        if (!g.bigSub.empty()) {
            TextStyle ss = style(FONT_HEADING, 32.f * sc, withAlpha(kWhite, Saturate((tt - 1.0f) / 0.5f) * a), ALIGN_CENTER);
            ss.shadow = 2.f * sc;
            text(L.W * 0.5f, cy + st.size * 0.52f, g.bigSub.c_str(), ss);
        }
        return;
    }
    // band
    float bandT = easeOutCubic(tt / 0.35f);
    float bh = 190.f * sc * bandT;
    float by = cy - bh * 0.5f;
    u32 dark = C(0.f, 0.f, 0.03f, 0.62f * a);
    gradientRectH(0, by, L.W * 0.5f, bh, C(0.f, 0.f, 0.03f, 0.f), dark);
    gradientRectH(L.W * 0.5f, by, L.W * 0.5f, bh, dark, C(0.f, 0.f, 0.03f, 0.f));
    float lineW = L.W * 0.36f * easeOutCubic((tt - 0.1f) / 0.5f);
    u32 accent = g.bigColor | 0xff000000u;
    gradientRectH(L.W * 0.5f - lineW, by, lineW, 2.f * sc, withAlpha(accent, 0.f), withAlpha(accent, 0.9f * a));
    gradientRectH(L.W * 0.5f, by, lineW, 2.f * sc, withAlpha(accent, 0.9f * a), withAlpha(accent, 0.f));
    gradientRectH(L.W * 0.5f - lineW, by + bh - 2.f * sc, lineW, 2.f * sc, withAlpha(accent, 0.f), withAlpha(accent, 0.9f * a));
    gradientRectH(L.W * 0.5f, by + bh - 2.f * sc, lineW, 2.f * sc, withAlpha(accent, 0.9f * a), withAlpha(accent, 0.f));
    // title
    float ta = Saturate(tt / 0.25f) * a;
    float scale = 1.f + 0.3f * (1.f - easeOutBack(tt / 0.45f, 1.4f));
    TextStyle st = style(FONT_TITLE, 96.f * sc * scale, withAlpha(accent, ta), ALIGN_CENTER);
    st.colorBottom = withAlpha(lerpColor(accent, C(1.f, 0.55f, 0.1f), 0.45f), ta);
    st.outline = 2.5f * sc;
    st.outlineColor = C(0.f, 0.f, 0.02f, 0.9f * ta);
    st.shadow = 4.f * sc;
    st.shadowSoft = 1.f;
    st.tracking = 0.02f + 0.18f * (1.f - easeOutCubic(tt / 0.6f));
    float titleY = cy - st.size * 0.62f - (g.bigSub.empty() ? 0.f : 14.f * sc);
    float tw = text(L.W * 0.5f, titleY, up.c_str(), st);
    // light sweep (skipped with reduced flashing)
    float sw = (tt - 0.35f) / 0.7f;
    if (sw > 0.f && sw < 1.f && !calm()) {
        ClipState pc = getClip();
        setClipRect(L.W * 0.5f - tw * 0.5f, titleY, tw, st.size * 1.1f);
        setAdditive(true);
        float x = L.W * 0.5f - tw * 0.5f - 120.f * sc + (tw + 240.f * sc) * easeInOutCubic(sw);
        u32 hi = C(1.f, 0.95f, 0.8f, 0.55f * ta);
        quad4(vec2(x - 60.f * sc, titleY + st.size * 1.1f), vec2(x, titleY), vec2(x + 40.f * sc, titleY), vec2(x - 20.f * sc, titleY + st.size * 1.1f),
              withAlpha(hi, 0.f), withAlpha(hi, 0.f), hi, hi);
        setAdditive(false);
        setClip(pc);
    }
    if (!g.bigSub.empty()) {
        float sa = Saturate((tt - 0.45f) / 0.35f) * a;
        TextStyle ss = style(FONT_HEADING, 34.f * sc, withAlpha(kWhite, sa), ALIGN_CENTER);
        ss.shadow = 2.f * sc;
        ss.shadowSoft = 0.6f;
        ss.tracking = 0.06f;
        float rise = (1.f - easeOutCubic((tt - 0.45f) / 0.5f)) * 10.f * sc;
        std::string sub = upper(g.bigSub);
        text(L.W * 0.5f, cy + 30.f * sc + rise, sub.c_str(), ss);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Weapon wheel
void splitSlot(const std::string& s, std::string& name, std::string& ammo) {
    size_t p = s.rfind("  ");
    if (p != std::string::npos) {
        name = s.substr(0, p);
        ammo = s.substr(p + 2);
        while (!ammo.empty() && ammo[0] == ' ') ammo.erase(0, 1);
    } else {
        name = s;
        ammo.clear();
    }
}

void drawWeaponWheel(const HudState& s, const Layout& L, float dt, float t) {
    g.wheelAnim = fadeTo(g.wheelAnim, s.weaponWheelOpen, dt, 9.f, 7.f);
    if (s.weaponWheelOpen) {
        g.wheelSlots = s.wheelSlots;
        g.wheelIcons = s.wheelIcons;
    }
    if (g.wheelAnim <= 0.01f) {
        g.wheelSelShown = -1;
        return;
    }
    float a = g.wheelAnim;
    float e = easeOutCubic(a);
    float sc = L.s * (0.92f + 0.08f * e);
    vec2 c(L.W * 0.5f, L.H * 0.5f);
    if (s.rendererScreenFx) rect(0, 0, L.W, L.H, C(0.02f, 0.03f, 0.09f, 0.30f * a));   // the scene is already blurred
    else backdrop(0, 0, L.W, L.H, 0.f, C(0.75f, 0.75f, 0.8f, a), C(0.02f, 0.03f, 0.09f, 0.45f), 0.12f);
    int sel = s.weaponWheelOpen ? s.wheelSelected : g.wheelSelShown;
    if (sel != g.wheelSelShown) {
        g.wheelSelShown = sel;
        g.wheelSelT = 0.f;
    }
    g.wheelSelT += dt;
    const float rIn = 150.f * sc, rOut = 338.f * sc;
    const float rMid = (rIn + rOut) * 0.5f, thick = rOut - rIn;
    const float half = kPi / 8.f;
    // outer glow ring
    ringSoft(c.x, c.y, rOut + 18.f * sc, 10.f * sc, 30.f * sc, C(0.18f, 0.9f, 1.f, 0.10f * a));
    for (int i = 0; i < 8; i++) {
        float mid = i * kPi / 4.f;
        vec2 dir(sinf(mid), -cosf(mid));
        bool has = i < (int)g.wheelSlots.size() && !g.wheelSlots[i].empty();
        bool isSel = i == sel;
        float pop = isSel ? 12.f * sc * easeOutBack(Min(1.f, g.wheelSelT / 0.25f)) : 0.f;
        vec2 sc2 = c + dir * pop;
        if (isSel) {
            arc(sc2.x, sc2.y, rMid, thick, mid, half, withAlpha(C(0.74f, 0.10f, 0.45f), 0.94f * a), 10.f * sc);
            for (int k = 0; k < 6; k++) {
                float fr = (float)k / 6.f;
                arc(sc2.x, sc2.y, rIn + thick * (0.5f + 0.5f * fr) , thick * (1.f - fr) * 0.5f + 2.f, mid, half,
                    withAlpha(kPink, 0.16f * a), 10.f * sc);
            }
            arc(sc2.x, sc2.y, rOut - 3.f * sc, 5.f * sc, mid, half, withAlpha(C(1.f, 0.78f, 0.92f), a), 10.f * sc);
        } else {
            arc(sc2.x, sc2.y, rMid, thick, mid, half, C(0.04f, 0.06f, 0.14f, 0.82f * a), 10.f * sc);
            arc(sc2.x, sc2.y, rOut - 1.5f * sc, 2.f * sc, mid, half, C(1.f, 1.f, 1.f, 0.14f * a), 10.f * sc);
        }
        vec2 ic = sc2 + dir * rMid;
        if (has) {
            int icon = i < (int)g.wheelIcons.size() ? g.wheelIcons[i] : -1;
            if (icon < 0) icon = 0;
            float w = 172.f * sc * (isSel ? 1.08f : 1.f);
            drawWeapon(icon, ic.x, ic.y - 8.f * sc, w, withAlpha(isSel ? kWhite : C(0.86f, 0.89f, 0.97f), a), 1.2f * sc, C(0.f, 0.f, 0.03f, 0.7f * a));
            std::string name, ammo;
            splitSlot(g.wheelSlots[i], name, ammo);
            if (!ammo.empty()) {
                TextStyle as = style(FONT_HEADING, 21.f * sc, withAlpha(isSel ? kWhite : kTextDim, a), ALIGN_CENTER);
                text(ic.x, ic.y + 30.f * sc, ammo.c_str(), as);
            }
        } else {
            circle(ic.x, ic.y, 4.f * sc, C(1.f, 1.f, 1.f, 0.18f * a));
        }
    }
    // center disc
    circle(c.x, c.y, rIn - 14.f * sc, C(0.03f, 0.04f, 0.10f, 0.88f * a));
    circle(c.x, c.y, rIn - 14.f * sc, withAlpha(kCyan, 0.35f * a), 1.5f * sc);
    if (sel >= 0 && sel < (int)g.wheelSlots.size() && !g.wheelSlots[sel].empty()) {
        std::string name, ammo;
        splitSlot(g.wheelSlots[sel], name, ammo);
        std::string n = upper(name);
        TextStyle ns = style(FONT_HEADING, 30.f * sc, withAlpha(kWhite, a), ALIGN_CENTER);
        float maxW = (rIn - 24.f * sc) * 2.f;
        float nw = textWidth(n.c_str(), ns);
        if (nw > maxW) ns.size *= maxW / nw;
        text(c.x, c.y - 30.f * sc, n.c_str(), ns);
        if (!ammo.empty()) {
            TextStyle as = style(FONT_HEADING, 22.f * sc, withAlpha(kCyan, a), ALIGN_CENTER);
            text(c.x, c.y + 6.f * sc, ammo.c_str(), as);
        }
        TextStyle hs = style(FONT_BODY, 15.f * sc, withAlpha(kTextMute, a), ALIGN_CENTER);
        text(c.x, c.y + 40.f * sc, StrFormat("SLOT %d", sel + 1).c_str(), hs);
    } else {
        TextStyle ns = style(FONT_HEADING, 24.f * sc, withAlpha(kTextDim, a), ALIGN_CENTER);
        text(c.x, c.y - 14.f * sc, "SELECT", ns);
    }
    (void)t;
}

}  // namespace hud_ui

namespace uix {

float drawSubtitleBlock(float cx, float bottom, float maxW, float sc, const std::string& speaker, u32 speakerColor, const std::string& text,
                        bool padPrompts, float a) {
    // accessibility: subtitle size, a backing box and plain or colored speaker names (Settings > Accessibility)
    const UiOptions& uo = uiOptions();
    float k = uo.subtitleScale;
    float subW = maxW * Min(1.f, 0.8f + 0.2f * k);   // larger text wraps to more lines (keeps clear of the corner HUD)
    TextStyle st = hud_ui::style(FONT_BODY, 28.f * sc * k, withAlpha(kWhite, a), ALIGN_CENTER);
    st.outline = 2.f * sc * k;
    st.outlineColor = C(0.f, 0.f, 0.03f, 0.9f * a);
    st.shadow = 2.f * sc * k;
    st.shadowSoft = 0.7f;
    std::string line;
    if (!speaker.empty()) {
        u32 c = uo.speakerColors ? speakerColor : kWhite;
        line = StrFormat("~#%02x%02x%02x~%s:~s~ ", c & 255, (c >> 8) & 255, (c >> 16) & 255, speaker.c_str()) + text;
    } else line = text;
    RichOpts ro;
    ro.pad = padPrompts;
    ro.alpha = a;
    vec2 sz = richMeasure(line.c_str(), st, subW, ro);
    float y = bottom - sz.y;
    if (uo.subtitleBackground > 0.01f) {
        float px = 18.f * sc * k, py = 9.f * sc * k;
        roundRect(cx - sz.x * 0.5f - px, y - py, sz.x + 2.f * px, sz.y + 2.f * py, 10.f * sc, C(0.01f, 0.01f, 0.04f, 0.9f * uo.subtitleBackground * a));
    }
    richDraw(cx, y, line.c_str(), st, subW, ro);
    return y;
}

// Looking through a magnifying scope: a round field of view in a dark surround (soft edge, darker lens rim), with a
// duplex crosshair (kind 0) or a mil-dot sniper reticle (kind 1); the centre turns red over a hostile.
void drawScopeView(const Layout& L, vec2 c, int kind, float a, bool onEnemy) {
    float sc = L.s;
    float R = Min(L.W, L.H) * 0.46f;
    u32 black = C(0.f, 0.f, 0.f, a);
    // surround: the four sides of the aperture's square, then a ring out to its corners
    rect(0.f, 0.f, L.W, c.y - R, black);
    rect(0.f, c.y + R, L.W, L.H - (c.y + R), black);
    rect(0.f, c.y - R, c.x - R, 2.f * R, black);
    rect(c.x + R, c.y - R, L.W - (c.x + R), 2.f * R, black);
    ringSoft(c.x, c.y, R * 1.25f, R * 0.5f + 4.f, 6.f * sc, black);
    ringSoft(c.x, c.y, R - 14.f * sc, 40.f * sc, 60.f * sc, C(0.f, 0.f, 0.f, 0.55f * a));   // lens rim shading
    u32 ink = C(0.02f, 0.02f, 0.02f, 0.95f * a);
    float thin = 1.6f * sc, thick = 6.f * sc;
    float inner = R * (kind == 1 ? 0.62f : 0.36f);
    for (int q = 0; q < 4; q++) {
        vec2 d = q == 0 ? vec2(1, 0) : q == 1 ? vec2(-1, 0) : q == 2 ? vec2(0, 1) : vec2(0, -1);
        vec2 e = c + d * R, m = c + d * inner;
        line(m.x, m.y, e.x, e.y, thick, ink);                       // heavy outer posts
        line(c.x, c.y, m.x, m.y, thin, ink);                        // fine centre lines
        if (kind == 1)
            for (int k = 1; k <= 4; k++) {                          // mil dots
                vec2 p = c + d * (inner * k / 5.f);
                circle(p.x, p.y, 2.6f * sc, ink);
            }
    }
    u32 dot = onEnemy ? C(1.f, 0.12f, 0.1f, a) : C(0.9f, 0.9f, 0.85f, 0.9f * a);
    circle(c.x, c.y, 2.2f * sc, dot);
}

void drawReticleShape(vec2 c, float sc, float spread, int target, float a) {
    bool hc = uiOptions().highContrastReticle;   // bigger, bolder, yellow on black (Settings > Accessibility)
    u32 col = target == 1 ? (hc ? C(1.f, 0.1f, 0.1f) : kRed) : target == 2 ? kCyan : (hc ? C(1.f, 0.93f, 0.1f) : kWhite);
    u32 ol = C(0.f, 0.f, 0.02f, (hc ? 1.f : 0.7f) * a);
    float k = hc ? 1.6f : 1.f;
    circle(c.x, c.y, (3.4f * k + (hc ? 1.2f : 0.f)) * sc, ol);
    circle(c.x, c.y, 2.3f * k * sc, withAlpha(col, a));
    float sp = 7.f * sc * k + spread * sc;
    if (spread > 0.5f || hc) {
        for (int q = 0; q < 4; q++) {
            vec2 d = q == 0 ? vec2(1, 0) : q == 1 ? vec2(-1, 0) : q == 2 ? vec2(0, 1) : vec2(0, -1);
            vec2 p0 = c + d * sp, p1 = c + d * (sp + 9.f * sc * k);
            capsule(p0.x, p0.y, p1.x, p1.y, (4.2f * k + (hc ? 1.5f : 0.f)) * sc, ol);
            capsule(p0.x, p0.y, p1.x, p1.y, 2.2f * k * sc, withAlpha(col, a));
        }
    }
}

}  // namespace uix

// ------------------------------------------------------------------------------------------------------------------
void hudInit() {
    double t0 = TimeSeconds();
    uix::ensureIcons();
    uix::mapInit();
    LOG("hudInit done in %.0f ms", (TimeSeconds() - t0) * 1000.0);
}

void hudReset() { hud_ui::g = hud_ui::State(); }

void drawHud(const HudState& s, float dt) {
    using namespace hud_ui;
    uix::ensureIcons();
    uix::advanceTime(dt);
    dt = Clamp(dt, 0.f, 0.1f);
    if (uix::photoModeActive()) return;   // photo mode shows the bare, graded frame
    Layout L = layout();
    L.s *= uiOptions().hudScale;          // Settings > Accessibility > HUD Scale (anchors stay in the safe area)
    float t = uiTime();
    if (g.first) {
        g.first = false;
        g.radarAlpha = s.radarVisible ? 1.f : 0.f;
        g.zoomShown = s.radarZoom;
        g.healthShown = g.healthLag = s.health;
        g.armorShown = g.armorLag = s.armor;
        g.specialShown = s.special;
        g.lastHealth = s.health;
    }
    g.radarAlpha = fadeTo(g.radarAlpha, s.radarVisible, dt, 5.f, 5.f);
    drawCenter(s, L, dt, t);
    drawLockOn(s, L, dt, t);
    drawRadar(s, L, dt, t);
    drawTopRight(s, L, dt, t);
    drawTopLeft(s, L, dt, t);
    drawRadio(s, L, dt);
    if (!uix::phoneCoversBottomRight()) drawBottomRight(s, L, dt, t);   // the phone sits there
    drawBottomCenter(s, L, dt);
    drawBigMessage(s, L, dt);
    drawWeaponWheel(s, L, dt, t);
}

}  // namespace UI
