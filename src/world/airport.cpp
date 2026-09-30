// Porto Sol International Airport meshes: runway/taxiway markings and lights, terminal with the wave roof,
// concourses, jet bridges, control tower, hangars, fuel farm, aircraft (original designs), fence, approach lights.
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace airport_mesh {

using namespace sitegeo;

const u32 kPaintW = 0xffffffffu;
inline u32 matWhite() { return M(MAT_PAINT_WHITE); }
inline u32 matYellow() { return M(MAT_PAINT_YELLOW); }

// ------------------------------------------------------------------------------------------------ runway
void genRunway(const SiteElem& e, G& g) {
    vec2 a = e.a, b = e.b;
    vec2 d = normalize(b - a), n = perp(d);
    float len = length(b - a), W = e.p[0], hw = W * 0.5f;
    float z = e.z + 0.03f + 0.012f;
    u32 white = matWhite(), yellow = matYellow();
    const u32 cW = kPaintW;
    // Names: "09L|27R" -> west end label, east end label
    std::string nm = e.text;
    std::string endName[2] = {nm.substr(0, nm.find('|')), nm.substr(nm.find('|') + 1)};
    for (int end = 0; end < 2; end++) {
        vec2 thr = end == 0 ? a : b;
        vec2 in = end == 0 ? d : -d;       // landing direction from this threshold
        vec2 rightV = -perp(in);           // pilot's right
        // Threshold bars
        int perSide = W >= 55.f ? 8 : 6;
        for (int s = -1; s <= 1; s += 2)
            for (int k = 0; k < perSide; k++) {
                vec2 c = thr + in * (6.f + 15.f) + n * (s * (3.9f + k * 3.4f));
                if (g.owns(c)) paintRect(g, c, in, 15.f, 0.9f, z, cW, white);
            }
        // Designation: letter nearer the threshold, number beyond it, readable by the approaching pilot
        std::string des = endName[end];
        std::string number = des.substr(0, 2), letter = des.size() > 2 ? des.substr(2) : "";
        float numH = W >= 55.f ? 18.f : 15.f;
        float stroke = numH * 0.09f;
        vec3 up3(in, 0.f), rt3(rightV, 0.f);
        float dist = 6.f + 30.f + 12.f;
        if (!letter.empty()) {
            float wL = textAdvance(letter.c_str(), numH, 0.f);
            vec2 o = thr + in * dist - rightV * (wL * 0.5f - numH * 0.06f);
            if (g.owns(thr + in * (dist + numH * 0.5f))) strokeText(g, *g.d, letter.c_str(), vec3(o, z), rt3, up3, numH, stroke, cW, white, 0.f, 0.f);
            dist += numH + 6.f;
        }
        {
            float wN = textAdvance(number.c_str(), numH, 0.35f) - numH / 9.f * 2.1f;
            vec2 o = thr + in * dist - rightV * (wN * 0.5f);
            if (g.owns(thr + in * (dist + numH * 0.5f))) strokeText(g, *g.d, number.c_str(), vec3(o, z), rt3, up3, numH, stroke, cW, white, 0.f, 0.35f);
        }
        // Aiming point (300 m) and touchdown zone bars
        for (int s = -1; s <= 1; s += 2) {
            vec2 ap = thr + in * (305.f + 22.5f) + n * (s * (W >= 55.f ? 11.5f : 9.f));
            if (g.owns(ap)) paintRect(g, ap, in, 22.5f, W >= 55.f ? 4.5f : 3.6f, z, cW, white);
            const float tdz[] = {150.f, 450.f, 600.f, 750.f, 900.f};
            const int bars[] = {3, 2, 2, 1, 1};
            for (int t = 0; t < 5; t++)
                for (int k = 0; k < bars[t]; k++) {
                    vec2 c = thr + in * (tdz[t] + 11.25f) + n * (s * (hw * 0.28f + k * 3.3f));
                    if (g.owns(c)) paintRect(g, c, in, 11.25f, 0.9f, z, cW, white);
                }
        }
        // Blast pad chevrons (yellow) beyond the runway end
        float bl = e.p[2];
        for (float t = 8.f; t < bl; t += 15.f) {
            vec2 apex = thr - in * t;
            for (int s = -1; s <= 1; s += 2) {
                vec2 tip = apex - in * (hw * 0.9f) + n * (s * hw * 0.9f);
                vec2 mid = (apex + tip) * 0.5f;
                if (g.owns(mid)) paintLine(g, apex, tip, 1.0f, z - 0.015f, cW, yellow);
            }
        }
        // Threshold / end lights (green + red bi-colour), PAPI units on the left side
        for (int k = -6; k <= 6; k++) {
            vec2 p = thr - in * 1.5f + n * (k * hw / 6.5f);
            if (!g.owns(p)) continue;
            lamp(g, vec3(p, e.z + 0.25f), 0.32f, vec3(0.1f, 1.f, 0.35f), 0.9f);
            lamp(g, vec3(p - in * 0.5f, e.z + 0.25f), 0.3f, vec3(1.f, 0.08f, 0.05f), 0.8f);
        }
        vec2 papi = thr + in * 300.f - rightV * (hw + 16.f);
        if (g.owns(papi)) {
            for (int k = 0; k < 4; k++) {
                vec2 p = papi - rightV * (k * 9.f);
                boxY(g, vec3(p, e.z + 0.75f), in, vec3(0.6f, 0.45f, 0.35f), rgb(0.85f, 0.85f, 0.82f), M(MAT_METAL_PAINTED));
                if (g.detail) boxY(g, vec3(p, e.z + 0.2f), in, vec3(0.08f, 0.08f, 0.4f), rgb(0.4f), M(MAT_METAL_PAINTED));
                vec3 col = k < 2 ? vec3(1.f, 0.95f, 0.85f) : vec3(1.f, 0.1f, 0.06f);
                lamp(g, vec3(p + in * 0.62f, e.z + 0.8f), 0.28f, col, 0.95f);
            }
        }
    }
    // Centerline dashes (36 m stripe, 24 m gap) between the designations
    float cl0 = 120.f, cl1 = len - 120.f;
    for (float s = cl0; s + 36.f <= cl1; s += 60.f) {
        vec2 c = a + d * (s + 18.f);
        if (g.owns(c)) paintRect(g, c, d, 18.f, 0.45f, z, cW, white);
    }
    // Side stripes
    for (int sd = -1; sd <= 1; sd += 2) {
        for (float s = 0.f; s < len; s += 120.f) {
            float e1 = Min(len, s + 120.f);
            vec2 c = a + d * ((s + e1) * 0.5f) + n * (sd * (hw - 1.0f));
            if (g.owns(c)) paintRect(g, c, d, (e1 - s) * 0.5f, 0.45f, z, cW, white);
        }
    }
    // Edge lights every 60 m (last 600 m amber), centerline lights every 30 m (near LOD only)
    for (float s = 0.f; s <= len + 0.1f; s += 60.f) {
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 p = a + d * s + n * (sd * (hw + 1.2f));
            if (!g.owns(p)) continue;
            bool amber = s > len - 600.f || s < 600.f;
            vec3 col = amber ? vec3(1.f, 0.78f, 0.35f) : vec3(1.f, 0.96f, 0.88f);
            if (g.detail) boxY(g, vec3(p, e.z + 0.18f), d, vec3(0.06f, 0.06f, 0.18f), rgb(0.9f, 0.8f, 0.2f), M(MAT_METAL_PAINTED));
            lamp(g, vec3(p, e.z + 0.42f), 0.26f, col, 0.9f);
        }
    }
    if (g.detail)
        for (float s = 30.f; s < len; s += 30.f) {
            vec2 p = a + d * s;
            if (!g.owns(p)) continue;
            g.d->box(vec3(p, z + 0.01f) - g.org, vec3(d, 0), vec3(n, 0), vec3(0, 0, 1), vec3(0.18f, 0.12f, 0.02f), rgb(0.95f, 0.95f, 1.f, 0.5f), emMat(), false);
        }
}

// ------------------------------------------------------------------------------------------------ taxiways
void genTaxiMarks(const SiteElem& e, G& g) {
    vec2 a = e.a, b = e.b;
    vec2 d = normalize(b - a), n = perp(d);
    float len = length(b - a), hw = e.p[0] * 0.5f;
    bool connector = e.p[1] > 0.5f;
    float z = e.z + 0.02f + 0.012f;
    const u32 cW = kPaintW;
    u32 yellow = matYellow();
    float endLen = connector ? len - e.p[2] : len;
    // Centerline (continuous yellow), split per cell
    for (float s = 0.f; s < endLen; s += 40.f) {
        float s1 = Min(endLen, s + 40.f);
        vec2 c = a + d * ((s + s1) * 0.5f);
        if (g.owns(c)) paintRect(g, c, d, (s1 - s) * 0.5f, 0.2f, z, cW, yellow);
    }
    // Edge markings (double yellow) where no other pavement joins
    const SiteSet& S = *gSites;
    for (int sd = -1; sd <= 1; sd += 2) {
        for (float s = 0.f; s < endLen; s += 10.f) {
            float s1 = Min(endLen, s + 10.f);
            vec2 c = a + d * ((s + s1) * 0.5f) + n * (sd * hw);
            if (!g.owns(c)) continue;
            // open where another pad continues beyond the edge (apron, connectors)
            vec2 probe = c + n * (sd * 5.5f);
            const Pad* pd = S.padAt(probe);
            if (pd && (pd->kind == PAD_APRON || pd->kind == PAD_TAXIWAY)) continue;
            paintRect(g, c - n * (sd * 0.0f), d, (s1 - s) * 0.5f, 0.1f, z, cW, yellow);
            paintRect(g, c + n * (sd * 0.35f), d, (s1 - s) * 0.5f, 0.1f, z, cW, yellow);
        }
    }
    // Blue edge lights every 45 m
    for (float s = 0.f; s <= endLen; s += 45.f)
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 p = a + d * s + n * (sd * (hw + 4.2f));
            if (!g.owns(p)) continue;
            const Pad* pd = S.padAt(p + n * (sd * 2.f));
            if (pd && (pd->kind == PAD_APRON || pd->kind == PAD_TAXIWAY)) continue;
            if (g.detail) cyl(g, vec3(p, e.z), 0.05f, 0.05f, 0.3f, 5, rgb(0.2f, 0.25f, 0.6f), M(MAT_METAL_PAINTED), false);
            lamp(g, vec3(p, e.z + 0.36f), 0.2f, vec3(0.15f, 0.35f, 1.f), 0.9f);
        }
    if (!connector) return;
    // Hold-short marking (2 solid + 2 dashed) and the mandatory / location signs
    float holdS = len - 75.f;
    vec2 hc = a + d * holdS;
    if (g.owns(hc)) {
        for (int k = 0; k < 2; k++) paintRect(g, hc - d * (0.35f + k * 0.45f), n, hw, 0.08f, z, cW, yellow);
        for (int k = 0; k < 2; k++)
            for (float t = -hw; t < hw; t += 1.8f) paintRect(g, hc + d * (0.35f + k * 0.45f) + n * (t + 0.45f), n, 0.45f, 0.08f, z, cW, yellow);
        // Mandatory sign (red, runway name) on the left, location sign (black/yellow) on the right
        vec2 left = n;
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 sp = hc - d * 2.f + left * (sd * (hw + 7.f));
            vec3 base(sp, e.z);
            vec3 faceN(-d, 0);
            vec3 rt = vec3(-n.x, -n.y, 0) * -1.f;
            float sw = 5.2f, sh = 1.1f;
            boxY(g, base + vec3(0, 0, 0.95f), n, vec3(sw * 0.5f, 0.25f, sh * 0.5f), rgb(0.15f, 0.15f, 0.15f), M(MAT_METAL_PAINTED));
            // faces (both sides), lit at night
            bool mand = sd > 0;
            u32 faceCol = mand ? rgb(0.85f, 0.06f, 0.05f, 0.18f) : rgb(0.05f, 0.05f, 0.05f, 0.05f);
            u32 txtCol = mand ? rgb(1.f, 1.f, 1.f, 0.5f) : rgb(1.f, 0.85f, 0.1f, 0.45f);
            for (int f = -1; f <= 1; f += 2) {
                vec3 fn = faceN * (float)f;
                vec3 c = base + vec3(0, 0, 0.95f) + fn * 0.27f;
                vec3 rr = vec3(perp(vec2(fn.x, fn.y)), 0);
                quad(g, *g.m, c - rr * (sw * 0.48f) - vec3(0, 0, sh * 0.45f), c + rr * (sw * 0.48f) - vec3(0, 0, sh * 0.45f),
                     c + rr * (sw * 0.48f) + vec3(0, 0, sh * 0.45f), c - rr * (sw * 0.48f) + vec3(0, 0, sh * 0.45f), faceCol, emMat(), fn);
                if (g.detail) {
                    std::string t = mand ? (e.b.y > 1500.f ? "09L-27R" : "09R-27L") : e.text;
                    float th = 0.62f;
                    float tw = textAdvance(t.c_str(), th, 0.3f);
                    strokeText(g, *g.m, t.c_str(), c + fn * 0.02f - rr * (tw * 0.5f) - vec3(0, 0, th * 0.5f), rr, vec3(0, 0, 1), th, 0.09f, txtCol, emMat(), 0.f,
                               0.3f);
                }
            }
            (void)rt;
            if (g.detail)
                for (int l = -1; l <= 1; l += 2) boxY(g, base + vec3(n * (l * sw * 0.35f), 0.2f), n, vec3(0.06f, 0.06f, 0.22f), rgb(0.3f), M(MAT_METAL_PAINTED));
        }
    }
}

// ------------------------------------------------------------------------------------------------ apron markings
void genApronMarks(const SiteElem& e, G& g) {
    const u32 cW = kPaintW;
    u32 yellow = matYellow(), white = matWhite();
    float z = e.z + 0.012f;
    if (e.variant == 2) {
        // Stand: lead-in line along the aircraft axis, stop bar, stand number, red equipment restraint outline
        vec2 nose = e.c, dir = e.ax, l = perp(dir);
        float span = e.p[0];
        float leadLen = span > 50.f ? 95.f : 70.f;
        for (float s = 4.f; s < leadLen; s += 12.f) {
            float s1 = Min(leadLen, s + 12.f);
            vec2 c = nose - dir * ((s + s1) * 0.5f);
            if (g.owns(c)) paintRect(g, c, dir, (s1 - s) * 0.5f, 0.2f, z, cW, yellow);
        }
        if (!g.owns(nose)) return;
        paintRect(g, nose - dir * 2.f, l, 2.5f, 0.25f, z, cW, yellow);
        // stand number painted on the pavement, readable from the cockpit
        if (g.detail) {
            float th = 2.4f;
            float tw = textAdvance(e.text.c_str(), th, 0.3f);
            vec3 up3(dir, 0), rt3(-perp(dir), 0);
            vec2 o = nose - dir * 9.f - (-perp(dir)) * (tw * 0.5f) + l * 0.f;
            strokeText(g, *g.d, e.text.c_str(), vec3(o, z), rt3, up3, th, 0.3f, cW, yellow, 0.f, 0.3f);
        }
        // equipment restraint area (red) around the stand
        float hwid = span * 0.5f + 3.f, len = span > 50.f ? 72.f : 44.f;
        vec2 cc = nose - dir * (len * 0.5f - 2.f);
        u32 red = rgb(0.75f, 0.05f, 0.04f);
        paintRect(g, cc + l * hwid, dir, len * 0.5f, 0.12f, z, red, white);
        paintRect(g, cc - l * hwid, dir, len * 0.5f, 0.12f, z, red, white);
        return;
    }
    // Large apron: taxilane centerlines (yellow) and the vehicle service road (white dashed) along the terminal side
    vec2 c = e.c;
    float x0 = c.x - e.hx, x1 = c.x + e.hx, y0 = c.y - e.hy, y1 = c.y + e.hy;
    std::vector<std::pair<vec2, vec2>> lanes;
    if (e.variant == 0) {
        lanes.push_back({vec2(x0, 1535.f), vec2(x1 - 40.f, 1535.f)});
        lanes.push_back({vec2(x0, 1345.f), vec2(x1 - 40.f, 1345.f)});
        lanes.push_back({vec2(x0, 1810.f), vec2(x1 - 10.f, 1810.f)});
        lanes.push_back({vec2(x0, 1085.f + 0.f), vec2(x1 - 10.f, 1090.f)});
        lanes.push_back({vec2(178.f, y0 + 10.f), vec2(178.f, y1 - 10.f)});
        lanes.push_back({vec2(60.f, y0 + 10.f), vec2(60.f, y1 - 10.f)});
        // service road along the pier tips and terminal
        for (int k = 0; k < 2; k++) {
            float xx = 196.f + k * 4.f;
            for (float y = y0 + 5.f; y < y1 - 5.f; y += 9.f) {
                vec2 pc(xx, y + 2.f);
                if (g.owns(pc)) paintRect(g, pc, vec2(0, 1), 2.f, 0.12f, z, cW, white);
            }
        }
    } else {
        lanes.push_back({vec2(-560.f, y0 + 10.f), vec2(-560.f, y1 - 10.f)});
        lanes.push_back({vec2(-300.f, y0 + 10.f), vec2(-300.f, y1 - 10.f)});
        lanes.push_back({vec2(x0 + 10.f, 1480.f), vec2(x1, 1480.f)});
        lanes.push_back({vec2(x0 + 60.f, 1620.f), vec2(x0 + 100.f, 1620.f)});
        lanes.push_back({vec2(x0 + 60.f, 1760.f), vec2(x0 + 100.f, 1760.f)});
    }
    for (auto& ln : lanes) {
        vec2 a = ln.first, b = ln.second;
        vec2 d = normalize(b - a);
        float L = length(b - a);
        for (float s = 0; s < L; s += 30.f) {
            float s1 = Min(L, s + 30.f);
            vec2 m = a + d * ((s + s1) * 0.5f);
            if (g.owns(m)) paintRect(g, m, d, (s1 - s) * 0.5f, 0.2f, z, cW, yellow);
        }
    }
}

// ------------------------------------------------------------------------------------------------ aircraft
struct AirSpec {
    float L, R, zc;             // length, fuselage radius, fuselage axis height
    float noseL, tailL;         // nose / tail cone lengths
    float wingX, rootC, tipC, semi, sweep, dihedral, wingZ;  // wing: root LE distance from nose, chords, semi span...
    float stabX, stabSemi, stabRootC, stabTipC;
    float finX, finH, finRootC, finTipC;
    int engines;                // 2 wing, 4 wing, -2 rear fuselage
    float engY[2], engX[2], engR, engL;
    bool tTail, hump, winglet;
};

AirSpec airSpec(int type) {
    AirSpec s = {};
    switch (type) {
        default:
        case 0: s = {37.6f, 1.98f, 2.95f, 4.6f, 7.6f, 12.8f, 6.3f, 1.6f, 16.8f, 25.f, 5.f, -0.6f, 30.8f, 6.1f, 3.6f, 1.4f, 29.6f, 5.9f, 5.2f, 1.7f,
                     2, {5.8f, 0.f}, {10.8f, 0.f}, 1.02f, 4.3f, false, false, true};
            break;
        case 1: s = {63.7f, 3.05f, 4.45f, 7.f, 12.f, 22.f, 11.f, 2.4f, 30.f, 31.f, 6.f, -1.0f, 53.f, 10.6f, 6.f, 2.2f, 50.5f, 9.6f, 9.f, 3.f,
                     2, {9.8f, 0.f}, {19.5f, 0.f}, 1.85f, 7.2f, false, false, true};
            break;
        case 2: s = {70.6f, 3.25f, 4.85f, 7.5f, 13.f, 25.f, 13.f, 3.f, 31.5f, 37.f, 6.f, -1.1f, 60.f, 11.f, 7.f, 2.4f, 56.5f, 11.2f, 10.f, 3.2f,
                     4, {11.8f, 21.5f}, {23.f, 29.f}, 1.35f, 5.4f, false, true, false};
            break;
        case 3: s = {31.f, 1.36f, 2.15f, 3.4f, 6.f, 11.6f, 4.6f, 1.3f, 12.3f, 26.f, 3.f, -0.4f, 28.5f, 4.3f, 2.4f, 1.1f, 25.3f, 4.8f, 4.6f, 2.6f,
                     -2, {2.35f, 0.f}, {21.8f, 0.f}, 0.76f, 4.2f, true, false, false};
            break;
    }
    return s;
}

struct Livery {
    const char* name;
    vec3 body, belly, stripe, tail, logo, engine;
};
const Livery kLiveries[7] = {
    {"PALMERA AIR", vec3(0.95f, 0.95f, 0.94f), vec3(0.72f, 0.74f, 0.76f), vec3(0.05f, 0.55f, 0.55f), vec3(0.03f, 0.45f, 0.48f), vec3(1.f, 0.72f, 0.2f),
     vec3(0.9f, 0.9f, 0.9f)},
    {"CORALINA", vec3(0.96f, 0.95f, 0.93f), vec3(0.96f, 0.95f, 0.93f), vec3(0.95f, 0.38f, 0.3f), vec3(0.95f, 0.36f, 0.28f), vec3(1.f, 0.95f, 0.9f),
     vec3(0.95f, 0.38f, 0.3f)},
    {"GULFWING", vec3(0.94f, 0.95f, 0.97f), vec3(0.1f, 0.2f, 0.45f), vec3(0.1f, 0.2f, 0.45f), vec3(0.08f, 0.16f, 0.4f), vec3(0.95f, 0.8f, 0.2f),
     vec3(0.1f, 0.2f, 0.45f)},
    {"SOL ATLANTIC", vec3(0.97f, 0.96f, 0.92f), vec3(0.85f, 0.2f, 0.15f), vec3(1.f, 0.75f, 0.1f), vec3(0.85f, 0.18f, 0.12f), vec3(1.f, 0.8f, 0.1f),
     vec3(0.95f, 0.95f, 0.95f)},
    {"AERO CAYO", vec3(0.92f, 0.95f, 0.98f), vec3(0.92f, 0.95f, 0.98f), vec3(0.35f, 0.75f, 0.2f), vec3(0.2f, 0.55f, 0.85f), vec3(0.45f, 0.85f, 0.25f),
     vec3(0.2f, 0.55f, 0.85f)},
    {"TRANSVELA", vec3(0.93f, 0.93f, 0.93f), vec3(0.45f, 0.47f, 0.5f), vec3(0.45f, 0.1f, 0.35f), vec3(0.35f, 0.08f, 0.3f), vec3(0.9f, 0.9f, 0.9f),
     vec3(0.6f, 0.6f, 0.62f)},
    {"MANGROVE CARGO", vec3(0.92f, 0.93f, 0.9f), vec3(0.6f, 0.62f, 0.6f), vec3(0.15f, 0.45f, 0.2f), vec3(0.12f, 0.38f, 0.18f), vec3(0.95f, 0.85f, 0.3f),
     vec3(0.92f, 0.93f, 0.9f)},
};

// Local aircraft frame: X forward (toward the nose), Y left, Z up. Origin at the nose tip on the ground.
struct AF {
    vec3 o;       // world position of the nose tip (ground)
    vec3 X, Y, Z;
    vec3 P(float x, float y, float z) const { return o + X * x + Y * y + Z * z; }
    vec3 V(vec3 v) const { return X * v.x + Y * v.y + Z * v.z; }
};

// Tube along the local X axis: rings at x positions (negative = aft of nose), radius and z center per ring
void fuselage(G& g, const AF& f, const AirSpec& s, const Livery& lv, bool detail, bool freighter) {
    int seg = detail ? 16 : 8;
    std::vector<float> xs;
    int nn = detail ? 8 : 4, nt = detail ? 7 : 4;
    for (int i = 0; i <= nn; i++) {
        float t = (float)i / nn;
        xs.push_back(-s.noseL * (1.f - cosf(t * kHalfPi)));  // denser near the tip
    }
    xs.push_back(-s.noseL - (s.L - s.noseL - s.tailL) * 0.5f);
    for (int i = 0; i <= nt; i++) xs.push_back(-(s.L - s.tailL) - s.tailL * (float)i / nt);
    auto radiusAt = [&](float x) {
        float d = -x;
        if (d < s.noseL) {
            float t = d / s.noseL;
            return s.R * sqrtf(Max(0.f, 1.f - (1.f - t) * (1.f - t))) * (0.08f + 0.92f * Min(1.f, t * 3.f + 0.1f));
        }
        if (d > s.L - s.tailL) {
            float t = (d - (s.L - s.tailL)) / s.tailL;
            return s.R * Lerp(1.f, 0.14f, powf(t, 1.3f));
        }
        return s.R;
    };
    auto zcAt = [&](float x) {
        float d = -x;
        if (d < s.noseL) return s.zc - 0.22f * s.R * (1.f - d / s.noseL);
        if (d > s.L - s.tailL) return s.zc + 0.62f * s.R * powf((d - (s.L - s.tailL)) / s.tailL, 1.4f);
        return s.zc;
    };
    auto humpAt = [&](float x) {
        if (!s.hump) return 0.f;
        float d = -x;
        return 2.3f * SmoothStep(2.5f, 9.f, d) * (1.f - SmoothStep(24.f, 32.f, d));
    };
    MeshData& m = *g.m;
    u32 mat = M(MAT_METAL_PAINTED);
    u32 base = (u32)m.verts.size();
    for (size_t i = 0; i < xs.size(); i++) {
        float x = xs[i], r = radiusAt(x), zc = zcAt(x), hp = humpAt(x);
        bool tail = -x > s.L - s.tailL * 1.15f;
        for (int k = 0; k <= seg; k++) {
            float th = kTwoPi * k / seg;  // 0 = left side, pi/2 = top
            float cy = cosf(th), sz = sinf(th);
            float zz = sz * r + (sz > 0.f ? hp * sz * sz : 0.f);
            vec3 p = f.P(x, cy * r, zc + zz);
            vec3 nrm = f.V(normalize(vec3(0.f, cy, sz + (sz > 0 ? hp * 0.3f * sz : 0.f))));
            if (i == 0) nrm = f.X;
            vec3 col = lv.body;
            if (sz < -0.42f) col = lv.belly;
            else if (sz < -0.18f && !freighter) col = lerp(lv.body, lv.stripe, 0.85f);
            if (tail && sz > -0.2f) col = lerp(col, lv.tail, 0.8f);
            m.addVertex(p - g.org, nrm, f.V(vec3(0, -sz, cy)), vec2(-x, th * r), rgbv(col), mat);
        }
    }
    for (size_t i = 0; i + 1 < xs.size(); i++)
        for (int k = 0; k < seg; k++) {
            u32 a = base + (u32)(i * (seg + 1) + k), b = a + 1, c = a + seg + 1, d = c + 1;
            m.quadIdx(a, c, d, b);
        }
    // Windows (near LOD: individual panes, far LOD: none), cockpit windscreen
    float wz = s.zc + s.R * 0.28f;
    float wy = sqrtf(Max(0.f, s.R * s.R - (s.R * 0.28f) * (s.R * 0.28f))) + 0.015f;
    u32 winCol = rgb(0.08f, 0.1f, 0.13f);
    if (detail && !freighter) {
        float pitch = s.R > 2.5f ? 0.62f : 0.54f;
        float wH = s.R > 2.5f ? 0.5f : 0.42f, wW = 0.3f;
        for (float x = -s.noseL - 3.f; x > -(s.L - s.tailL - 1.f); x -= pitch) {
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 c = f.P(x, sd * wy, wz);
                vec3 out = f.Y * (float)sd;
                vec3 a0 = c - f.X * (wW * 0.5f) - f.Z * (wH * 0.5f), a1 = c + f.X * (wW * 0.5f) - f.Z * (wH * 0.5f);
                vec3 a2 = c + f.X * (wW * 0.5f) + f.Z * (wH * 0.5f), a3 = c - f.X * (wW * 0.5f) + f.Z * (wH * 0.5f);
                m.quadFacing(a0 - g.org, a1 - g.org, a2 - g.org, a3 - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), winCol, M(MAT_GLASS), out);
            }
        }
        // Doors (outline as slightly darker panels)
        for (int sd = -1; sd <= 1; sd += 2)
            for (int k = 0; k < 2; k++) {
                float x = k == 0 ? -(s.noseL + 1.2f) : -(s.L - s.tailL - 1.5f);
                vec3 c = f.P(x, sd * (wy - 0.005f), s.zc + s.R * 0.05f);
                vec3 out = f.Y * (float)sd;
                float dh = Min(1.9f, s.R * 0.95f), dw = 0.85f;
                vec3 a0 = c - f.X * dw * 0.5f - f.Z * dh * 0.5f, a1 = c + f.X * dw * 0.5f - f.Z * dh * 0.5f;
                vec3 a2 = c + f.X * dw * 0.5f + f.Z * dh * 0.5f, a3 = c - f.X * dw * 0.5f + f.Z * dh * 0.5f;
                m.quadFacing(a0 - g.org, a1 - g.org, a2 - g.org, a3 - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(lv.body * 0.82f), mat, out);
            }
    } else if (!freighter) {
        // far: a thin window band
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 out = f.Y * (float)sd;
            vec3 a0 = f.P(-s.noseL - 2.f, sd * wy, wz - 0.25f), a1 = f.P(-(s.L - s.tailL - 1.f), sd * wy, wz - 0.25f);
            m.quadFacing(a0 - g.org, a1 - g.org, a1 + f.Z * 0.5f - g.org, a0 + f.Z * 0.5f - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), winCol, M(MAT_GLASS), out);
        }
    }
    // Windscreen: two slanted panels per side near the nose top
    {
        float x0 = -s.noseL * 0.42f, x1 = -s.noseL * 0.78f;
        float r0 = radiusAt(x0), r1 = radiusAt(x1);
        float z0 = zcAt(x0) + r0 * 0.45f, z1 = zcAt(x1) + r1 * 0.62f;
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 p0 = f.P(x0, sd * r0 * 0.35f, z0 + 0.03f), p1 = f.P(x0, sd * r0 * 0.9f, zcAt(x0) + r0 * 0.3f);
            vec3 p2 = f.P(x1, sd * r1 * 0.9f, zcAt(x1) + r1 * 0.38f), p3 = f.P(x1, sd * r1 * 0.3f, z1 + 0.03f);
            vec3 out = normalize(f.X * 0.6f + f.Z * 0.7f + f.Y * (sd * 0.4f));
            m.quadFacing(p0 - g.org, p1 - g.org, p2 - g.org, p3 - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), winCol, M(MAT_GLASS), out);
        }
    }
}

// Tapered swept surface (wing / stabiliser / fin) between a root and a tip section.
// rootLE/tipLE in local frame, chords along -X, thickness ratio, `vertical` for fins (thickness along Y)
void liftSurface(G& g, const AF& f, vec3 rootLE, vec3 tipLE, float rootC, float tipC, float thick, bool vertical, u32 col, u32 col2) {
    vec3 up = vertical ? vec3(0, 1, 0) : vec3(0, 0, 1);
    float tr = rootC * thick * 0.5f, tt = tipC * thick * 0.5f;
    // chordwise stations: LE, 30% (max thickness), TE
    vec3 r[3] = {rootLE, rootLE + vec3(-rootC * 0.3f, 0, 0), rootLE + vec3(-rootC, 0, 0)};
    vec3 t[3] = {tipLE, tipLE + vec3(-tipC * 0.3f, 0, 0), tipLE + vec3(-tipC, 0, 0)};
    float rt[3] = {0.25f * tr, tr, 0.05f * tr}, tt3[3] = {0.25f * tt, tt, 0.05f * tt};
    MeshData& m = *g.m;
    u32 mat = M(MAT_METAL_PAINTED);
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 0; k < 2; k++) {
            vec3 a = r[k] + up * (side * rt[k]), b = r[k + 1] + up * (side * rt[k + 1]);
            vec3 c = t[k + 1] + up * (side * tt3[k + 1]), d = t[k] + up * (side * tt3[k]);
            vec3 A = f.P(a.x, a.y, a.z), B = f.P(b.x, b.y, b.z), C = f.P(c.x, c.y, c.z), D = f.P(d.x, d.y, d.z);
            vec3 facing = f.V(up * (float)side);
            m.quadFacing(A - g.org, B - g.org, C - g.org, D - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), side > 0 ? col : col2, mat, facing);
        }
        // leading edge nose strip
        vec3 a = r[0] + up * (side * rt[0]), d = t[0] + up * (side * tt3[0]);
        vec3 A = f.P(a.x, a.y, a.z), D = f.P(d.x, d.y, d.z);
        vec3 B = f.P(r[0].x + rootC * 0.02f, r[0].y, r[0].z), C = f.P(t[0].x + tipC * 0.02f, t[0].y, t[0].z);
        m.quadFacing(A - g.org, B - g.org, C - g.org, D - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), col, mat, f.V(vec3(1, 0, 0) + up * (side * 0.3f)));
    }
    // tip cap
    vec3 T0 = f.P(t[0].x, t[0].y, t[0].z), T2 = f.P(t[2].x, t[2].y, t[2].z);
    vec3 T1u = f.P(t[1].x + up.x * tt, t[1].y + up.y * tt, t[1].z + up.z * tt), T1d = f.P(t[1].x - up.x * tt, t[1].y - up.y * tt, t[1].z - up.z * tt);
    vec3 outward = normalize(f.V(tipLE - rootLE) - f.V(up) * dot(f.V(tipLE - rootLE), f.V(up)));
    m.quadFacing(T0 - g.org, T1u - g.org, T2 - g.org, T1d - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), col, mat, outward);
}

// Engine nacelle along the local X axis (front at xFront)
void nacelle(G& g, const AF& f, float xFront, float y, float z, float r, float len, u32 col, bool detail) {
    int seg = detail ? 14 : 7;
    std::vector<vec2> prof = {vec2(r * 0.92f, 0.f), vec2(r, 0.25f * len), vec2(r * 0.97f, 0.65f * len), vec2(r * 0.72f, len)};
    MeshData& m = *g.m;
    u32 mat = M(MAT_METAL_PAINTED);
    u32 base = (u32)m.verts.size();
    for (size_t i = 0; i < prof.size(); i++) {
        for (int k = 0; k <= seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 rad = f.Y * cosf(th) + f.Z * sinf(th);
            vec3 p = f.P(xFront - prof[i].y, y, z) + rad * prof[i].x;
            m.addVertex(p - g.org, rad, f.X, vec2(th * r, prof[i].y), col, mat);
        }
    }
    for (size_t i = 0; i + 1 < prof.size(); i++)
        for (int k = 0; k < seg; k++) {
            u32 a = base + (u32)(i * (seg + 1) + k), b = a + 1, c = a + seg + 1, d = c + 1;
            m.quadIdx(a, b, d, c);
        }
    // intake (dark) and exhaust cone
    std::vector<vec3> ring;
    vec3 fc = f.P(xFront - 0.25f, y, z);
    u32 dark = rgb(0.12f, 0.12f, 0.13f);
    u32 c0 = m.addVertex(fc - f.X * 0.1f - g.org, f.X, f.Y, vec2(0, 0), dark, M(MAT_METAL_BRUSHED));
    u32 r0 = (u32)m.verts.size();
    for (int k = 0; k <= seg; k++) {
        float th = kTwoPi * k / seg;
        vec3 p = fc + (f.Y * cosf(th) + f.Z * sinf(th)) * (r * 0.9f);
        m.addVertex(p - g.org, f.X, f.Y, vec2(cosf(th), sinf(th)), dark, M(MAT_METAL_BRUSHED));
    }
    for (int k = 0; k < seg; k++) m.tri(c0, r0 + k + 1, r0 + k);
    if (detail) {
        vec3 ec = f.P(xFront - len, y, z);
        u32 tipI = m.addVertex(ec - f.X * (r * 0.9f) - g.org, -f.X, f.Y, vec2(0, 0), rgb(0.35f, 0.33f, 0.3f), M(MAT_METAL_BRUSHED));
        u32 rr = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 rad = f.Y * cosf(th) + f.Z * sinf(th);
            m.addVertex(ec + rad * (r * 0.62f) - g.org, normalize(rad - f.X * 0.5f), f.Y, vec2(th, 0), rgb(0.35f, 0.33f, 0.3f), M(MAT_METAL_BRUSHED));
        }
        for (int k = 0; k < seg; k++) m.tri(tipI, rr + k, rr + k + 1);
    }
}

void wheelPair(G& g, const AF& f, float x, float y, float r, float w) {
    for (int s = -1; s <= 1; s += 2) {
        vec3 c = f.P(x, y + s * (w * 0.5f + 0.1f), r);
        rod(g, c - f.Y * (w * 0.5f), c + f.Y * (w * 0.5f), r, 10, rgb(0.06f, 0.06f, 0.06f), M(MAT_RUBBER));
    }
}

void genAirliner(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    int type = e.variant;
    AirSpec s = airSpec(type);
    const Livery& lv = kLiveries[Clamp((int)e.p[0], 0, 6)];
    bool freighter = e.p[2] > 0.5f;
    AF f;
    f.o = vec3(e.c, e.z);
    f.X = vec3(e.ax, 0);
    f.Y = vec3(perp(e.ax), 0);
    f.Z = vec3(0, 0, 1);
    bool detail = g.detail;
    fuselage(g, f, s, lv, detail, freighter);
    u32 wingTop = rgbv(lv.body * 0.92f + vec3(0.02f)), wingBot = rgbv(lv.belly * 0.9f);
    // Wings
    for (int sd = -1; sd <= 1; sd += 2) {
        float rootY = s.R * 0.55f;
        vec3 root(-s.wingX, sd * rootY, s.zc + s.wingZ);
        float sweepX = s.semi * tanf(s.sweep * kDegToRad);
        vec3 tip(-s.wingX - sweepX, sd * (rootY + s.semi), s.zc + s.wingZ + s.semi * tanf(s.dihedral * kDegToRad));
        liftSurface(g, f, root, tip, s.rootC, s.tipC, 0.11f, false, wingTop, wingBot);
        if (s.winglet && detail) {
            vec3 wl0 = tip + vec3(-0.2f, 0, 0), wl1 = tip + vec3(-s.tipC * 0.8f - 0.6f, sd * 0.25f, s.R * 0.9f);
            liftSurface(g, f, wl0, wl1, s.tipC * 0.9f, s.tipC * 0.45f, 0.08f, true, rgbv(lv.tail), rgbv(lv.tail));
        }
        // navigation light at the wing tip (red left, green right), lit on some aircraft
        if ((e.seed & 3) == 0) {
            vec3 nl = f.P(tip.x - s.tipC * 0.2f, tip.y + sd * 0.15f, tip.z);
            lamp(g, nl, 0.22f, sd > 0 ? vec3(1.f, 0.08f, 0.05f) : vec3(0.1f, 1.f, 0.3f), 0.9f);
        }
    }
    // Horizontal stabiliser and fin
    {
        float finTop = s.zc + s.R * 0.62f + s.finH;
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 root, tip;
            if (s.tTail) {
                root = vec3(-s.stabX - s.finH * 0.55f, 0.f, finTop - 0.2f);
                tip = vec3(-s.stabX - s.finH * 0.55f - s.stabSemi * 0.5f, sd * s.stabSemi, finTop);
            } else {
                root = vec3(-s.stabX, sd * s.R * 0.3f, s.zc + s.R * 0.25f);
                tip = vec3(-s.stabX - s.stabSemi * 0.62f, sd * (s.R * 0.3f + s.stabSemi), s.zc + s.R * 0.25f + s.stabSemi * 0.12f);
            }
            liftSurface(g, f, root, tip, s.stabRootC, s.stabTipC, 0.09f, false, wingTop, wingBot);
        }
        vec3 froot(-s.finX, 0.f, s.zc + s.R * 0.55f);
        vec3 ftip(-s.finX - s.finH * 0.75f, 0.f, finTop);
        liftSurface(g, f, froot, ftip, s.finRootC, s.finTipC, 0.1f, true, rgbv(lv.tail), rgbv(lv.tail));
        // tail logo: a sun disc on both fin sides
        if (detail) {
            vec3 lc = (froot + ftip) * 0.5f + vec3(-s.finRootC * 0.35f, 0, 0);
            float lr = s.finH * 0.16f;
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 cW = f.P(lc.x, sd * (s.finRootC * 0.055f + 0.03f), lc.z);
                std::vector<vec3> ring;
                u32 b0 = (u32)g.m->verts.size();
                vec3 out = f.Y * (float)sd;
                g.m->addVertex(cW - g.org, out, f.X, vec2(0, 0), rgbv(lv.logo), M(MAT_METAL_PAINTED));
                for (int k = 0; k <= 12; k++) {
                    float a = kTwoPi * k / 12;
                    vec3 p = cW + f.X * (cosf(a) * lr) + f.Z * (sinf(a) * lr);
                    g.m->addVertex(p - g.org, out, f.X, vec2(0, 0), rgbv(lv.logo), M(MAT_METAL_PAINTED));
                }
                for (int k = 0; k < 12; k++) {
                    if (sd > 0) g.m->tri(b0, b0 + 1 + k, b0 + 2 + k);
                    else g.m->tri(b0, b0 + 2 + k, b0 + 1 + k);
                }
            }
        }
        if ((e.seed & 3) == 0) lamp(g, f.P(-s.L + 0.3f, 0.f, s.zc + s.R * 0.9f), 0.2f, vec3(1.f), 0.8f);
        if ((e.seed & 7) == 0) lamp(g, f.P(-s.L * 0.45f, 0.f, s.zc + s.R + 0.15f), 0.25f, vec3(1.f, 0.05f, 0.03f), 1.f, EA_BLINK, e.seed & 255u);
    }
    // Engines + pylons
    u32 engCol = rgbv(lv.engine);
    if (s.engines > 0) {
        for (int i = 0; i < s.engines; i++) {
            int sd = (i & 1) ? 1 : -1;
            int k = s.engines == 4 ? i / 2 : 0;
            float ey = s.engY[k] * sd;
            float span01 = (s.engY[k] - s.R * 0.55f) / s.semi;
            float wingLE = -s.wingX - s.semi * tanf(s.sweep * kDegToRad) * span01;
            float wz = s.zc + s.wingZ + s.engY[k] * tanf(s.dihedral * kDegToRad);
            float ez = wz - s.engR - 0.45f;
            float xf = wingLE + s.engL * 0.55f;
            nacelle(g, f, xf, ey, ez, s.engR, s.engL, engCol, detail);
            vec3 pa = f.P(xf - s.engL * 0.5f, ey, ez + s.engR * 0.7f), pb = f.P(wingLE - 1.f, ey, wz);
            beam(g, pa, pb, 0.35f, s.engR * 0.6f, wingBot, M(MAT_METAL_PAINTED), f.Y);
            if (detail) collide(g, f.P(xf - s.engL * 0.5f, ey, ez), e.ax, vec3(s.engL * 0.5f, s.engR, s.engR));
        }
    } else {
        for (int sd = -1; sd <= 1; sd += 2) {
            float ey = sd * (s.R + s.engR + 0.35f);
            float ez = s.zc + s.R * 0.45f;
            nacelle(g, f, -s.engX[0], ey, ez, s.engR, s.engL, engCol, detail);
            beam(g, f.P(-s.engX[0] - s.engL * 0.5f, sd * s.R * 0.8f, ez), f.P(-s.engX[0] - s.engL * 0.5f, ey - sd * s.engR * 0.6f, ez), 1.4f, 0.25f, wingBot,
                 M(MAT_METAL_PAINTED), f.Z);
        }
    }
    // Landing gear
    if (detail) {
        float wr = s.R > 2.5f ? 0.62f : 0.5f;
        float gx = -s.noseL * 0.7f;
        rod(g, f.P(gx, 0, wr), f.P(gx, 0, s.zc - s.R * 0.8f), 0.12f, 6, rgb(0.8f, 0.8f, 0.8f), M(MAT_METAL_BRUSHED));
        wheelPair(g, f, gx, 0.f, wr * 0.75f, 0.3f);
        float mx = -s.wingX - s.rootC * 0.55f;
        int axles = type == 1 || type == 2 ? 3 : 1;
        for (int sd = -1; sd <= 1; sd += 2) {
            float my = sd * (s.R * 0.95f + (type == 2 ? 0.8f : 0.f));
            rod(g, f.P(mx, my, wr * 2.f), f.P(mx, my, s.zc + s.wingZ), 0.2f, 6, rgb(0.8f, 0.8f, 0.8f), M(MAT_METAL_BRUSHED));
            for (int ax = 0; ax < axles; ax++) wheelPair(g, f, mx + (ax - (axles - 1) * 0.5f) * 1.45f, my, wr, 0.45f);
        }
        if (type == 2)
            for (int sd = -1; sd <= 1; sd += 2)
                for (int ax = 0; ax < 2; ax++) wheelPair(g, f, mx + 3.f + ax * 1.45f, sd * s.R * 0.35f, wr, 0.45f);
        // Airline name along the upper fuselage
        float th = s.R * 0.36f;
        float tw = textAdvance(lv.name, th, 0.28f);
        for (int sd = -1; sd <= 1; sd += 2) {
            // left side (+Y) reads nose -> tail, right side (-Y) reads tail -> nose (both upright for a viewer outside)
            float y = sd * (sqrtf(Max(0.f, s.R * s.R - (s.R * 0.62f) * (s.R * 0.62f))) + 0.02f);
            vec3 out = f.Y * (float)sd;
            vec3 rightV = sd > 0 ? -f.X : f.X;
            vec3 o = sd > 0 ? f.P(-s.noseL - 4.f, y, s.zc + s.R * 0.55f) : f.P(-s.noseL - 4.f - tw, y, s.zc + s.R * 0.55f);
            strokeText(g, *g.m, lv.name, o + out * 0.02f, rightV, f.Z, th, th * 0.13f, rgbv(lv.stripe), M(MAT_METAL_PAINTED), 0.f, 0.28f);
        }
        collide(g, f.P(-s.L * 0.5f, 0, s.zc), e.ax, vec3(s.L * 0.5f, s.R, s.R));
        collide(g, f.P(-s.wingX - s.rootC * 0.5f, 0, s.zc + s.wingZ), e.ax, vec3(s.rootC * 0.5f, s.R + s.semi * 0.45f, 0.35f));
    }
    // Ground support equipment around parked aircraft
    if (detail && e.p[1] > 0.5f) {
        Rng r(e.seed ^ 0x65E);
        vec2 dir = e.ax, rgt = -perp(dir);  // aircraft right side (service side)
        vec3 base(e.c, e.z);
        auto vbox = [&](vec2 p, vec2 d, vec3 he, u32 c, float z0) { boxY(g, base + vec3(p - e.c, 0) + vec3(0, 0, z0 + he.z), d, he, c, M(MAT_METAL_PAINTED)); };
        // baggage tug + carts
        vec2 p0 = e.c - dir * (s.noseL + 7.f) + rgt * (s.R + 7.f);
        vbox(p0, dir, vec3(1.3f, 0.8f, 0.55f), rgb(0.95f, 0.8f, 0.1f), 0.3f);
        for (int k = 1; k <= 3; k++) {
            vec2 pc = p0 - dir * (k * 3.4f);
            vbox(pc, dir, vec3(1.3f, 0.85f, 0.15f), rgb(0.35f, 0.35f, 0.38f), 0.35f);
            vbox(pc, dir, vec3(1.2f, 0.8f, 0.6f), rgbv(hsvToRgb(r.f(), 0.5f, 0.7f)), 0.55f);
        }
        // belt loader to the forward hold
        vec2 bl = e.c - dir * (s.noseL + 5.f) + rgt * (s.R + 2.2f);
        vec3 lo = base + vec3(bl - e.c, 0) + vec3(0, 0, 0.6f) + vec3(rgt, 0) * 3.5f, hi = base + vec3(bl - e.c, 0) + vec3(0, 0, s.zc - s.R * 0.4f);
        beam(g, lo, hi, 0.9f, 0.25f, rgb(0.2f, 0.2f, 0.22f), M(MAT_RUBBER));
        vbox(bl + rgt * 4.5f, rgt, vec3(1.5f, 0.9f, 0.5f), rgb(0.95f, 0.8f, 0.1f), 0.3f);
        // catering truck at the rear door
        vec2 ct = e.c - dir * (s.L - s.tailL - 2.f) + rgt * (s.R + 3.5f);
        vbox(ct, rgt, vec3(3.f, 1.2f, 0.5f), rgb(0.9f, 0.9f, 0.92f), 0.4f);
        vbox(ct - rgt * 0.5f, rgt, vec3(2.2f, 1.15f, 1.2f), rgb(0.95f, 0.95f, 0.95f), s.zc - 1.5f);
        // fuel dispenser under the wing
        vec2 fd = e.c - dir * (s.wingX + s.rootC * 0.3f) + rgt * (s.R + s.semi * 0.35f);
        vbox(fd, dir, vec3(2.6f, 1.1f, 0.9f), rgb(0.85f, 0.85f, 0.3f), 0.3f);
        // ground power unit and cones
        vbox(e.c + dir * 2.f + rgt * 2.f, dir, vec3(1.f, 0.7f, 0.6f), rgb(0.2f, 0.35f, 0.6f), 0.2f);
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 cone = e.c - dir * (s.wingX + s.semi * tanf(s.sweep * kDegToRad) + 2.f) + perp(dir) * (sd * (s.semi + s.R * 0.6f));
            cyl(g, vec3(cone, e.z), 0.22f, 0.02f, 0.7f, 6, rgb(1.f, 0.4f, 0.05f), M(MAT_PLASTER), false);
        }
    }
}

// Light aircraft: 0 high-wing single, 1 low-wing twin
void genSmallPlane(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    AF f;
    f.o = vec3(e.c, e.z);
    f.X = vec3(e.ax, 0);
    f.Y = vec3(perp(e.ax), 0);
    f.Z = vec3(0, 0, 1);
    Rng r(e.seed);
    vec3 accent = hsvToRgb(0.55f + e.p[0] * 0.13f, 0.7f, 0.75f);
    u32 bodyC = rgb(0.95f, 0.95f, 0.95f), acc = rgbv(accent);
    bool twin = e.variant == 1;
    float L = twin ? 9.2f : 8.2f, R = twin ? 0.7f : 0.62f, zc = twin ? 1.3f : 1.25f;
    // fuselage (tapered tube)
    int seg = g.detail ? 10 : 6;
    std::vector<float> xs = {0.f, -0.6f, -1.6f, -3.2f, -5.2f, -L};
    std::vector<float> rs = {0.18f, R * 0.8f, R, R, R * 0.6f, R * 0.18f};
    std::vector<float> zs = {zc, zc, zc + 0.1f, zc + 0.1f, zc + 0.15f, zc + 0.35f};
    MeshData& m = *g.m;
    u32 b0 = (u32)m.verts.size();
    for (size_t i = 0; i < xs.size(); i++)
        for (int k = 0; k <= seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 p = f.P(xs[i], cosf(th) * rs[i], zs[i] + sinf(th) * rs[i] * 1.15f);
            bool stripe = sinf(th) > -0.35f && sinf(th) < -0.05f;
            m.addVertex(p - g.org, f.V(vec3(0, cosf(th), sinf(th))), f.X, vec2(0, 0), stripe ? acc : bodyC, M(MAT_METAL_PAINTED));
        }
    for (size_t i = 0; i + 1 < xs.size(); i++)
        for (int k = 0; k < seg; k++) {
            u32 a = b0 + (u32)(i * (seg + 1) + k), b = a + 1, c = a + seg + 1, d = c + 1;
            m.quadIdx(a, c, d, b);
        }
    // cabin windows
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 c = f.P(-2.2f, sd * (R + 0.01f), zc + R * 0.55f);
        vec3 out = f.Y * (float)sd;
        m.quadFacing(c - f.X * 0.9f - f.Z * 0.3f - g.org, c + f.X * 0.9f - f.Z * 0.3f - g.org, c + f.X * 0.7f + f.Z * 0.3f - g.org, c - f.X * 0.9f + f.Z * 0.3f - g.org,
                     vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.08f, 0.1f, 0.12f), M(MAT_GLASS), out);
    }
    float wz = twin ? zc - R * 0.5f : zc + R * 1.2f;
    for (int sd = -1; sd <= 1; sd += 2) {
        liftSurface(g, f, vec3(-1.8f, sd * 0.3f, wz), vec3(-2.f, sd * 5.6f, wz + (twin ? 0.3f : 0.f)), 1.6f, 1.1f, 0.13f, false, bodyC, bodyC);
        liftSurface(g, f, vec3(-L + 1.3f, sd * 0.15f, zs.back()), vec3(-L + 0.8f, sd * 1.8f, zs.back()), 0.9f, 0.6f, 0.1f, false, bodyC, bodyC);
        if (!twin && g.detail) rod(g, f.P(-2.3f, sd * R * 0.9f, zc - R * 0.6f), f.P(-2.4f, sd * 2.5f, wz), 0.04f, 4, bodyC, M(MAT_METAL_PAINTED));
    }
    liftSurface(g, f, vec3(-L + 1.6f, 0.f, zs.back() + 0.1f), vec3(-L + 0.6f, 0.f, zs.back() + 1.5f), 1.3f, 0.7f, 0.1f, true, acc, acc);
    // props / engines
    auto prop = [&](vec3 hub) {
        cyl(g, hub - f.X * 0.1f + vec3(0, 0, 0), 0.01f, 0.01f, 0.01f, 3, bodyC, M(MAT_METAL_PAINTED), false);
        float a = r.f() * kPi;
        vec3 bladeDir = f.Y * cosf(a) + f.Z * sinf(a);
        beam(g, hub - bladeDir * 0.95f, hub + bladeDir * 0.95f, 0.12f, 0.03f, rgb(0.1f, 0.1f, 0.1f), M(MAT_METAL_PAINTED), f.X);
    };
    if (twin) {
        for (int sd = -1; sd <= 1; sd += 2) {
            nacelle(g, f, -0.8f, sd * 2.2f, wz + 0.15f, 0.35f, 2.2f, bodyC, g.detail);
            if (g.detail) prop(f.P(-0.75f, sd * 2.2f, wz + 0.15f));
        }
    } else if (g.detail) prop(f.P(0.05f, 0.f, zc));
    if (g.detail) {
        wheelPair(g, f, -0.9f, 0.f, 0.22f, 0.12f);
        for (int sd = -1; sd <= 1; sd += 2) wheelPair(g, f, -2.9f, sd * 1.25f, 0.26f, 0.14f);
        collide(g, f.P(-L * 0.5f, 0, zc), e.ax, vec3(L * 0.5f, 0.8f, 0.8f));
    }
}

void genHelicopter(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    AF f;
    f.o = vec3(e.c, e.z) + vec3(e.ax * 4.5f, 0);
    f.X = vec3(e.ax, 0);
    f.Y = vec3(perp(e.ax), 0);
    f.Z = vec3(0, 0, 1);
    u32 red = rgb(0.8f, 0.08f, 0.06f), white = rgb(0.95f, 0.95f, 0.95f);
    int seg = g.detail ? 12 : 6;
    std::vector<float> xs = {0.f, -0.8f, -2.f, -3.8f, -5.f, -9.8f};
    std::vector<float> rs = {0.2f, 0.9f, 1.15f, 1.1f, 0.5f, 0.16f};
    std::vector<float> zs = {1.35f, 1.45f, 1.6f, 1.7f, 1.95f, 2.3f};
    MeshData& m = *g.m;
    u32 b0 = (u32)m.verts.size();
    for (size_t i = 0; i < xs.size(); i++)
        for (int k = 0; k <= seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 p = f.P(xs[i], cosf(th) * rs[i], zs[i] + sinf(th) * rs[i] * (i < 4 ? 1.1f : 1.f));
            m.addVertex(p - g.org, f.V(vec3(0, cosf(th), sinf(th))), f.X, vec2(0, 0), (sinf(th) < -0.2f || i >= 4) ? red : white, M(MAT_METAL_PAINTED));
        }
    for (size_t i = 0; i + 1 < xs.size(); i++)
        for (int k = 0; k < seg; k++) {
            u32 a = b0 + (u32)(i * (seg + 1) + k), b = a + 1, c = a + seg + 1, d = c + 1;
            m.quadIdx(a, c, d, b);
        }
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 c = f.P(-1.1f, sd * 1.02f, 1.85f);
        m.quadFacing(c - f.X * 0.9f - f.Z * 0.4f - g.org, c + f.X * 0.6f - f.Z * 0.4f - g.org, c + f.X * 0.2f + f.Z * 0.4f - g.org, c - f.X * 0.9f + f.Z * 0.4f - g.org,
                     vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.06f, 0.08f, 0.1f), M(MAT_GLASS), f.Y * (float)sd);
        if (g.detail) {
            rod(g, f.P(0.3f, sd * 1.1f, 0.12f), f.P(-3.8f, sd * 1.1f, 0.12f), 0.06f, 5, rgb(0.3f), M(MAT_METAL_PAINTED));
            rod(g, f.P(-0.8f, sd * 1.1f, 0.12f), f.P(-1.f, sd * 0.5f, 1.1f), 0.05f, 4, rgb(0.3f), M(MAT_METAL_PAINTED));
            rod(g, f.P(-3.f, sd * 1.1f, 0.12f), f.P(-3.f, sd * 0.5f, 1.1f), 0.05f, 4, rgb(0.3f), M(MAT_METAL_PAINTED));
        }
    }
    liftSurface(g, f, vec3(-9.f, 0.f, 2.3f), vec3(-9.7f, 0.f, 3.5f), 1.f, 0.6f, 0.1f, true, red, red);
    vec3 hub = f.P(-2.4f, 0.f, 3.05f);
    cyl(g, hub - vec3(0, 0, 0.35f), 0.12f, 0.12f, 0.35f, 6, rgb(0.3f), M(MAT_METAL_BRUSHED), true);
    for (int k = 0; k < 4; k++) {
        float a = kHalfPi * k + 0.4f;
        vec3 d = f.X * cosf(a) + f.Y * sinf(a);
        beam(g, hub, hub + d * 5.4f - vec3(0, 0, 0.15f), 0.32f, 0.05f, rgb(0.15f, 0.15f, 0.15f), M(MAT_METAL_PAINTED));
    }
    if (g.detail) collide(g, f.P(-2.f, 0, 1.6f), e.ax, vec3(2.6f, 1.2f, 1.3f));
}

void genHelipad(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    float r = e.hx, z = e.z + 0.013f;
    // painted circle, H and edge lights
    int n = 40;
    for (int k = 0; k < n; k++) {
        float a0 = kTwoPi * k / n, a1 = kTwoPi * (k + 1) / n;
        vec2 p0 = e.c + vec2(cosf(a0), sinf(a0)) * (r - 0.6f), p1 = e.c + vec2(cosf(a1), sinf(a1)) * (r - 0.6f);
        paintLine(g, p0, p1, 0.9f, z, rgb(1.f, 0.85f, 0.1f), matYellow());
    }
    float th = r * 0.9f;
    strokeText(g, *g.d, "H", vec3(e.c + vec2(-th * 0.33f, -th * 0.5f), z), vec3(1, 0, 0), vec3(0, 1, 0), th, 1.2f, kWhiteC, matWhite(), 0.f, 0.f);
    for (int k = 0; k < 12; k++) {
        float a = kTwoPi * k / 12;
        lamp(g, vec3(e.c + vec2(cosf(a), sinf(a)) * (r + 0.6f), e.z + 0.15f), 0.25f, vec3(0.15f, 1.f, 0.3f), 0.9f);
    }
}

// ------------------------------------------------------------------------------------------------ terminal
float terminalRoofZ(const SiteElem& e, float x, float y) {
    // wave along the length (y) plus a gentle rise toward the landside (east) edge
    float y0 = e.c.y - e.hx - 10.f;
    float x0 = e.c.x - e.hy - e.p[1];
    float xspan = e.hy * 2.f + e.p[0] + e.p[1];
    return e.z + 26.f + 4.5f * sinf((y - y0) / 145.f * kTwoPi + 0.6f) + 3.2f * Saturate((x - x0) / xspan);
}

void genTerminal(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    float x0 = e.c.x - e.hy, x1 = e.c.x + e.hy, y0 = e.c.y - e.hx, y1 = e.c.y + e.hx;
    float z0 = e.z;
    float wallTop = z0 + 19.f;
    u32 fac = (u32)e.p[7];
    bool detail = g.detail;
    // Glass body
    std::vector<vec2> fp = {vec2(x0, y0), vec2(x1, y0), vec2(x1, y1), vec2(x0, y1)};
    facadeRing(g, fp, z0 - 1.f, wallTop, z0, fac, 3.0f);
    collide(g, vec3(e.c, z0 + 10.f), vec2(1, 0), vec3(e.hy, e.hx, 10.f));
    // Wave roof: top (white metal) + underside (warm timber) + fascia
    float rx0 = x0 - e.p[1], rx1 = x1 + e.p[0], ry0 = y0 - 10.f, ry1 = y1 + 10.f;
    int ny = detail ? (int)((ry1 - ry0) / 5.f) : (int)((ry1 - ry0) / 14.f);
    int nx = detail ? 6 : 3;
    MeshData& m = *g.m;
    u32 top = rgb(0.96f, 0.96f, 0.95f), under = rgb(1.1f, 0.95f, 0.8f);
    auto RZ = [&](float x, float y) { return terminalRoofZ(e, x, y); };
    for (int pass = 0; pass < 2; pass++) {
        u32 base = (u32)m.verts.size();
        float off = pass == 0 ? 0.f : -1.4f;
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                float x = Lerp(rx0, rx1, (float)i / nx), y = Lerp(ry0, ry1, (float)j / ny);
                float zz = RZ(x, y) + off;
                float dzdy = (RZ(x, y + 0.5f) - RZ(x, y - 0.5f));
                float dzdx = (RZ(x + 0.5f, y) - RZ(x - 0.5f, y));
                vec3 nrm = normalize(vec3(-dzdx, -dzdy, 1.f));
                if (pass == 1) nrm = -nrm;
                m.addVertex(vec3(x, y, zz) - g.org, nrm, vec3(1, 0, 0), vec2(x, y) * (pass == 0 ? 0.5f : 0.35f), pass == 0 ? top : under,
                            M(pass == 0 ? MAT_METAL_PAINTED : MAT_WOOD));
            }
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                u32 a = base + j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
                if (pass == 0) m.quadIdx(a, b, d, c);
                else m.quadIdx(a, c, d, b);
            }
    }
    // Fascia around the roof edge (brushed metal) + LED strip on the landside edge at night
    auto edgeStrip = [&](vec2 pa, vec2 pb, int steps, vec2 outN, bool led) {
        for (int k = 0; k < steps; k++) {
            vec2 qa = lerp(pa, pb, (float)k / steps), qb = lerp(pa, pb, (float)(k + 1) / steps);
            float za = RZ(qa.x, qa.y), zb = RZ(qb.x, qb.y);
            quad(g, m, vec3(qa, za + 0.05f), vec3(qb, zb + 0.05f), vec3(qb, zb - 1.45f), vec3(qa, za - 1.45f), rgb(0.8f, 0.82f, 0.85f), M(MAT_METAL_BRUSHED),
                 vec3(outN, 0));
            if (led) quad(g, m, vec3(qa + outN * 0.03f, za - 1.25f), vec3(qb + outN * 0.03f, zb - 1.25f), vec3(qb + outN * 0.03f, zb - 1.05f),
                          vec3(qa + outN * 0.03f, za - 1.05f), rgb(0.55f, 0.85f, 1.f, 0.55f), emMat(EA_NIGHT), vec3(outN, 0));
        }
    };
    edgeStrip(vec2(rx1, ry0), vec2(rx1, ry1), ny, vec2(1, 0), true);
    edgeStrip(vec2(rx0, ry1), vec2(rx0, ry0), ny, vec2(-1, 0), true);
    edgeStrip(vec2(rx0, ry0), vec2(rx1, ry0), nx, vec2(0, -1), false);
    edgeStrip(vec2(rx1, ry1), vec2(rx0, ry1), nx, vec2(0, 1), false);
    // Clerestory glass between the wall top and the roof underside
    int nc = detail ? (int)((y1 - y0) / 5.f) : (int)((y1 - y0) / 14.f);
    for (int side = 0; side < 2; side++) {
        float x = side ? x1 : x0;
        for (int k = 0; k < nc; k++) {
            float ya = Lerp(y0, y1, (float)k / nc), yb = Lerp(y0, y1, (float)(k + 1) / nc);
            float za = RZ(x, ya) - 1.4f, zb = RZ(x, yb) - 1.4f;
            quad(g, m, vec3(x, ya, wallTop), vec3(x, yb, wallTop), vec3(x, yb, zb), vec3(x, ya, za), rgb(0.55f, 0.75f, 0.8f), M(MAT_GLASS),
                 vec3(side ? 1.f : -1.f, 0, 0));
        }
    }
    for (int side = 0; side < 2; side++) {
        float y = side ? y1 : y0;
        int nk = 4;
        for (int k = 0; k < nk; k++) {
            float xa = Lerp(x0, x1, (float)k / nk), xb = Lerp(x0, x1, (float)(k + 1) / nk);
            quad(g, m, vec3(xa, y, wallTop), vec3(xb, y, wallTop), vec3(xb, y, RZ(xb, y) - 1.4f), vec3(xa, y, RZ(xa, y) - 1.4f), rgb(0.55f, 0.75f, 0.8f),
                 M(MAT_GLASS), vec3(0, side ? 1.f : -1.f, 0));
        }
    }
    // Tree columns along the curb median carry the east overhang
    for (float y = y0 + 20.f; y <= y1 - 10.f; y += 45.f) {
        vec2 base(x1 + 42.f, y);
        float zr = RZ(base.x, base.y) - 1.4f;
        float trunkTop = z0 + 13.f;
        cyl(g, vec3(base, z0), 0.85f, 0.55f, trunkTop - z0, detail ? 10 : 6, rgb(0.93f, 0.93f, 0.92f), M(MAT_METAL_PAINTED), false);
        if (detail) {
            for (int k = 0; k < 4; k++) {
                vec2 off((k & 1) ? 7.f : -7.f, (k & 2) ? 8.f : -8.f);
                vec3 tip(base + off, RZ(base.x + off.x, base.y + off.y) - 1.4f);
                rod(g, vec3(base, trunkTop - 0.3f), tip, 0.28f, 6, rgb(0.93f, 0.93f, 0.92f), M(MAT_METAL_PAINTED));
            }
            collide(g, vec3(base, z0 + 6.f), vec2(1, 0), vec3(0.8f, 0.8f, 6.f));
        } else {
            rod(g, vec3(base, trunkTop), vec3(base, zr), 0.4f, 4, rgb(0.93f), M(MAT_METAL_PAINTED));
        }
        light(g, vec3(x1 + 25.f, y, z0 + 16.f), vec3(1.f, 0.9f, 0.75f) * 9000.f, 32.f, 1, vec3(0, 0, -1), 0.25f);
    }
    // Signage on the landside facade: PORTO SOL INTERNATIONAL (3D letters, lit at night)
    {
        const char* txt = e.text.c_str();
        float th = 3.2f;
        float tw = textAdvance(txt, th, 0.3f);
        vec3 o(x1 + 0.9f, e.c.y - tw * 0.5f, z0 + 12.2f);
        strokeText(g, m, txt, o, vec3(0, 1, 0), vec3(0, 0, 1), th, 0.42f, rgb(0.9f, 0.95f, 1.f, 0.5f), emMat(EA_NIGHT), detail ? 0.3f : 0.f, 0.3f);
        float th2 = 2.2f;
        const char* dep = "DEPARTURES";
        float tw2 = textAdvance(dep, th2, 0.3f);
        vec3 o2(x1 + 0.9f, y1 - 60.f - tw2 * 0.5f, z0 + 12.6f);
        if (detail) strokeText(g, m, dep, o2, vec3(0, 1, 0), vec3(0, 0, 1), th2, 0.3f, rgb(1.f, 0.85f, 0.35f, 0.45f), emMat(EA_NIGHT), 0.2f, 0.3f);
        const char* arr = "ARRIVALS";
        float tw3 = textAdvance(arr, th2, 0.3f);
        vec3 o3(x1 + 0.9f, y0 + 60.f - tw3 * 0.5f, z0 + 12.6f);
        if (detail) strokeText(g, m, arr, o3, vec3(0, 1, 0), vec3(0, 0, 1), th2, 0.3f, rgb(1.f, 0.85f, 0.35f, 0.45f), emMat(EA_NIGHT), 0.2f, 0.3f);
    }
    // Landscaping: royal palms in the curb median
    if (detail)
        for (float y = y0 + 42.f; y <= y1 - 20.f; y += 45.f) prop(g, vec3(x1 + 42.f, y, z0), (float)(hash32((u32)y) & 1023) * 0.006f, 1.1f, PROP_PALM_TALL, 1);
}

// Concourse (pier) with gate lounges, barrel roof and a glass rotunda at the tip
void genConcourse(const SiteElem& e, G& g) {
    vec2 root = e.a, tip = e.b;
    if (!g.owns((root + tip) * 0.5f)) return;
    vec2 d = normalize(tip - root), n = perp(d);
    float hw = e.p[0], len = length(tip - root);
    float z0 = e.z, top = z0 + 11.f;
    u32 fac = (u32)e.p[7];
    std::vector<vec2> fp = {root - n * hw, tip - n * hw + d * 0.f, tip + n * hw, root + n * hw};
    // make CCW
    if (cross(fp[1] - fp[0], fp[2] - fp[1]) < 0) std::reverse(fp.begin(), fp.end());
    facadeRing(g, fp, z0 - 1.f, top, z0, fac, 3.0f);
    collide(g, vec3((root + tip) * 0.5f, z0 + 6.f), d, vec3(len * 0.5f, hw, 6.f));
    // barrel roof
    int arc = g.detail ? 8 : 4;
    MeshData& m = *g.m;
    u32 base = (u32)m.verts.size();
    for (int end = 0; end < 2; end++) {
        vec2 p = end ? tip : root;
        for (int k = 0; k <= arc; k++) {
            float t = (float)k / arc;
            float lat = Lerp(-hw - 1.f, hw + 1.f, t);
            float zz = top + 3.8f * (1.f - (2.f * t - 1.f) * (2.f * t - 1.f));
            vec3 nrm = normalize(vec3(n * ((2.f * t - 1.f) * 0.9f), 1.f));
            m.addVertex(vec3(p + n * lat, zz) - g.org, nrm, vec3(d, 0), vec2(lat, end ? len : 0.f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED));
        }
    }
    for (int k = 0; k < arc; k++) {
        u32 a = base + k, b = a + 1, c = base + arc + 1 + k, dd = c + 1;
        vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[c].pos - m.verts[a].pos);
        if (fn.z > 0) m.quadIdx(a, b, dd, c);
        else m.quadIdx(a, c, dd, b);
    }
    // gable glass at the root end and the fascia below the barrel
    for (int end = 0; end < 2; end++) {
        vec2 p = end ? tip : root;
        vec2 outD = end ? d : -d;
        std::vector<vec3> gable;
        u32 gb = (u32)m.verts.size();
        for (int k = 0; k <= arc; k++) {
            float t = (float)k / arc;
            float lat = Lerp(-hw, hw, t);
            float zz = top + 3.8f * (1.f - (2.f * t - 1.f) * (2.f * t - 1.f)) - 0.1f;
            m.addVertex(vec3(p + n * lat, zz) - g.org, vec3(outD, 0), vec3(n, 0), vec2(0, 0), rgb(0.5f, 0.7f, 0.75f), M(MAT_GLASS));
            m.addVertex(vec3(p + n * lat, top) - g.org, vec3(outD, 0), vec3(n, 0), vec2(0, 0), rgb(0.5f, 0.7f, 0.75f), M(MAT_GLASS));
        }
        for (int k = 0; k < arc; k++) {
            u32 a = gb + k * 2, b = a + 2;
            vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[a + 1].pos - m.verts[a].pos);
            if (dot(fn, vec3(outD, 0)) > 0) m.quadIdx(a, b, b + 1, a + 1);
            else m.quadIdx(a, a + 1, b + 1, b);
        }
    }
    // Tip rotunda: glass drum with a shallow dome
    vec2 rc = tip + d * 6.f;
    float rr = hw + 7.f;
    int seg = g.detail ? 20 : 10;
    std::vector<vec2> ring = circleFP(rc, rr, seg);
    facadeRing(g, ring, z0 - 1.f, top + 1.f, z0, fac, 3.0f);
    lathe(g, vec3(rc, top + 1.f), {vec2(rr + 0.8f, 0.f), vec2(rr * 0.85f, 2.2f), vec2(rr * 0.5f, 3.6f), vec2(0.f, 4.2f)}, seg, rgb(0.95f, 0.95f, 0.94f),
          M(MAT_METAL_PAINTED), false);
    collide(g, vec3(rc, z0 + 6.f), d, vec3(rr * 0.8f, rr * 0.8f, 6.f));
    // Concourse letter on the rotunda roof edge + gate lights
    if (g.detail) {
        float th = 2.4f;
        std::string t = e.text;
        float tw = textAdvance(t.c_str(), th, 0.3f);
        vec3 o(rc + d * (rr + 0.4f) - n * (tw * 0.5f), top - 3.4f);
        strokeText(g, m, t.c_str(), o, vec3(n, 0), vec3(0, 0, 1), th, 0.36f, rgb(0.9f, 0.95f, 1.f, 0.45f), emMat(EA_NIGHT), 0.2f, 0.3f);
        for (float s = 20.f; s < len; s += 60.f)
            for (int sd = -1; sd <= 1; sd += 2) light(g, vec3(root + d * s + n * (sd * (hw + 6.f)), z0 + 12.5f), vec3(1.f, 0.88f, 0.7f) * 5000.f, 26.f, 1);
    }
}

// Jet bridge: rotunda at the pier facade, sloped telescopic tunnel, cab at the aircraft door, drive bogie
void genJetBridge(const SiteElem& e, G& g) {
    if (!g.owns(e.a) || !g.detail) {
        if (!g.owns(e.a)) return;
        // far LOD: single slab
        vec3 a(e.a, e.z + 5.2f), b(e.b, e.z + e.p[0] + 0.8f);
        beam(g, a, b, 2.8f, 2.8f, rgb(0.8f, 0.8f, 0.8f), M(MAT_METAL_PAINTED));
        return;
    }
    vec2 facadeN = normalize(vec2(0.f, e.b.y > e.a.y ? 1.f : -1.f));
    vec2 rotC = e.a + facadeN * 3.5f;
    float floorZ = e.z + 5.2f, doorZ = e.z + e.p[0];
    u32 skin = rgb(0.82f, 0.83f, 0.85f), dark = rgb(0.25f, 0.27f, 0.3f);
    // fixed link from the facade to the rotunda
    beam(g, vec3(e.a, floorZ + 1.4f), vec3(rotC, floorZ + 1.4f), 2.8f, 2.9f, skin, M(MAT_METAL_PAINTED));
    cyl(g, vec3(rotC, floorZ - 0.2f), 2.3f, 2.3f, 3.3f, 12, skin, M(MAT_METAL_PAINTED), true);
    cyl(g, vec3(rotC, e.z), 0.6f, 0.6f, floorZ - e.z - 0.2f, 8, rgb(0.5f), M(MAT_METAL_PAINTED), false);
    // tunnel (two telescoping sections)
    vec2 door = e.b;
    vec2 dir = normalize(door - rotC);
    vec2 cabC = door - dir * 2.2f;
    vec3 a3(rotC + dir * 2.3f, floorZ + 1.45f), b3(cabC - dir * 1.8f, doorZ + 1.45f);
    beam(g, a3, lerp(a3, b3, 0.55f), 3.0f, 3.0f, skin, M(MAT_METAL_PAINTED));
    beam(g, lerp(a3, b3, 0.5f), b3, 2.7f, 2.75f, skin, M(MAT_METAL_PAINTED));
    // window strips along the tunnel
    vec3 side(perp(dir), 0);
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 o = side * (sd * 1.52f);
        quad(g, *g.m, a3 + o + vec3(0, 0, 0.1f), b3 + o + vec3(0, 0, 0.1f), b3 + o + vec3(0, 0, 0.6f), a3 + o + vec3(0, 0, 0.6f), dark, M(MAT_GLASS), side * (float)sd);
    }
    // cab facing the fuselage
    vec2 fus = -perp(normalize(door - e.a));
    (void)fus;
    boxY(g, vec3(cabC, doorZ + 1.4f), dir, vec3(1.8f, 1.9f, 1.55f), skin, M(MAT_METAL_PAINTED), true);
    boxY(g, vec3(cabC + dir * 1.9f, doorZ + 1.4f), dir, vec3(0.2f, 1.6f, 1.3f), rgb(0.1f, 0.1f, 0.1f), M(MAT_RUBBER), true);
    // drive column with bogie
    vec3 col = lerp(a3, b3, 0.72f);
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 leg = col + side * (sd * 1.1f);
        beam(g, vec3(leg.x, leg.y, e.z + 0.6f), vec3(leg.x, leg.y, col.z - 1.4f), 0.35f, 0.35f, rgb(0.5f, 0.5f, 0.52f), M(MAT_METAL_PAINTED), vec3(dir, 0));
    }
    boxY(g, vec3(col.x, col.y, e.z + 0.55f), dir, vec3(0.9f, 1.6f, 0.3f), rgb(0.35f), M(MAT_METAL_PAINTED));
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 wc(col.x + side.x * sd * 1.3f, col.y + side.y * sd * 1.3f, e.z + 0.45f);
        rod(g, wc - vec3(dir, 0) * 0.25f, wc + vec3(dir, 0) * 0.25f, 0.45f, 10, rgb(0.06f), M(MAT_RUBBER));
    }
    collide(g, vec3(col.x, col.y, e.z + 1.f), dir, vec3(0.9f, 1.8f, 1.f));
}

// ------------------------------------------------------------------------------------------------ control tower
void genControlTower(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec3 base(e.c, e.z);
    int seg = detail ? 20 : 10;
    u32 conc = rgb(0.93f, 0.92f, 0.9f);
    // base building (two storeys, dark window band)
    std::vector<vec2> fp = rectPoly(e.c, vec2(1, 0), 16.f, 16.f);
    prism(g, fp, e.z - 1.f, e.z + 9.f, conc, M(MAT_CONCRETE_PANEL), rgb(0.8f), M(MAT_ROOF_GRAVEL));
    for (int k = 0; k < 4; k++) {
        vec2 a = fp[k], b = fp[(k + 1) % 4];
        vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
        for (int fl = 0; fl < 2; fl++) {
            float zb = e.z + 1.2f + fl * 4.f;
            quad(g, *g.m, vec3(a + on * 0.03f, zb), vec3(b + on * 0.03f, zb), vec3(b + on * 0.03f, zb + 1.6f), vec3(a + on * 0.03f, zb + 1.6f), rgb(0.2f, 0.28f, 0.32f),
                 M(MAT_GLASS), vec3(on, 0));
        }
    }
    collide(g, vec3(e.c, e.z + 4.5f), vec2(1, 0), vec3(16.f, 16.f, 5.5f));
    // shaft with flare under the cab
    float cabZ = 74.f;
    lathe(g, base + vec3(0, 0, 9.f), {vec2(6.2f, 0.f), vec2(5.4f, 45.f), vec2(4.6f, 60.f), vec2(4.8f, cabZ - 12.f - 9.f), vec2(8.2f, cabZ - 9.f)}, seg, conc,
          M(MAT_CONCRETE), false);
    collide(g, base + vec3(0, 0, 40.f), vec2(1, 0), vec3(5.f, 5.f, 31.f));
    // three sculpted fins running up the shaft
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3.f + 0.5f;
        vec2 dir(cosf(a), sinf(a));
        vec3 p0 = base + vec3(dir * 6.2f, 9.f), p1 = base + vec3(dir * 4.6f, cabZ - 14.f);
        vec3 q0 = base + vec3(dir * 10.5f, 9.f), q1 = base + vec3(dir * 5.4f, cabZ - 14.f);
        vec3 nrm(perp(dir), 0);
        vec3 th = nrm * 0.4f;
        quad(g, *g.m, p0 + th, q0 + th, q1 + th, p1 + th, rgb(0.78f, 0.8f, 0.83f), M(MAT_METAL_PAINTED), nrm);
        quad(g, *g.m, p0 - th, q0 - th, q1 - th, p1 - th, rgb(0.78f, 0.8f, 0.83f), M(MAT_METAL_PAINTED), -nrm);
        quad(g, *g.m, q0 + th, q0 - th, q1 - th, q1 + th, rgb(0.78f, 0.8f, 0.83f), M(MAT_METAL_PAINTED), vec3(dir, 0));
        if (detail)
            for (float z = 12.f; z < cabZ - 16.f; z += 6.f) {
                float t = (z - 9.f) / (cabZ - 23.f);
                vec3 lp = base + vec3(dir * (Lerp(10.5f, 5.4f, t) + 0.1f), z);
                lamp(g, lp, 0.25f, vec3(0.5f, 0.8f, 1.f), 0.5f, EA_NIGHT);
            }
    }
    // cab: outward-leaning glass ring, slab, roof with antennas and beacon
    int cs = detail ? 12 : 8;
    float r0 = 8.f, r1 = 9.6f, zc0 = e.z + cabZ, zc1 = e.z + cabZ + 6.f;
    lathe(g, base + vec3(0, 0, cabZ - 0.6f), {vec2(8.4f, 0.f), vec2(8.4f, 0.6f)}, cs, rgb(0.75f), M(MAT_METAL_PAINTED), true);
    for (int k = 0; k < cs; k++) {
        float a0 = kTwoPi * k / cs, a1 = kTwoPi * (k + 1) / cs;
        vec3 p0(e.c + vec2(cosf(a0), sinf(a0)) * r0, zc0), p1(e.c + vec2(cosf(a1), sinf(a1)) * r0, zc0);
        vec3 p2(e.c + vec2(cosf(a1), sinf(a1)) * r1, zc1), p3(e.c + vec2(cosf(a0), sinf(a0)) * r1, zc1);
        vec3 out = normalize(vec3(cosf((a0 + a1) * 0.5f), sinf((a0 + a1) * 0.5f), -0.25f));
        quad(g, *g.m, p0, p1, p2, p3, rgb(0.3f, 0.5f, 0.45f), M(MAT_GLASS), out);
        if (detail) beam(g, p0, p3, 0.14f, 0.14f, rgb(0.2f), M(MAT_METAL_PAINTED));
    }
    // dim console glow inside the cab at night
    cyl(g, base + vec3(0, 0, cabZ + 0.2f), 6.5f, 6.5f, 1.1f, cs, rgb(0.3f, 0.9f, 0.6f, 0.08f), emMat(EA_NIGHT), false);
    lathe(g, base + vec3(0, 0, cabZ + 6.f), {vec2(10.4f, 0.f), vec2(10.4f, 0.9f), vec2(6.f, 1.4f), vec2(4.5f, 2.2f)}, cs, rgb(0.9f), M(MAT_METAL_PAINTED), true);
    float antBase = cabZ + 8.2f;
    lathe(g, base + vec3(3.f, 0, antBase), {vec2(0.1f, 0.f), vec2(1.5f, 0.8f), vec2(1.6f, 1.8f), vec2(1.1f, 2.7f), vec2(0.f, 3.1f)}, detail ? 10 : 6, rgb(0.95f),
          M(MAT_PLASTER), false);
    cyl(g, base + vec3(-2.f, 0.5f, antBase), 0.15f, 0.08f, 12.f, 6, rgb(0.85f, 0.15f, 0.1f), M(MAT_METAL_PAINTED), false);
    lamp(g, base + vec3(-2.f, 0.5f, antBase + 12.3f), 0.5f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, 17);
    light(g, base + vec3(-2.f, 0.5f, antBase + 12.3f), vec3(1.f, 0.1f, 0.05f) * 600.f, 14.f, 4);
    light(g, base + vec3(0, 0, cabZ + 3.f), vec3(0.6f, 1.f, 0.8f) * 800.f, 14.f, 1);
    // name band
    if (detail) {
        float th = 1.2f;
        const char* t = "PSI TOWER";
        float tw = textAdvance(t, th, 0.3f);
        strokeText(g, *g.m, t, base + vec3(-tw * 0.5f, -16.05f, 9.8f), vec3(1, 0, 0), vec3(0, 0, 1), th, 0.18f, rgb(0.2f, 0.25f, 0.3f), M(MAT_METAL_PAINTED));
    }
}

// ------------------------------------------------------------------------------------------------ hangar
void genHangar(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 fwd = e.ax, side = perp(fwd);
    float D = e.hx, Wd = e.hy, H = e.h;
    float wallH = H - 11.f;
    vec3 base(e.c, e.z);
    u32 wall = rgb(0.72f, 0.76f, 0.8f), roofC = rgb(0.82f, 0.84f, 0.86f);
    vec2 back = e.c - fwd * D, front = e.c + fwd * D;
    // side and back walls (corrugated)
    std::vector<vec2> fp = {back - side * Wd, front - side * Wd, front + side * Wd, back + side * Wd};
    for (int k = 0; k < 4; k++) {
        if (k == 1) continue;  // door side
        vec2 a = fp[k], b = fp[(k + 1) % 4];
        vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
        if (dot(on, a - e.c) < 0) on = -on;
        quad(g, *g.m, vec3(a, e.z - 0.5f), vec3(b, e.z - 0.5f), vec3(b, e.z + wallH), vec3(a, e.z + wallH), wall, M(MAT_CORRUGATED), vec3(on, 0));
        quad(g, *g.m, vec3(a, e.z), vec3(b, e.z), vec3(b, e.z + wallH), vec3(a, e.z + wallH), rgb(0.35f, 0.36f, 0.38f), M(MAT_CORRUGATED), vec3(-on, 0));
    }
    // arched roof (axis along fwd), outer and inner surface
    int arc = g.detail ? 14 : 6;
    MeshData& m = *g.m;
    for (int pass = 0; pass < 2; pass++) {
        u32 b0 = (u32)m.verts.size();
        for (int end = 0; end < 2; end++) {
            vec2 p = end ? front : back;
            for (int k = 0; k <= arc; k++) {
                float t = (float)k / arc;
                float lat = Lerp(-Wd, Wd, t);
                float zz = e.z + wallH + 11.f * (1.f - (2.f * t - 1.f) * (2.f * t - 1.f));
                vec3 nrm = normalize(vec3(side * ((2.f * t - 1.f) * 2.f * 11.f / Wd), 1.f));
                if (pass) nrm = -nrm;
                m.addVertex(vec3(p + side * lat, zz - pass * 0.3f) - g.org, nrm, vec3(fwd, 0), vec2(lat, end ? 2 * D : 0.f), pass ? rgb(0.4f) : roofC,
                            M(MAT_ROOF_METAL));
            }
        }
        for (int k = 0; k < arc; k++) {
            u32 a = b0 + k, b = a + 1, c = b0 + arc + 1 + k, d = c + 1;
            vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[c].pos - m.verts[a].pos);
            bool up = fn.z > 0;
            if (up != (pass == 1)) m.quadIdx(a, b, d, c);
            else m.quadIdx(a, c, d, b);
        }
    }
    // back gable
    for (int k = 0; k < arc; k++) {
        float t0 = (float)k / arc, t1 = (float)(k + 1) / arc;
        vec2 a = back + side * Lerp(-Wd, Wd, t0), b = back + side * Lerp(-Wd, Wd, t1);
        float za = e.z + wallH + 11.f * (1.f - (2.f * t0 - 1.f) * (2.f * t0 - 1.f)), zb = e.z + wallH + 11.f * (1.f - (2.f * t1 - 1.f) * (2.f * t1 - 1.f));
        quad(g, m, vec3(a, e.z + wallH), vec3(b, e.z + wallH), vec3(b, zb), vec3(a, za), wall, M(MAT_CORRUGATED), vec3(-fwd, 0));
    }
    // door header (front gable above the door opening) with the owner's name
    float doorH = wallH - 3.f;
    for (int k = 0; k < arc; k++) {
        float t0 = (float)k / arc, t1 = (float)(k + 1) / arc;
        vec2 a = front + side * Lerp(-Wd, Wd, t0), b = front + side * Lerp(-Wd, Wd, t1);
        float za = e.z + wallH + 11.f * (1.f - (2.f * t0 - 1.f) * (2.f * t0 - 1.f)), zb = e.z + wallH + 11.f * (1.f - (2.f * t1 - 1.f) * (2.f * t1 - 1.f));
        quad(g, m, vec3(a, e.z + doorH), vec3(b, e.z + doorH), vec3(b, zb), vec3(a, za), rgb(0.93f, 0.93f, 0.93f), M(MAT_METAL_PAINTED), vec3(fwd, 0));
    }
    {
        float th = 3.2f;
        float tw = textAdvance(e.text.c_str(), th, 0.3f);
        vec3 rt(-side.x, -side.y, 0);  // reads left to right for a viewer in front
        rt = vec3(side, 0) * -1.f;
        // viewer facing -fwd: right = cross(-fwd, up) = -perp(-fwd)... use explicit
        vec2 viewR = -perp(-fwd);
        vec3 o(front + fwd * 0.1f - viewR * (tw * 0.5f), e.z + doorH + 2.2f);
        strokeText(g, m, e.text.c_str(), o, vec3(viewR, 0), vec3(0, 0, 1), th, 0.45f, rgb(0.08f, 0.25f, 0.55f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
        (void)rt;
    }
    // sliding door panels: open fraction p[0] slides panels to both sides
    float open = e.p[0];
    int panels = 8;
    float pw = 2.f * Wd / panels;
    for (int k = 0; k < panels; k++) {
        float lat = -Wd + (k + 0.5f) * pw;
        float shift = 0.f;
        if (open > 0.f) shift = (lat < 0 ? -1.f : 1.f) * open * Wd * 0.9f * (1.f - fabsf(lat) / Wd * 0.6f);
        float l2 = Clamp(lat + shift, -Wd + pw * 0.5f, Wd - pw * 0.5f);
        float depthOff = 0.4f + (k & 1) * 0.6f;
        boxY(g, vec3(front + fwd * depthOff + side * l2, e.z + doorH * 0.5f), side, vec3(pw * 0.5f, 0.25f, doorH * 0.5f), rgb(0.55f, 0.6f, 0.66f),
             M(MAT_CORRUGATED));
    }
    // interior floor and lights
    polyFlat(g, m, fp, e.z + 0.02f, rgb(0.85f), M(MAT_CONCRETE));
    for (int k = -1; k <= 1; k++) light(g, vec3(e.c + side * (k * Wd * 0.55f), e.z + wallH), vec3(0.95f, 0.97f, 1.f) * 7000.f, 45.f, 1, vec3(0, 0, -1), 0.3f);
    for (int k = -1; k <= 1; k += 2) {
        vec3 lp(front + fwd * 1.f + side * (k * Wd * 0.6f), e.z + doorH + 1.f);
        light(g, lp, vec3(1.f, 0.85f, 0.6f) * 9000.f, 50.f, 1, normalize(vec3(fwd, -0.8f)), 0.3f);
        lamp(g, lp, 0.6f, vec3(1.f, 0.9f, 0.7f), 0.7f, EA_NIGHT);
    }
    // collision: walls, roof, closed door panels
    collide(g, vec3(back, e.z + wallH * 0.5f), fwd, vec3(0.5f, Wd, wallH * 0.5f + 1.f));
    for (int sd = -1; sd <= 1; sd += 2) collide(g, vec3(e.c + side * (sd * Wd), e.z + wallH * 0.5f), fwd, vec3(D, 0.5f, wallH * 0.5f + 1.f));
    collide(g, vec3(e.c, e.z + H - 3.f), fwd, vec3(D, Wd, 3.f));
    if (open < 0.05f) collide(g, vec3(front, e.z + doorH * 0.5f), fwd, vec3(1.f, Wd, doorH * 0.5f));
}

// ------------------------------------------------------------------------------------------------ fuel farm
void genFuelFarm(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    // earth berm
    float bx = e.hx - 5.f, by = e.hy - 5.f, bh = 2.2f;
    std::vector<vec2> outer = rectPoly(e.c, vec2(1, 0), bx + 4.f, by + 4.f), inner = rectPoly(e.c, vec2(1, 0), bx, by);
    for (int k = 0; k < 4; k++) {
        vec2 o0 = outer[k], o1 = outer[(k + 1) % 4], i0 = inner[k], i1 = inner[(k + 1) % 4];
        vec2 on = normalize(vec2(o1.y - o0.y, o0.x - o1.x));
        vec2 mid0 = (o0 + i0) * 0.5f, mid1 = (o1 + i1) * 0.5f;
        quad(g, *g.m, vec3(o0, e.z - 0.3f), vec3(o1, e.z - 0.3f), vec3(mid1, e.z + bh), vec3(mid0, e.z + bh), rgb(0.55f, 0.75f, 0.45f), M(MAT_GRASS),
             vec3(on, 1.f));
        quad(g, *g.m, vec3(i1, e.z), vec3(i0, e.z), vec3(mid0, e.z + bh), vec3(mid1, e.z + bh), rgb(0.55f, 0.75f, 0.45f), M(MAT_GRASS), vec3(-on, 1.f));
    }
    polyFlat(g, *g.m, inner, e.z + 0.05f, rgb(0.8f, 0.78f, 0.72f), M(MAT_CONCRETE));
    int seg = detail ? 28 : 12;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 3; i++) {
            vec2 c = e.c + vec2((i - 1) * 50.f, (j ? 28.f : -28.f));
            float r = 15.f, h = 15.f;
            lathe(g, vec3(c, e.z), {vec2(r, 0.f), vec2(r, h), vec2(r - 0.4f, h + 0.3f), vec2(r * 0.55f, h + 1.6f), vec2(0.f, h + 2.2f)}, seg, rgb(0.93f, 0.93f, 0.9f),
                  M(MAT_METAL_PAINTED), false);
            collide(g, vec3(c, e.z + h * 0.5f), vec2(1, 0), vec3(r * 0.88f, r * 0.88f, h * 0.5f + 1.f));
            if (detail) {
                // spiral stair and top railing
                for (int k = 0; k < 10; k++) {
                    float a0 = 0.3f + k * 0.12f, a1 = a0 + 0.12f;
                    vec3 p0(c + vec2(cosf(a0), sinf(a0)) * (r + 0.6f), e.z + h * k / 10.f), p1(c + vec2(cosf(a1), sinf(a1)) * (r + 0.6f), e.z + h * (k + 1) / 10.f);
                    beam(g, p0, p1, 0.9f, 0.08f, rgb(0.5f), M(MAT_METAL_PAINTED));
                }
                for (int k = 0; k < 16; k++) {
                    float a0 = kTwoPi * k / 16, a1 = kTwoPi * (k + 1) / 16;
                    rod(g, vec3(c + vec2(cosf(a0), sinf(a0)) * (r - 0.3f), e.z + h + 1.1f), vec3(c + vec2(cosf(a1), sinf(a1)) * (r - 0.3f), e.z + h + 1.1f), 0.04f, 4,
                        rgb(0.9f, 0.8f, 0.1f), M(MAT_METAL_PAINTED));
                }
                if (i == 1) {
                    float th = 3.f;
                    const char* t = "JET A-1";
                    float tw = textAdvance(t, th, 0.3f);
                    vec2 face = j ? vec2(0, 1) : vec2(0, -1);
                    vec2 rt = perp(face);
                    float ang = tw * 0.5f / r;
                    (void)ang;
                    strokeText(g, *g.m, t, vec3(c + face * (r + 0.05f) - rt * (tw * 0.5f), e.z + 6.f), vec3(rt, 0), vec3(0, 0, 1), th, 0.4f, rgb(0.8f, 0.1f, 0.08f),
                               M(MAT_METAL_PAINTED), 0.f, 0.3f);
                }
            }
        }
    // pump house and pipe rack toward the apron
    boxY(g, vec3(e.c + vec2(e.hx - 12.f, 0.f), e.z + 3.f), vec2(1, 0), vec3(6.f, 5.f, 3.f), rgb(0.85f, 0.83f, 0.78f), M(MAT_CONCRETE_PANEL));
    if (detail) {
        for (int k = 0; k < 3; k++) rod(g, vec3(e.c + vec2(-50.f + k * 50.f, 0.f), e.z + 1.2f), vec3(e.c + vec2(e.hx - 18.f, 0.f), e.z + 1.2f), 0.3f, 6, rgb(0.85f, 0.75f, 0.2f),
                                        M(MAT_METAL_PAINTED));
        rod(g, vec3(e.c + vec2(e.hx - 6.f, 2.f), e.z + 1.2f), vec3(e.c + vec2(e.hx + 60.f, 2.f), e.z + 1.2f), 0.35f, 6, rgb(0.85f, 0.75f, 0.2f), M(MAT_METAL_PAINTED));
        light(g, vec3(e.c + vec2(0, 0), e.z + 14.f), vec3(1.f, 0.85f, 0.6f) * 6000.f, 60.f, 1, vec3(0, 0, -1), 0.35f);
    }
}

// ------------------------------------------------------------------------------------------------ fire station
void genFireStation(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 face = e.ax, side = perp(face);
    float W = e.hx, D = e.hy, H = 9.f;
    std::vector<vec2> fp = {e.c - face * D - side * W, e.c - face * D + side * W, e.c + face * D + side * W, e.c + face * D - side * W};
    if (cross(fp[1] - fp[0], fp[2] - fp[1]) < 0) std::reverse(fp.begin(), fp.end());
    prism(g, fp, e.z - 1.f, e.z + H, rgb(0.92f, 0.9f, 0.86f), M(MAT_CONCRETE_PANEL), rgb(0.7f), M(MAT_ROOF_GRAVEL));
    collide(g, vec3(e.c, e.z + H * 0.5f), face, vec3(D, W, H * 0.5f));
    // four red apparatus doors
    for (int k = 0; k < 4; k++) {
        vec2 dc = e.c + face * (D + 0.05f) + side * (-W + 10.f + k * 13.f);
        quad(g, *g.m, vec3(dc - side * 5.f, e.z), vec3(dc + side * 5.f, e.z), vec3(dc + side * 5.f, e.z + 6.f), vec3(dc - side * 5.f, e.z + 6.f), rgb(0.75f, 0.08f, 0.06f),
             M(MAT_CORRUGATED), vec3(face, 0));
    }
    // training / lookout tower
    vec2 tc = e.c - side * (W - 5.f) - face * (D - 5.f);
    boxY(g, vec3(tc, e.z + 8.f), face, vec3(4.f, 4.f, 8.f), rgb(0.8f, 0.3f, 0.2f), M(MAT_BRICK));
    boxY(g, vec3(tc, e.z + 17.f), face, vec3(4.6f, 4.6f, 1.f), rgb(0.3f), M(MAT_METAL_PAINTED));
    lamp(g, vec3(tc, e.z + 18.5f), 0.4f, vec3(1.f, 0.1f, 0.05f), 0.9f, EA_SLOWBLINK, 40);
    // name
    if (g.detail) {
        float th = 1.6f;
        float tw = textAdvance(e.text.c_str(), th, 0.3f);
        vec2 viewR = -perp(-face);
        strokeText(g, *g.m, e.text.c_str(), vec3(e.c + face * (D + 0.06f) - viewR * (tw * 0.5f), e.z + 6.8f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.22f,
                   rgb(0.75f, 0.08f, 0.06f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
        // two crash tenders on the apron
        for (int k = 0; k < 2; k++) {
            vec2 tp = e.c + face * (D + 9.f) + side * (-W + 10.f + k * 13.f);
            boxY(g, vec3(tp, e.z + 1.9f), face, vec3(5.5f, 1.5f, 1.4f), rgb(0.9f, 0.85f, 0.15f), M(MAT_METAL_PAINTED));
            boxY(g, vec3(tp + face * 4.2f, e.z + 3.4f), face, vec3(1.2f, 1.45f, 0.35f), rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS));
            for (int a = 0; a < 3; a++)
                for (int s = -1; s <= 1; s += 2) {
                    vec3 wc(tp + face * (-3.5f + a * 3.3f) + side * (s * 1.5f), e.z + 0.6f);
                    rod(g, wc - vec3(side, 0) * 0.3f, wc + vec3(side, 0) * 0.3f, 0.6f, 8, rgb(0.05f), M(MAT_RUBBER));
                }
            collide(g, vec3(tp, e.z + 1.9f), face, vec3(5.5f, 1.5f, 1.9f));
        }
        light(g, vec3(e.c + face * (D + 2.f), e.z + 8.f), vec3(1.f, 0.9f, 0.75f) * 6000.f, 30.f, 1, vec3(0, 0, -1), 0.3f);
    }
}

// ------------------------------------------------------------------------------------------------ lighting masts, windsocks, fence
void genFloodMast(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    float H = e.h;
    cyl(g, vec3(e.c, e.z), 0.5f, 0.25f, H, g.detail ? 8 : 5, rgb(0.6f, 0.62f, 0.65f), M(MAT_METAL_PAINTED), false);
    boxY(g, vec3(e.c, e.z + H + 0.6f), vec2(1, 0), vec3(2.2f, 0.9f, 0.6f), rgb(0.35f), M(MAT_METAL_PAINTED), true);
    for (int k = -1; k <= 1; k++)
        for (int s = -1; s <= 1; s += 2)
            quad(g, *g.m, vec3(e.c + vec2(k * 1.3f - 0.5f, s * 0.9f - 0.35f), e.z + H - 0.02f), vec3(e.c + vec2(k * 1.3f + 0.5f, s * 0.9f - 0.35f), e.z + H - 0.02f),
                 vec3(e.c + vec2(k * 1.3f + 0.5f, s * 0.9f + 0.35f), e.z + H - 0.02f), vec3(e.c + vec2(k * 1.3f - 0.5f, s * 0.9f + 0.35f), e.z + H - 0.02f),
                 rgb(1.f, 0.9f, 0.75f, g.detail ? 0.6f : 1.f), emMat(EA_NIGHT), vec3(0, 0, -1));
    light(g, vec3(e.c, e.z + H - 0.5f), vec3(1.f, 0.88f, 0.7f) * 36000.f, 95.f, 1, vec3(0, 0, -1), 0.42f);
    collide(g, vec3(e.c, e.z + H * 0.5f), vec2(1, 0), vec3(0.45f, 0.45f, H * 0.5f));
}

void genWindsock(const SiteElem& e, G& g) {
    if (!g.owns(e.c) || !g.detail) return;
    float H = 6.5f;
    cyl(g, vec3(e.c, e.z), 0.08f, 0.06f, H, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
    vec2 wind = normalize(vec2(-0.92f, -0.3f));  // prevailing easterly: sock points west
    vec3 mouth(e.c, e.z + H);
    for (int k = 0; k < 5; k++) {
        float t0 = k / 5.f, t1 = (k + 1) / 5.f;
        vec3 c0 = mouth + vec3(wind * (t0 * 3.6f), -t0 * t0 * 0.6f), c1 = mouth + vec3(wind * (t1 * 3.6f), -t1 * t1 * 0.6f);
        float r0 = Lerp(0.45f, 0.2f, t0), r1 = Lerp(0.45f, 0.2f, t1);
        u32 c = (k & 1) ? rgb(0.95f, 0.95f, 0.95f) : rgb(1.f, 0.35f, 0.05f);
        vec3 ax = normalize(c1 - c0);
        vec3 n1 = normalize(anyPerp(ax)), n2 = cross(ax, n1);
        MeshData& m = *g.m;
        u32 b = (u32)m.verts.size();
        for (int s = 0; s <= 8; s++) {
            float a = kTwoPi * s / 8;
            vec3 dd = n1 * cosf(a) + n2 * sinf(a);
            m.addVertex(c0 + dd * r0 - g.org, dd, ax, vec2(0, 0), c, M(MAT_FABRIC));
            m.addVertex(c1 + dd * r1 - g.org, dd, ax, vec2(0, 0), c, M(MAT_FABRIC));
        }
        for (int s = 0; s < 8; s++) {
            u32 i0 = b + s * 2;
            m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
            m.quadIdx(i0, i0 + 1, i0 + 3, i0 + 2);
        }
    }
    lamp(g, mouth + vec3(0, 0, 0.5f), 0.25f, vec3(1.f, 0.2f, 0.1f), 0.8f);
}

void genFence(const SiteElem& e, G& g) {
    if (!g.detail) return;
    vec2 a = e.a, b = e.b;
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    vec2 ca = a, cb = b;
    if (!clipSegment(ca, cb, mn, mx)) return;
    vec2 d = normalize(b - a);
    vec2 out = perp(d);
    float H = e.h;
    u32 post = rgb(0.55f, 0.57f, 0.6f), wire = rgb(0.45f, 0.47f, 0.5f);
    float L = length(cb - ca);
    int n = Max(1, (int)(L / 4.f));
    // Dead-end turning bulbs near the line: the fence bows out around them so long vehicles U-turning in the bulb clear it
    // (a bus following a 5-6.5 m turning circle sweeps ~10.5 m from its centre; keep the fence 16 m away)
    const float kBulbClear = 16.f;
    std::vector<vec2> bulbs;
    if (gRoads)
        for (const RoadNode& nd : gRoads->nodes) {
            if (nd.edges.size() != 1) continue;
            vec2 q = nd.p;
            float t = Clamp(dot(q - a, d), 0.f, length(b - a));
            if (length(a + d * t - q) < kBulbClear) bulbs.push_back(q);
        }
    auto fp = [&](int j) {
        vec2 p = lerp(ca, cb, (float)j / n);
        for (vec2 q : bulbs) {
            float along = dot(p - q, d), lat = dot(p - q, out);
            float req = sqrtf(Max(0.f, kBulbClear * kBulbClear - along * along));
            if (fabsf(lat) < req) p += out * ((lat >= 0.f ? 1.f : -1.f) * (req - fabsf(lat)));
        }
        return p;
    };
    // runs of 4 m spans between gaps where a road (plus sidewalk) passes through the fence line
    int k = 0;
    while (k < n) {
        vec2 pm = (fp(k) + fp(k + 1)) * 0.5f;
        if (gRoads && gRoads->nearRoad(pm, 0.8f)) { k++; continue; }
        int k1 = k;
        while (k1 + 1 < n && !(gRoads && gRoads->nearRoad((fp(k1 + 1) + fp(k1 + 2)) * 0.5f, 0.8f))) k1++;
        for (int j = k; j <= k1 + 1; j++) {
            vec2 p = fp(j);
            boxY(g, vec3(p, e.z + H * 0.5f - 0.2f), d, vec3(0.04f, 0.04f, H * 0.5f + 0.2f), post, M(MAT_METAL_PAINTED));
            if ((j & 1) == 0) beam(g, vec3(p, e.z + H), vec3(p + out * 0.45f, e.z + H + 0.45f), 0.05f, 0.05f, post, M(MAT_METAL_PAINTED));
        }
        // straight pieces (merged spans) carry the chain-link wires, the barbed top and one collider each
        int j0 = k;
        while (j0 <= k1) {
            vec2 pa = fp(j0), dir = normalize(fp(j0 + 1) - pa);
            int j1 = j0;
            while (j1 + 1 <= k1 && dot(normalize(fp(j1 + 2) - fp(j1 + 1)), dir) > 0.9998f) j1++;
            vec2 pb = fp(j1 + 1), po = perp(dir);
            for (int s = 0; s < 4; s++) {
                float z = e.z + 0.15f + s * (H - 0.3f) / 3.f;
                beam(g, vec3(pa, z), vec3(pb, z), 0.03f, 0.03f, wire, M(MAT_METAL_PAINTED));
            }
            for (int s = 0; s < 3; s++) {
                float o = 0.15f + s * 0.15f;
                beam(g, vec3(pa + po * o, e.z + H + o), vec3(pb + po * o, e.z + H + o), 0.02f, 0.02f, wire, M(MAT_METAL_PAINTED));
            }
            collide(g, vec3((pa + pb) * 0.5f, e.z + H * 0.5f), dir, vec3(length(pb - pa) * 0.5f, 0.1f, H * 0.5f));
            j0 = j1 + 1;
        }
        for (int j = k; j <= k1; j++) {
            vec2 p0 = fp(j), p1 = fp(j + 1);
            beam(g, vec3(p0, e.z + 0.15f), vec3(p1, e.z + H), 0.015f, 0.015f, wire, M(MAT_METAL_PAINTED));
            beam(g, vec3(p1, e.z + 0.15f), vec3(p0, e.z + H), 0.015f, 0.015f, wire, M(MAT_METAL_PAINTED));
        }
        k = k1 + 1;
    }
}

void genApproachLights(const SiteElem& e, G& g) {
    vec2 thr = e.a, out = normalize(e.b - e.a), n = perp(out);
    int count = (int)e.p[0];
    float spacing = e.p[1];
    int cross = (int)e.p[2];
    for (int k = 0; k < count; k++) {
        vec2 p = thr + out * ((k + 1) * spacing);
        if (!g.owns(p)) continue;
        if (gRoads && gRoads->nearRoad(p, 2.f)) continue;
        if (gBuildings && gBuildings->pointInBuilding(p, 1.f)) continue;
        float ground = gMap->heightAt(p.x, p.y);
        float zl = Max(e.z + 0.8f + k * 0.05f, ground + 0.8f);
        bool isCross = k == cross;
        float half = isCross ? 15.f : 2.1f;
        if (g.detail) {
            cyl(g, vec3(p, ground - 0.3f), 0.1f, 0.08f, zl - ground + 0.2f, 6, rgb(0.9f, 0.4f, 0.1f), M(MAT_METAL_PAINTED), false);
            beam(g, vec3(p - n * half, zl), vec3(p + n * half, zl), 0.12f, 0.12f, rgb(0.9f, 0.4f, 0.1f), M(MAT_METAL_PAINTED));
            if (isCross)
                for (int s = -1; s <= 1; s += 2) cyl(g, vec3(p + n * (s * 12.f), ground - 0.3f), 0.08f, 0.06f, zl - ground + 0.2f, 5, rgb(0.9f, 0.4f, 0.1f), M(MAT_METAL_PAINTED), false);
        }
        int lamps = isCross ? 17 : 5;
        for (int l = 0; l < lamps; l++) {
            float t = lamps == 1 ? 0.f : Lerp(-half, half, (float)l / (lamps - 1));
            lamp(g, vec3(p + n * t, zl + 0.2f), 0.3f, vec3(1.f, 0.95f, 0.85f), 0.85f);
        }
        // sequenced flasher ("rabbit") running toward the threshold
        lamp(g, vec3(p, zl + 0.55f), 0.34f, vec3(0.9f, 0.95f, 1.f), 1.f, EA_RABBIT, (u32)(count - k) * 16u);
    }
}

// Security gatehouse with barrier arm
void genGatehouse(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 road = e.ax, side = perp(road);
    vec2 hc = e.c + side * 9.5f;
    boxY(g, vec3(hc, e.z + 1.5f), road, vec3(1.6f, 1.3f, 1.5f), rgb(0.92f, 0.92f, 0.9f), M(MAT_STUCCO));
    boxY(g, vec3(hc, e.z + 3.15f), road, vec3(2.0f, 1.7f, 0.15f), rgb(0.3f), M(MAT_METAL_PAINTED));
    for (int s = -1; s <= 1; s += 2)
        quad(g, *g.m, vec3(hc + side * (s * 1.31f) - road * 1.2f, e.z + 1.2f), vec3(hc + side * (s * 1.31f) + road * 1.2f, e.z + 1.2f),
             vec3(hc + side * (s * 1.31f) + road * 1.2f, e.z + 2.6f), vec3(hc + side * (s * 1.31f) - road * 1.2f, e.z + 2.6f), rgb(0.2f, 0.3f, 0.35f), M(MAT_GLASS),
             vec3(side * (float)s, 0));
    collide(g, vec3(hc, e.z + 1.6f), road, vec3(1.6f, 1.3f, 1.6f));
    if (!g.detail) return;
    // barrier arm across the inbound lane (raised)
    vec3 pivot(hc - side * 1.6f, e.z + 1.1f);
    vec3 tip = pivot + normalize(vec3(-side * 0.5f, 0.87f)) * 7.f;
    for (int k = 0; k < 7; k++) beam(g, lerp(pivot, tip, k / 7.f), lerp(pivot, tip, (k + 1) / 7.f), 0.12f, 0.12f, (k & 1) ? rgb(0.95f) : rgb(0.85f, 0.08f, 0.06f),
                                     M(MAT_METAL_PAINTED));
    light(g, vec3(hc, e.z + 3.f), vec3(1.f, 0.9f, 0.75f) * 2500.f, 16.f, 1, vec3(0, 0, -1), 0.3f);
}

// Monument sign at the airport entrance
void genAirportSign(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 face = e.ax, rt = perp(face);  // right for a viewer looking at the face
    vec3 base(e.c, e.z);
    boxY(g, base + vec3(0, 0, 1.4f), rt, vec3(14.f, 1.1f, 1.6f), rgb(0.85f, 0.8f, 0.7f), M(MAT_STONE));
    collide(g, base + vec3(0, 0, 1.4f), rt, vec3(14.f, 1.1f, 1.6f));
    const char* l1 = "PORTO SOL";
    const char* l2 = "INTERNATIONAL AIRPORT";
    float th1 = 1.3f, th2 = 0.62f;
    float w1 = textAdvance(l1, th1, 0.3f), w2 = textAdvance(l2, th2, 0.3f);
    vec3 fo = base + vec3(face * 1.12f, 0);
    strokeText(g, *g.m, l1, fo + vec3(-rt * (w1 * 0.5f), 1.4f), vec3(rt, 0), vec3(0, 0, 1), th1, 0.2f, rgb(0.9f, 0.95f, 1.f, 0.4f), emMat(EA_NIGHT),
               g.detail ? 0.08f : 0.f, 0.3f);
    strokeText(g, *g.m, l2, fo + vec3(-rt * (w2 * 0.5f), 0.45f), vec3(rt, 0), vec3(0, 0, 1), th2, 0.1f, rgb(0.9f, 0.95f, 1.f, 0.35f), emMat(EA_NIGHT), 0.f, 0.3f);
    // stylised wave sculpture: three curved blades
    for (int k = 0; k < 3; k++) {
        vec2 bc = e.c + rt * (16.f + k * 1.6f);
        for (int s = 0; s < 8; s++) {
            float t0 = s / 8.f, t1 = (s + 1) / 8.f;
            vec3 p0 = vec3(bc + face * (sinf(t0 * 3.f) * 1.5f), e.z + t0 * (7.f - k)), p1 = vec3(bc + face * (sinf(t1 * 3.f) * 1.5f), e.z + t1 * (7.f - k));
            beam(g, p0, p1, 0.35f, 0.9f, k == 1 ? rgb(1.f, 0.6f, 0.2f) : rgb(0.1f, 0.6f, 0.65f), M(MAT_METAL_PAINTED), vec3(rt, 0));
        }
    }
    light(g, base + vec3(face * 6.f, 0.5f), vec3(0.9f, 0.95f, 1.f) * 3000.f, 16.f, 1, normalize(vec3(-face, 0.4f)), 0.25f);
    if (g.detail)
        for (int s = -1; s <= 1; s += 2) prop(g, base + vec3(rt * (s * 16.5f) - face * 4.f, 0), 0.4f * s, 1.f, PROP_PALM_TALL, 2);
}

// Parked car: body, tapered glass cabin with a body-coloured roof, dark wheel band (far LOD: body block only)
void parkedCar(G& g, vec2 c, vec2 fwd, float z, u32 col, bool detail) {
    vec2 sd = perp(fwd);
    boxY(g, vec3(c, z + 0.63f), fwd, vec3(2.2f, 0.88f, 0.36f), col, M(MAT_METAL_PAINTED));
    if (!detail) return;
    boxY(g, vec3(c, z + 0.2f), fwd, vec3(1.85f, 0.8f, 0.17f), rgb(0.05f), M(MAT_RUBBER));
    // cabin frustum: bottom z+0.99 (len 2.7), top z+1.43 (len 1.55), set back toward the rear
    float zb = z + 0.99f, zt = z + 1.43f;
    vec2 cb = c - fwd * 0.2f, ct = c - fwd * 0.35f;
    vec3 b[4] = {vec3(cb - fwd * 1.35f - sd * 0.82f, zb), vec3(cb + fwd * 1.35f - sd * 0.82f, zb), vec3(cb + fwd * 1.35f + sd * 0.82f, zb),
                 vec3(cb - fwd * 1.35f + sd * 0.82f, zb)};
    vec3 t[4] = {vec3(ct - fwd * 0.78f - sd * 0.7f, zt), vec3(ct + fwd * 0.78f - sd * 0.7f, zt), vec3(ct + fwd * 0.78f + sd * 0.7f, zt),
                 vec3(ct - fwd * 0.78f + sd * 0.7f, zt)};
    u32 glass = rgb(0.07f, 0.09f, 0.11f), gm = M(MAT_GLASS);
    quad(g, *g.m, b[0], b[1], t[1], t[0], glass, gm, vec3(-sd, 0.3f));
    quad(g, *g.m, b[1], b[2], t[2], t[1], glass, gm, vec3(fwd, 0.6f));
    quad(g, *g.m, b[2], b[3], t[3], t[2], glass, gm, vec3(sd, 0.3f));
    quad(g, *g.m, b[3], b[0], t[0], t[3], glass, gm, vec3(-fwd, 0.6f));
    quad(g, *g.m, t[0], t[1], t[2], t[3], col, M(MAT_METAL_PAINTED), vec3(0, 0, 1));
}

// Surface lot: stall lines, light poles, parked cars
void genParkingMarks(const SiteElem& e, G& g) {
    float x0 = e.c.x - e.hx, x1 = e.c.x + e.hx, y0 = e.c.y - e.hy, y1 = e.c.y + e.hy;
    float depth = e.p[0], aisle = e.p[1];
    float z = e.z + 0.01f + 0.012f;
    Rng r(e.seed);
    float y = y0 + 1.f;
    int row = 0;
    while (y + depth * 2.f < y1) {
        for (int half = 0; half < 2; half++) {
            float ys = y + half * depth;
            for (float x = x0 + 2.f; x + 2.6f < x1 - 2.f; x += 2.6f) {
                vec2 lc(x, ys + depth * 0.5f);
                if (e.p[4] > e.p[3] && x > e.p[2] && lc.y > e.p[3] && lc.y < e.p[4]) continue;  // turning apron
                bool own = g.owns(lc);
                if (own && g.detail) paintRect(g, vec2(x, ys + depth * 0.5f), vec2(0, 1), depth * 0.5f, 0.06f, z, kWhiteC, matWhite());
                u32 h = hash3i((int)(x * 4.f), (int)(ys * 4.f), (int)e.seed);
                if (own && hashToFloat(h) < 0.55f) {
                    vec2 cc(x + 1.3f, ys + depth * 0.5f);
                    vec3 col = hsvToRgb(hashToFloat(h >> 4), hashToFloat(h >> 12) < 0.5f ? 0.05f : 0.6f, 0.25f + hashToFloat(h >> 16) * 0.7f);
                    float fy = half ? -1.f : 1.f;
                    parkedCar(g, cc, vec2(0, fy), e.z, rgbv(col), g.detail);
                    if (g.detail) collide(g, vec3(cc, e.z + 0.75f), vec2(0, 1), vec3(0.9f, 2.2f, 0.75f));
                }
            }
        }
        row++;
        y += depth * 2.f + aisle;
        // light poles along the aisle
        for (float x = x0 + 20.f; x < x1 - 10.f; x += 40.f) {
            vec2 lp(x, y - aisle * 0.5f);
            if (!g.owns(lp)) continue;
            if (e.p[4] > e.p[3] && x > e.p[2] - 2.f && lp.y > e.p[3] && lp.y < e.p[4]) continue;
            cyl(g, vec3(lp, e.z), 0.15f, 0.1f, 9.f, 6, rgb(0.55f), M(MAT_METAL_PAINTED), false);
            boxY(g, vec3(lp, e.z + 9.1f), vec2(1, 0), vec3(0.5f, 0.3f, 0.1f), rgb(0.3f), M(MAT_METAL_PAINTED));
            quad(g, *g.m, vec3(lp + vec2(-0.45f, -0.25f), e.z + 8.99f), vec3(lp + vec2(0.45f, -0.25f), e.z + 8.99f), vec3(lp + vec2(0.45f, 0.25f), e.z + 8.99f),
                 vec3(lp + vec2(-0.45f, 0.25f), e.z + 8.99f), rgb(1.f, 0.85f, 0.6f, 0.6f), emMat(EA_NIGHT), vec3(0, 0, -1));
            light(g, vec3(lp, e.z + 8.8f), vec3(1.f, 0.8f, 0.55f) * 8000.f, 30.f, 0, vec3(0, 0, -1), 0.2f);
            collide(g, vec3(lp, e.z + 4.5f), vec2(1, 0), vec3(0.15f, 0.15f, 4.5f));
        }
    }
}

// Garage roof deck finish and its external ramp. pts: roof min / max corners, driveway corners.
// p: ramp center x, foot y, climb direction (+-1 along y), length, half width, deck height, landing length.
void genGarageRamp(const SiteElem& e, G& g) {
    const float rx = e.p[0], yFoot = e.p[1], dir = e.p[2], len = e.p[3], hw = e.p[4], deckZ = e.p[5], land = e.p[6];
    const float z0 = e.z, slope = (deckZ - z0) / len;
    const float yTop = yFoot + dir * len, yEnd = yTop + dir * land;
    const vec2 roofMn = e.pts[0], roofMx = e.pts[1];
    const u32 conc = rgb(0.8f, 0.79f, 0.76f), cMat = M(MAT_CONCRETE);
    const float ground = z0 - 0.3f, soffit = 0.55f, bT = 0.25f, bH = 1.05f;
    const float xL = rx - hw, xR = rx + hw;
    auto yAt = [&](float s) { return yFoot + dir * s; };
    auto surf = [&](float s) { return z0 + Clamp(s, 0.f, len) * slope; };
    const float sSolid = 24.f;  // earth-filled abutment below ~3 m of rise
    const vec3 fwd(0.f, dir, 0.f);
    // ---- ramp: slab, barriers, abutment, markings (8 m segments owned by the cell holding their midpoint)
    const int nSeg = (int)ceilf(len / 8.f);
    for (int i = 0; i < nSeg; i++) {
        float s0 = len * i / nSeg, s1 = len * (i + 1) / nSeg;
        if (!g.owns(vec2(rx, yAt((s0 + s1) * 0.5f)))) continue;
        float za = surf(s0), zb = surf(s1), ya = yAt(s0), yb = yAt(s1);
        bool solid = s1 <= sSolid + 0.01f;
        float ua = solid ? ground : za - soffit, ub = solid ? ground : zb - soffit;
        vec3 upN = normalize(vec3(0.f, -dir * slope, 1.f));
        quad(g, *g.m, vec3(xL, ya, za + 0.02f), vec3(xR, ya, za + 0.02f), vec3(xR, yb, zb + 0.02f), vec3(xL, yb, zb + 0.02f), conc, cMat, upN);
        if (!solid)
            quad(g, *g.m, vec3(xL - bT, ya, ua), vec3(xR + bT, ya, ua), vec3(xR + bT, yb, ub), vec3(xL - bT, yb, ub), rgb(0.62f), cMat, -upN);
        for (int sd = -1; sd <= 1; sd += 2) {
            float xin = sd < 0 ? xL : xR, xout = xin + sd * bT;
            vec3 outN((float)sd, 0.f, 0.f);
            quad(g, *g.m, vec3(xin, ya, za), vec3(xin, yb, zb), vec3(xin, yb, zb + bH), vec3(xin, ya, za + bH), rgb(0.86f, 0.85f, 0.82f), cMat, -outN);
            quad(g, *g.m, vec3(xin, ya, za + bH), vec3(xin, yb, zb + bH), vec3(xout, yb, zb + bH), vec3(xout, ya, za + bH), rgb(0.9f), cMat, upN);
            quad(g, *g.m, vec3(xout, ya, ua), vec3(xout, yb, ub), vec3(xout, yb, zb + bH), vec3(xout, ya, za + bH), conc, cMat, outN);
            // barrier collision (stepped along the climb, outside the lane)
            collide(g, vec3(xin + sd * bT * 0.5f, (ya + yb) * 0.5f, (Min(ua, za) + zb + bH) * 0.5f), vec2(1, 0),
                    vec3(bT * 0.5f, fabsf(yb - ya) * 0.5f, (zb + bH - Min(ua, za)) * 0.5f));
        }
        if (i == 0) {
            // foot: kerb face and barrier end caps
            quad(g, *g.m, vec3(xL, ya, ground), vec3(xR, ya, ground), vec3(xR, ya, za + 0.02f), vec3(xL, ya, za + 0.02f), conc, cMat, -fwd);
            for (int sd = -1; sd <= 1; sd += 2) {
                float xin = sd < 0 ? xL : xR, xout = xin + sd * bT;
                quad(g, *g.m, vec3(xin, ya, ground), vec3(xout, ya, ground), vec3(xout, ya, za + bH), vec3(xin, ya, za + bH), rgb(0.95f, 0.8f, 0.1f), cMat, -fwd);
            }
        }
        if (solid && s1 + 8.f > sSolid + 0.01f) {
            // abutment end wall where the ramp takes off on columns
            quad(g, *g.m, vec3(xL - bT, yb, ground), vec3(xR + bT, yb, ground), vec3(xR + bT, yb, zb - soffit), vec3(xL - bT, yb, zb - soffit), rgb(0.7f), cMat, fwd);
            collide(g, vec3(rx, yb - dir * 0.3f, (ground + zb - soffit) * 0.5f), vec2(1, 0), vec3(hw + bT, 0.3f, (zb - soffit - ground) * 0.5f));
        }
        if (g.detail) {
            // dashed centerline and edge lines on the slope (decals)
            float zc = 0.035f;
            for (float t = 0.f; t < 1.f; t += 0.5f) {
                float sa = s0 + (s1 - s0) * t, sb = sa + (s1 - s0) * 0.3f;
                float yA = yAt(sa), yB = yAt(sb), zA2 = surf(sa) + zc, zB2 = surf(sb) + zc;
                quad(g, *g.d, vec3(rx - 0.07f, yA, zA2), vec3(rx + 0.07f, yA, zA2), vec3(rx + 0.07f, yB, zB2), vec3(rx - 0.07f, yB, zB2), rgb(0.95f, 0.75f, 0.1f),
                     matYellow(), upN);
            }
            for (int sd = -1; sd <= 1; sd += 2) {
                float xe = rx + sd * (hw - 0.35f);
                quad(g, *g.d, vec3(xe - 0.06f, ya, za + zc), vec3(xe + 0.06f, ya, za + zc), vec3(xe + 0.06f, yb, zb + zc), vec3(xe - 0.06f, yb, zb + zc), kPaintW,
                     matWhite(), upN);
            }
        }
    }
    // ---- columns with pier caps every 12 m beyond the abutment, plus under the landing
    for (float s = sSolid + 10.f; s < len + land; s += 12.f) {
        vec2 mid(rx, yAt(s));
        if (!g.owns(mid)) continue;
        float zs = s <= len ? surf(s) : deckZ;
        float top = zs - soffit;
        float capH = 0.6f;
        boxAA(g, vec3(xL - bT, mid.y - 0.45f, top - capH), vec3(xR + bT, mid.y + 0.45f, top), rgb(0.68f), cMat, true);
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 cp(rx + sd * (hw - 1.1f), mid.y);
            boxAA(g, vec3(cp - vec2(0.35f), ground), vec3(cp + vec2(0.35f), top - capH), rgb(0.72f), cMat);
            collide(g, vec3(cp, (ground + top) * 0.5f), vec2(1, 0), vec3(0.35f, 0.35f, (top - ground) * 0.5f));
        }
    }
    // ---- top landing: slab from the garage wall to the east barrier, barriers on the open sides
    {
        vec2 lc((roofMx.x + xR + bT) * 0.5f, (yTop + yEnd) * 0.5f);
        if (g.owns(lc)) {
            float x0 = roofMx.x, x1 = xR + bT;
            float ymn = Min(yTop, yEnd), ymx = Max(yTop, yEnd);
            boxAA(g, vec3(x0, ymn, deckZ - soffit), vec3(x1, ymx, deckZ + 0.02f), conc, cMat, true);
            // east barrier (continues the ramp's east barrier), end barrier across the landing, short barrier over the gap
            // between the garage wall and the ramp's west barrier
            vec3 eb0(xR + bT * 0.5f, (ymn + ymx) * 0.5f, deckZ + bH * 0.5f);
            boxAA(g, vec3(xR, ymn, deckZ), vec3(xR + bT, ymx, deckZ + bH), rgb(0.88f), cMat);
            collide(g, eb0, vec2(1, 0), vec3(bT * 0.5f, (ymx - ymn) * 0.5f, bH * 0.5f + 0.3f));
            float yE = yEnd - dir * bT * 0.5f;
            boxAA(g, vec3(x0, yE - bT * 0.5f, deckZ), vec3(x1, yE + bT * 0.5f, deckZ + bH), rgb(0.88f), cMat);
            collide(g, vec3((x0 + x1) * 0.5f, yE, deckZ + bH * 0.5f), vec2(1, 0), vec3((x1 - x0) * 0.5f, bT * 0.5f, bH * 0.5f + 0.3f));
            float yS = yTop + dir * bT * 0.5f;
            boxAA(g, vec3(x0, yS - bT * 0.5f, deckZ), vec3(xL - bT, yS + bT * 0.5f, deckZ + bH), rgb(0.88f), cMat);
            collide(g, vec3((x0 + xL - bT) * 0.5f, yS, deckZ + bH * 0.5f), vec2(1, 0), vec3((xL - bT - x0) * 0.5f, bT * 0.5f, bH * 0.5f + 0.3f));
            if (g.detail) {
                // painted turn arrow toward the deck
                vec2 ac(rx - 1.5f, (yTop + yEnd) * 0.5f);
                paintRect(g, ac + vec2(1.2f, 0.f), vec2(1, 0), 1.6f, 0.2f, deckZ + 0.05f, kPaintW, matWhite());
                paintLine(g, ac - vec2(0.9f, 0.f), ac + vec2(0.1f, 0.9f), 0.35f, deckZ + 0.05f, kPaintW, matWhite());
                paintLine(g, ac - vec2(0.9f, 0.f), ac + vec2(0.1f, -0.9f), 0.35f, deckZ + 0.05f, kPaintW, matWhite());
            }
        }
    }
    // ---- roof parapet (1.05 m) around the deck with the opening onto the landing, collision included
    {
        const float gy0 = Min(yTop, yEnd), gy1 = Max(yTop, yEnd);
        struct Run { vec2 a, b; vec2 in; };
        std::vector<Run> runs;
        vec2 c00 = roofMn, c11 = roofMx, c10(roofMx.x, roofMn.y), c01(roofMn.x, roofMx.y);
        runs.push_back({c00, c10, vec2(0, 1)});
        runs.push_back({c01, c11, vec2(0, -1)});
        runs.push_back({c00, c01, vec2(1, 0)});
        runs.push_back({c10, vec2(roofMx.x, gy0), vec2(-1, 0)});
        runs.push_back({vec2(roofMx.x, gy1), c11, vec2(-1, 0)});
        for (const Run& r : runs) {
            float L = length(r.b - r.a);
            if (L < 0.5f) continue;
            int n = (int)ceilf(L / 16.f);
            vec2 d = (r.b - r.a) / L;
            for (int k = 0; k < n; k++) {
                vec2 pa = r.a + d * (L * k / n), pb = r.a + d * (L * (k + 1) / n);
                vec2 mid = (pa + pb) * 0.5f + r.in * 0.15f;
                if (!g.owns(mid)) continue;
                boxY(g, vec3(mid, deckZ + bH * 0.5f - 0.02f), d, vec3(L / n * 0.5f, 0.15f, bH * 0.5f + 0.02f), rgb(0.84f, 0.83f, 0.8f), cMat);
                if (g.detail) boxY(g, vec3(mid, deckZ + bH + 0.04f), d, vec3(L / n * 0.5f, 0.2f, 0.05f), rgb(0.95f), cMat);
                collide(g, vec3(mid, deckZ + bH * 0.5f), d, vec3(L / n * 0.5f, 0.15f, bH * 0.5f + 0.3f));
            }
        }
    }
    // ---- glass stair / lift towers on the terminal side, driveway sign, ramp lamps
    for (int t = 0; t < 2; t++) {
        float ty = t == 0 ? roofMn.y + 26.f : roofMx.y - 26.f;
        vec2 tc(roofMn.x - 3.5f, ty);
        if (!g.owns(tc)) continue;
        float topZ = deckZ + 3.6f;
        boxAA(g, vec3(tc.x - 3.5f, ty - 6.f, z0), vec3(tc.x + 3.5f, ty + 6.f, topZ), rgb(0.82f), cMat);
        quad(g, *g.m, vec3(tc.x - 3.52f, ty - 5.f, z0 + 0.4f), vec3(tc.x - 3.52f, ty + 5.f, z0 + 0.4f), vec3(tc.x - 3.52f, ty + 5.f, topZ - 0.6f),
             vec3(tc.x - 3.52f, ty - 5.f, topZ - 0.6f), rgb(0.35f, 0.5f, 0.58f), M(MAT_GLASS), vec3(-1, 0, 0));
        boxAA(g, vec3(tc.x - 4.f, ty - 6.5f, topZ), vec3(tc.x + 4.f, ty + 6.5f, topZ + 0.35f), rgb(0.95f), M(MAT_METAL_PAINTED));
        collide(g, vec3(tc, (z0 + topZ) * 0.5f), vec2(1, 0), vec3(3.5f, 6.f, (topZ - z0) * 0.5f));
        // "P" cube sign on the roof (lit at night)
        vec3 sc(tc.x, ty, topZ + 2.2f);
        boxAA(g, sc - vec3(1.4f, 1.4f, 1.4f), sc + vec3(1.4f, 1.4f, 1.4f), rgb(0.1f, 0.3f, 0.75f), M(MAT_METAL_PAINTED));
        for (int f = 0; f < 4; f++) {
            vec2 fn(cosf(f * kHalfPi), sinf(f * kHalfPi)), rt = perp(fn);
            vec3 o = sc + vec3(fn * 1.42f, 0.f) - vec3(rt * 0.55f, 0.f) - vec3(0, 0, 0.9f);
            strokeText(g, *g.m, "P", o, vec3(rt, 0), vec3(0, 0, 1), 1.8f, 0.34f, rgb(1.f, 1.f, 1.f, 0.35f), emMat(EA_NIGHT), 0.f, 0.f);
        }
        light(g, vec3(tc.x - 5.f, ty, z0 + 4.f), vec3(0.95f, 0.95f, 1.f) * 2500.f, 14.f, 1, vec3(0, 0, -1), 0.3f);
    }
    {
        // entry sign at the driveway, facing the Perimeter Road (east)
        vec2 sp(931.f, yFoot - dir * 2.f);
        if (g.owns(sp)) {
            cyl(g, vec3(sp, z0), 0.12f, 0.12f, 5.2f, 8, rgb(0.5f), M(MAT_METAL_PAINTED), true);
            boxAA(g, vec3(sp.x + 0.05f, sp.y - 1.6f, z0 + 3.f), vec3(sp.x + 0.25f, sp.y + 1.6f, z0 + 5.4f), rgb(0.1f, 0.3f, 0.75f), M(MAT_METAL_PAINTED));
            vec3 o(sp.x + 0.27f, sp.y - 0.6f, z0 + 4.15f);
            strokeText(g, *g.m, "P", o, vec3(0, 1, 0), vec3(0, 0, 1), 1.05f, 0.2f, rgb(1.f, 1.f, 1.f, 0.3f), emMat(EA_NIGHT), 0.f, 0.f);
            float tw = textAdvance(e.text.c_str(), 0.42f, 0.3f);
            strokeText(g, *g.m, e.text.c_str(), vec3(sp.x + 0.27f, sp.y - tw * 0.5f, z0 + 3.35f), vec3(0, 1, 0), vec3(0, 0, 1), 0.42f, 0.07f,
                       rgb(1.f, 1.f, 1.f, 0.3f), emMat(EA_NIGHT), 0.f, 0.3f);
            collide(g, vec3(sp, z0 + 2.6f), vec2(1, 0), vec3(0.15f, 0.15f, 2.6f));
        }
        // lamps on the east barrier every 24 m (light sources on every other one)
        int li = 0;
        for (float s = 12.f; s < len; s += 24.f, li++) {
            vec2 lp(xR + bT + 0.2f, yAt(s));
            if (!g.owns(lp)) continue;
            float zs = surf(s) + bH;
            if (g.detail) {
                cyl(g, vec3(lp, zs), 0.07f, 0.06f, 3.2f, 6, rgb(0.45f), M(MAT_METAL_PAINTED), false);
                boxAA(g, vec3(lp.x - 1.2f, lp.y - 0.2f, zs + 3.1f), vec3(lp.x + 0.1f, lp.y + 0.2f, zs + 3.3f), rgb(0.4f), M(MAT_METAL_PAINTED));
            }
            quad(g, *g.m, vec3(lp.x - 1.1f, lp.y - 0.15f, zs + 3.09f), vec3(lp.x, lp.y - 0.15f, zs + 3.09f), vec3(lp.x, lp.y + 0.15f, zs + 3.09f),
                 vec3(lp.x - 1.1f, lp.y + 0.15f, zs + 3.09f), rgb(1.f, 0.88f, 0.7f, 0.6f), emMat(EA_NIGHT), vec3(0, 0, -1));
            if (li % 2 == 0) light(g, vec3(lp.x - 0.8f, lp.y, zs + 2.9f), vec3(1.f, 0.85f, 0.65f) * 3500.f, 20.f, 0, vec3(0, 0, -1), 0.2f);
        }
    }
}

void genGse(const SiteElem& e, G& g) {
    if (e.variant == 100) genJetBridge(e, g);
    else if (e.variant == 200) genGatehouse(e, g);
}

// ------------------------------------------------------------------------------------------------ landside forecourt
// Rectangular panel facing `face`, bottom at z, with text lines ('|' separated) in fg; optional arrow per line ('<' / '>'
// as the first character of a line puts an arrow before the text)
void signPanel(G& g, vec2 c, vec2 face, float z, float w, float h, u32 bg, u32 fg, const std::string& text, u32 mat, float th) {
    vec2 rt = perp(-face);
    vec3 b(c + face * 0.005f, z);
    quad(g, *g.m, b - vec3(rt * (w * 0.5f), 0.f), b + vec3(rt * (w * 0.5f), 0.f), b + vec3(rt * (w * 0.5f), h), b + vec3(-rt * (w * 0.5f), h), bg, mat,
         vec3(face, 0.f));
    std::vector<std::string> lines;
    std::string cur;
    for (char ch : text) {
        if (ch == '|') {
            lines.push_back(cur);
            cur.clear();
        } else cur += ch;
    }
    lines.push_back(cur);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string ln = lines[i];
        int arrow = 0;
        if (!ln.empty() && (ln[0] == '<' || ln[0] == '>')) {
            arrow = ln[0] == '<' ? -1 : 1;
            ln = ln.substr(1);
        }
        float tz = z + h - (i + 1) * th * 1.55f;
        float x0 = -w * 0.5f + 0.12f + (arrow ? th * 1.3f : 0.f);
        float tw = textAdvance(ln.c_str(), th, 0.3f), avail = w * 0.5f - 0.12f - x0;
        float sc = tw > avail ? avail / tw : 1.f;
        strokeText(g, *g.m, ln.c_str(), vec3(c + face * 0.012f + rt * x0, tz), vec3(rt, 0.f), vec3(0, 0, 1), th * sc, th * sc * 0.14f, fg, mat, 0.f, 0.3f);
        if (arrow) {
            vec2 ac = c + rt * (-w * 0.5f + 0.12f + th * 0.55f) + face * 0.012f;
            float s = th * 0.45f;
            vec3 tip(ac + rt * (s * (float)arrow), tz + th * 0.5f), tail(ac - rt * (s * (float)arrow), tz + th * 0.5f);
            beam(g, tail, tip, th * 0.16f, 0.004f, fg, mat, vec3(face, 0.f));
            beam(g, tip, tip - vec3(rt * (s * 0.7f * arrow), s * 0.6f), th * 0.16f, 0.004f, fg, mat, vec3(face, 0.f));
            beam(g, tip, tip - vec3(rt * (s * 0.7f * arrow), -s * 0.6f), th * 0.16f, 0.004f, fg, mat, vec3(face, 0.f));
        }
    }
}

// Forecourt dressing. variant 0: bollards along a..b every p[0] m; 1: taxi rank (shelter, lit TAXI totem, queue
// stanchions); 2: luggage trolley corral with loose trolleys; 3: wayfinding pylon (e.text); 4: flag poles along a..b;
// 5: bench, planter and bin group; 6: overhead sign gantry across the drive (posts at a and b, e.text)
void genForecourt(const SiteElem& e, G& g) {
    if (!g.detail) return;
    u32 paint = M(MAT_METAL_PAINTED), brushed = M(MAT_METAL_BRUSHED);
    switch (e.variant) {
        case 0: {
            vec2 d = normalize(e.b - e.a);
            float L = length(e.b - e.a), step = Max(e.p[0], 1.f);
            for (float s = 0.f; s <= L + 0.01f; s += step) {
                vec2 p = e.a + d * s;
                if (g.owns(p)) prop(g, vec3(p, e.z), 0.f, 1.f, PROP_BOLLARD);
            }
            break;
        }
        case 1: {
            if (!g.owns(e.c)) return;
            vec2 face = e.ax, rt = perp(-face);
            // shelter (the street shelter prototype: frame, glass back wall, bench, ad light box)
            prop(g, vec3(e.c, e.z), atan2f(face.x, -face.y), 1.f, PROP_BUS_STOP, (u8)(e.seed & 3u));
            // TAXI totem at the kerb end of the rank, lit box on a pole
            vec2 tp = e.c + face * 2.4f - rt * 4.2f;
            cyl(g, vec3(tp, e.z), 0.07f, 0.06f, 3.2f, 8, rgb(0.25f), paint, false);
            boxY(g, vec3(tp, e.z + 3.4f), face, vec3(0.12f, 0.55f, 0.3f), rgb(0.12f), paint, true);
            for (int s = -1; s <= 1; s += 2) {
                vec2 fc = tp + face * (s * 0.125f);
                vec2 r2 = perp(-face * (float)s);
                quad(g, *g.m, vec3(fc - r2 * 0.5f, e.z + 3.14f), vec3(fc + r2 * 0.5f, e.z + 3.14f), vec3(fc + r2 * 0.5f, e.z + 3.66f), vec3(fc - r2 * 0.5f, e.z + 3.66f),
                     rgb(1.f, 0.82f, 0.1f, 0.25f), emMat(), vec3(face * (float)s, 0.f));
                float th = 0.3f, tw = textAdvance("TAXI", th, 0.3f);
                strokeText(g, *g.m, "TAXI", vec3(fc + face * (s * 0.006f) - r2 * (tw * 0.5f), e.z + 3.25f), vec3(r2, 0.f), vec3(0, 0, 1), th, 0.05f, rgb(0.05f), paint,
                           0.f, 0.3f);
            }
            collide(g, vec3(tp, e.z + 1.6f), face, vec3(0.1f, 0.1f, 1.6f));
            // queue stanchions with belts along the rank
            for (int k = 0; k < 6; k++) {
                vec2 p = e.c + face * 2.1f + rt * (-1.6f + k * 1.4f);
                cyl(g, vec3(p, e.z), 0.035f, 0.035f, 0.95f, 6, rgb(0.75f), brushed, true);
                cyl(g, vec3(p, e.z), 0.16f, 0.16f, 0.03f, 8, rgb(0.2f), paint, true);
                if (k < 5) beam(g, vec3(p, e.z + 0.9f), vec3(p + rt * 1.4f, e.z + 0.9f), 0.05f, 0.012f, rgb(0.1f, 0.2f, 0.5f), M(MAT_FABRIC), vec3(face, 0.f));
            }
            break;
        }
        case 2: {
            if (!g.owns(e.c)) return;
            vec2 X = e.ax, Y = perp(e.ax);
            Rng r(e.seed);
            // corral rails
            for (int s = -1; s <= 1; s += 2) {
                beam(g, vec3(e.c + Y * (s * 0.55f) - X * 3.2f, e.z + 0.95f), vec3(e.c + Y * (s * 0.55f) + X * 3.2f, e.z + 0.95f), 0.05f, 0.05f, rgb(0.8f), brushed);
                for (int k = 0; k < 3; k++) {
                    vec2 p = e.c + Y * (s * 0.55f) + X * (-3.2f + k * 3.2f);
                    cyl(g, vec3(p, e.z), 0.04f, 0.04f, 0.98f, 6, rgb(0.8f), brushed, false);
                }
                collide(g, vec3(e.c + Y * (s * 0.55f), e.z + 0.5f), X, vec3(3.2f, 0.05f, 0.5f));
            }
            // luggage trolley: wire basket frame, handle, low wheels; nested in a row in the corral, a few left loose
            auto trolley = [&](vec2 p, vec2 f, float lift) {
                vec2 s2 = perp(f);
                u32 fr = rgb(0.72f), dark = rgb(0.08f);
                vec3 base(p, e.z + lift);
                for (int s = -1; s <= 1; s += 2) {
                    beam(g, base + vec3(s2 * (s * 0.28f) - f * 0.45f, 0.18f), base + vec3(s2 * (s * 0.28f) + f * 0.5f, 0.18f), 0.03f, 0.03f, fr, brushed);
                    beam(g, base + vec3(s2 * (s * 0.28f) - f * 0.45f, 0.18f), base + vec3(s2 * (s * 0.28f) - f * 0.55f, 1.05f), 0.03f, 0.03f, fr, brushed);
                    for (float x : {-0.35f, 0.4f}) cyl(g, base + vec3(s2 * (s * 0.24f) + f * x, 0.f), 0.07f, 0.07f, 0.05f, 6, dark, M(MAT_RUBBER), true);
                }
                beam(g, base + vec3(-s2 * 0.3f - f * 0.55f, 1.05f), base + vec3(s2 * 0.3f - f * 0.55f, 1.05f), 0.035f, 0.035f, rgb(0.85f, 0.15f, 0.1f), paint);
                quad(g, *g.m, base + vec3(-s2 * 0.28f - f * 0.45f, 0.2f), base + vec3(s2 * 0.28f - f * 0.45f, 0.2f), base + vec3(s2 * 0.28f + f * 0.5f, 0.2f),
                     base + vec3(-s2 * 0.28f + f * 0.5f, 0.2f), rgb(0.55f), brushed, vec3(0, 0, 1));
                quad(g, *g.m, base + vec3(-s2 * 0.28f - f * 0.47f, 0.2f), base + vec3(s2 * 0.28f - f * 0.47f, 0.2f), base + vec3(s2 * 0.28f - f * 0.5f, 0.75f),
                     base + vec3(-s2 * 0.28f - f * 0.5f, 0.75f), rgb(0.6f), brushed, vec3(-f, 0.2f));
            };
            int nested = 6 + (int)(e.seed % 5u);
            for (int k = 0; k < nested; k++) trolley(e.c - X * 2.6f + X * (k * 0.42f), X, 0.f);
            for (int k = 0; k < 3; k++) {
                float a = r.f() * kTwoPi;
                vec2 p = e.c + X * r.range(-6.f, 6.f) + Y * r.range(1.4f, 3.2f) * (r.chance(0.5f) ? 1.f : -1.f);
                trolley(p, vec2(cosf(a), sinf(a)), 0.f);
            }
            break;
        }
        case 3: {
            if (!g.owns(e.c)) return;
            vec2 face = e.ax;
            float w = e.hx * 2.f, h = e.h;
            boxY(g, vec3(e.c, e.z + h * 0.5f), face, vec3(0.12f, w * 0.5f + 0.06f, h * 0.5f), rgb(0.2f, 0.22f, 0.25f), paint, false);
            for (int s = -1; s <= 1; s += 2)
                signPanel(g, e.c + face * (s * 0.125f), face * (float)s, e.z + h - 1.6f, w, 1.5f, rgb(0.08f, 0.2f, 0.42f), rgb(0.95f), e.text, paint, 0.2f);
            quad(g, *g.m, vec3(e.c + face * 0.13f - perp(-face) * (w * 0.5f), e.z + h - 0.12f), vec3(e.c + face * 0.13f + perp(-face) * (w * 0.5f), e.z + h - 0.12f),
                 vec3(e.c + face * 0.13f + perp(-face) * (w * 0.5f), e.z + h - 0.02f), vec3(e.c + face * 0.13f - perp(-face) * (w * 0.5f), e.z + h - 0.02f),
                 rgb(1.f, 0.8f, 0.2f), paint, vec3(face, 0.f));
            collide(g, vec3(e.c, e.z + h * 0.5f), face, vec3(0.12f, w * 0.5f, h * 0.5f));
            break;
        }
        case 4: {
            vec2 d = normalize(e.b - e.a);
            int n = Max(2, (int)e.p[0]);
            const vec3 pal[] = {vec3(0.1f, 0.5f, 0.55f), vec3(0.95f), vec3(0.95f, 0.6f, 0.15f), vec3(0.15f, 0.25f, 0.55f), vec3(0.85f, 0.2f, 0.25f),
                                vec3(0.2f, 0.55f, 0.3f), vec3(0.98f, 0.85f, 0.2f)};
            for (int k = 0; k < n; k++) {
                vec2 p = lerp(e.a, e.b, (float)k / (n - 1));
                if (!g.owns(p)) continue;
                float H = 10.f;
                cyl(g, vec3(p, e.z), 0.09f, 0.05f, H, 8, rgb(0.85f), brushed, false);
                cyl(g, vec3(p, e.z), 0.2f, 0.2f, 0.3f, 8, rgb(0.6f), M(MAT_CONCRETE), true);
                boxY(g, vec3(p, e.z + H + 0.06f), vec2(1, 0), vec3(0.07f), rgb(0.85f, 0.75f, 0.4f), M(MAT_CHROME), true);
                // flag waving slightly (three panels): a hoist band, the field and a sun disc (the airport and Palmera colours)
                u32 h = hash32(e.seed * 31u + (u32)k);
                vec3 field = pal[h % 7u], band = pal[(h >> 5) % 7u];
                if (band == field) band = vec3(0.95f);
                float fw = 2.2f, fh = 1.4f;
                vec2 fd = normalize(vec2(-0.9f, 0.35f));   // the sea breeze from the east-south-east
                vec2 fside = perp(fd);
                float zt = e.z + H - 0.15f;
                float wv[4];
                for (int j = 0; j < 4; j++) wv[j] = sinf(j * 1.4f + (float)k) * 0.12f;
                for (int seg = 0; seg < 3; seg++) {
                    float u0 = fw * seg / 3.f, u1 = fw * (seg + 1) / 3.f;
                    panel2(g, vec3(p + fd * u0 + fside * wv[seg], zt - fh), vec3(p + fd * u1 + fside * wv[seg + 1], zt - fh),
                           vec3(p + fd * u1 + fside * wv[seg + 1], zt), vec3(p + fd * u0 + fside * wv[seg], zt), rgbv(seg == 0 ? band : field), M(MAT_FABRIC));
                }
                {
                    // sun disc on the middle panel, both faces
                    vec3 a0(p + fd * (fw / 3.f) + fside * wv[1], 0.f), a1(p + fd * (fw * 2.f / 3.f) + fside * wv[2], 0.f);
                    vec3 c = (a0 + a1) * 0.5f + vec3(0, 0, zt - fh * 0.5f);
                    vec3 ux = normalize(a1 - a0), nz = normalize(cross(ux, vec3(0, 0, 1)));
                    MeshData& m = *g.m;
                    u32 sunC = rgb(0.98f, 0.92f, 0.6f);
                    for (int side = -1; side <= 1; side += 2) {
                        vec3 n = nz * (float)side;
                        u32 b0 = m.addVertex(c + n * 0.004f - g.org, n, ux, vec2(0, 0), sunC, M(MAT_FABRIC));
                        for (int q = 0; q <= 10; q++) {
                            float an = kTwoPi * q / 10.f;
                            m.addVertex(c + n * 0.004f + ux * (cosf(an) * 0.3f) + vec3(0, 0, sinf(an) * 0.3f) - g.org, n, ux, vec2(0, 0), sunC, M(MAT_FABRIC));
                        }
                        for (int q = 0; q < 10; q++) {
                            vec3 fn = cross(m.verts[b0 + 1 + q].pos - m.verts[b0].pos, m.verts[b0 + 2 + q].pos - m.verts[b0].pos);
                            if (dot(fn, n) > 0.f) m.tri(b0, b0 + 1 + q, b0 + 2 + q);
                            else m.tri(b0, b0 + 2 + q, b0 + 1 + q);
                        }
                    }
                }
                collide(g, vec3(p, e.z + H * 0.5f), d, vec3(0.1f, 0.1f, H * 0.5f));
            }
            break;
        }
        case 5: {
            if (!g.owns(e.c)) return;
            vec2 X = e.ax;
            float yaw = atan2f(X.y, X.x);
            prop(g, vec3(e.c - X * 1.3f, e.z), yaw, 1.f, PROP_BENCH);
            prop(g, vec3(e.c + X * 1.3f, e.z), yaw, 1.f, PROP_BENCH);
            prop(g, vec3(e.c + X * 3.4f, e.z), yaw, 1.f, PROP_PLANTER, (u8)(e.seed & 1u));
            prop(g, vec3(e.c - X * 3.2f, e.z), yaw, 1.f, PROP_BIN);
            break;
        }
        default: {
            // overhead gantry: posts at a and b, a truss beam, a sign panel over the lanes facing the traffic (ax)
            vec2 d = normalize(e.b - e.a);
            vec2 mid = (e.a + e.b) * 0.5f;
            if (!g.owns(mid)) return;
            float H = 6.2f, L = length(e.b - e.a);
            for (vec2 p : {e.a, e.b}) {
                cyl(g, vec3(p, e.z), 0.22f, 0.2f, H + 0.9f, 10, rgb(0.55f, 0.57f, 0.6f), paint, true);
                collide(g, vec3(p, e.z + (H + 0.9f) * 0.5f), d, vec3(0.22f, 0.22f, (H + 0.9f) * 0.5f));
            }
            for (float z : {H + 0.2f, H + 0.85f}) beam(g, vec3(e.a, e.z + z), vec3(e.b, e.z + z), 0.14f, 0.14f, rgb(0.55f, 0.57f, 0.6f), paint);
            for (int k = 0; k < 8; k++) {
                float s0 = L * k / 8.f, s1 = L * (k + 1) / 8.f;
                beam(g, vec3(e.a + d * s0, e.z + H + 0.2f), vec3(e.a + d * s1, e.z + H + 0.85f), 0.06f, 0.06f, rgb(0.55f, 0.57f, 0.6f), paint);
            }
            float w = Min(L - 2.f, e.p[0] > 0.f ? e.p[0] : 8.f);
            signPanel(g, mid + e.ax * 0.2f, e.ax, e.z + H - 1.1f, w, 1.9f, rgb(0.07f, 0.35f, 0.2f), rgb(0.97f), e.text, paint, 0.34f);
            quad(g, *g.m, vec3(mid + e.ax * 0.18f - perp(-e.ax) * (w * 0.5f), e.z + H - 1.1f), vec3(mid + e.ax * 0.18f + perp(-e.ax) * (w * 0.5f), e.z + H - 1.1f),
                 vec3(mid + e.ax * 0.18f + perp(-e.ax) * (w * 0.5f), e.z + H + 0.8f), vec3(mid + e.ax * 0.18f - perp(-e.ax) * (w * 0.5f), e.z + H + 0.8f),
                 rgb(0.4f), paint, vec3(-e.ax, 0.f));
            light(g, vec3(mid + e.ax * 1.2f, e.z + H - 1.3f), vec3(1.f, 0.95f, 0.9f) * 1500.f, 8.f, 1, normalize(vec3(-e.ax, 0.8f)), 0.3f);
            break;
        }
    }
}

}  // namespace airport_mesh
}  // namespace World
