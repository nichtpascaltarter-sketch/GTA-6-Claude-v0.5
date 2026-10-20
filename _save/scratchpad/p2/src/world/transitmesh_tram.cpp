// Sol Beach Streetcar geometry, built per streaming cell through the site system (included by transitmesh.cpp):
// embedded grooved rails in a concrete track slab along the curb lane, the overhead contact wire with its curbside poles,
// bracket arms, droppers and the corner poles' pull-offs on the curves; stops with a glass canopy shelter (bench, line
// map, stop display), ticket machine, stop totem and a tactile boarding strip along the curb. Pieces are emitted by the
// cell that owns their midpoint / foot, so chunks spanning several cells never double up.

namespace World {
namespace transit_mesh {

const vec3 kTramCoral(0.95f, 0.36f, 0.26f);
const vec3 kTramCoralGlow(1.0f, 0.55f, 0.4f);

const TramLine* tramLine(int idx) {
    const TransitNet& N = *gTransit;
    if (!N.ready || idx < 0 || idx >= (int)N.trams.size()) return nullptr;
    return &N.trams[idx];
}

// right of travel at track position s
vec2 tramRight(const TramLine& T, float s) {
    vec2 d = T.dirAt(s);
    return vec2(d.y, -d.x);
}

// ---------------------------------------------------------------------------------------------------------------- track
void genTramTrack(const SiteElem& e, G& g) {
    const TramLine* TP = tramLine(e.variant);
    if (!TP) return;
    const TramLine& T = *TP;
    MeshData& m = *g.m;
    const float s0 = e.p[0], s1 = e.p[1];
    const float half = tram_dims::kGauge * 0.5f;
    // ---- slab and rails (sample to sample; each segment belongs to the cell holding its midpoint)
    float step = g.detail ? T.ds : T.ds * 4.f;
    int nSeg = Max(1, (int)ceilf((s1 - s0) / step));
    float h = (s1 - s0) / nSeg;
    u32 slabCol = rgb(0.5f, 0.49f, 0.47f), slabMat = M(MAT_CONCRETE);
    u32 railCol = rgb(0.68f, 0.68f, 0.7f), railMat = M(MAT_METAL_BRUSHED);
    u32 grooveCol = rgb(0.05f, 0.05f, 0.05f), grooveMat = M(MAT_ASPHALT);
    u32 jointCol = rgb(0.2f, 0.2f, 0.2f), jointMat = M(MAT_ASPHALT);
    for (int k = 0; k < nSeg; k++) {
        float sa = s0 + k * h, sb = sa + h;
        vec3 a = T.at(sa), b = T.at(sb);
        if (!g.owns((a.xy() + b.xy()) * 0.5f)) continue;
        vec2 ra = tramRight(T, sa), rb = tramRight(T, sb);
        auto P = [&](const vec3& c, vec2 r, float lat, float dz) { return vec3(c.xy() + r * lat, c.z + dz) - g.org; };
        // concrete track slab (decal, flush with the road)
        const float W = 1.28f;
        g.d->quadFacing(P(a, ra, -W, 0.f), P(b, rb, -W, 0.f), P(b, rb, W, 0.f), P(a, ra, W, 0.f), vec2(sa, 0.f), vec2(sb, 0.f), vec2(sb, 2.f * W),
                        vec2(sa, 2.f * W), slabCol, slabMat, vec3(0, 0, 1));
        if (!g.detail) continue;
        // slab joints every 6 m
        if (floorf(sa / 6.f) != floorf(sb / 6.f)) {
            float sj = floorf(sb / 6.f) * 6.f;
            vec3 c = T.at(sj);
            vec2 r = tramRight(T, sj);
            paintRect(g, c.xy(), r, W - 0.05f, 0.012f, c.z + 0.002f, jointCol, jointMat);
        }
        for (int sd = -1; sd <= 1; sd += 2) {
            float in = sd * (half - 0.036f), out = sd * (half + 0.036f), groove = sd * (half - 0.036f - 0.024f);
            // rail head: top and both flanks, 12 mm proud of the slab
            float lo = sd < 0 ? out : in, hi = sd < 0 ? in : out;
            m.quadFacing(P(a, ra, lo, 0.012f), P(b, rb, lo, 0.012f), P(b, rb, hi, 0.012f), P(a, ra, hi, 0.012f), vec2(sa, 0.f), vec2(sb, 0.f), vec2(sb, 0.07f),
                         vec2(sa, 0.07f), railCol, railMat, vec3(0, 0, 1));
            vec3 nIn(-ra * (float)sd, 0.f), nOut(ra * (float)sd, 0.f);
            m.quadFacing(P(a, ra, in, 0.f), P(b, rb, in, 0.f), P(b, rb, in, 0.012f), P(a, ra, in, 0.012f), vec2(sa, 0.f), vec2(sb, 0.f), vec2(sb, 0.012f),
                         vec2(sa, 0.012f), railCol, railMat, nIn);
            m.quadFacing(P(a, ra, out, 0.f), P(b, rb, out, 0.f), P(b, rb, out, 0.012f), P(a, ra, out, 0.012f), vec2(sa, 0.f), vec2(sb, 0.f), vec2(sb, 0.012f),
                         vec2(sa, 0.012f), railCol, railMat, nOut);
            // flangeway groove on the gauge side
            float g0 = groove - 0.02f, g1 = groove + 0.02f;
            g.d->quadFacing(P(a, ra, g0, 0.003f), P(b, rb, g0, 0.003f), P(b, rb, g1, 0.003f), P(a, ra, g1, 0.003f), vec2(sa, 0.f), vec2(sb, 0.f), vec2(sb, 0.04f),
                            vec2(sa, 0.04f), grooveCol, grooveMat, vec3(0, 0, 1));
        }
    }
    // ---- contact wire: spans between support points starting in this chunk
    u32 wireCol = rgb(0.22f, 0.17f, 0.13f), wireMat = M(MAT_METAL_BRUSHED);
    const float H = tram_dims::kWireHeight;
    int nw = (int)T.wire.size();
    for (int k = 0; k < nw && g.detail; k++) {
        float sa = T.wire[k];
        if (sa < s0 || sa >= s1) continue;
        float sb = k + 1 < nw ? T.wire[k + 1] : T.wire[0] + T.length;
        if (sb - sa > 70.f) continue;   // no support for a long way: leave the gap rather than cut across
        vec3 a = T.at(sa) + vec3(0, 0, H), b = T.at(sb) + vec3(0, 0, H);
        if (!g.owns((a.xy() + b.xy()) * 0.5f)) continue;
        beam(g, a, b, 0.014f, 0.014f, wireCol, wireMat);
    }
    // ---- poles whose first hold lies in this chunk
    u32 poleCol = rgb(0.13f, 0.17f, 0.15f), poleMat = M(MAT_METAL_PAINTED);
    u32 insCol = rgb(0.45f, 0.25f, 0.15f), insMat = M(MAT_PLASTIC);
    for (const TramPole& Pl : T.poles) {
        if (Pl.holds.empty() || Pl.holds[0] < s0 || Pl.holds[0] >= s1 || !g.owns(Pl.pos)) continue;
        bool corner = Pl.holds.size() > 1;
        float railZ = T.at(Pl.holds[0]).z;
        float top = railZ + H + (corner ? 1.1f : 0.85f);
        float hgt = top - Pl.z;
        vec3 foot(Pl.pos, Pl.z);
        cyl(g, foot, 0.14f, 0.1f, 0.35f, g.detail ? 10 : 6, poleCol, poleMat);
        cyl(g, foot, 0.115f, 0.075f, hgt, g.detail ? 10 : 5, poleCol, poleMat);
        if (!g.detail) continue;
        cyl(g, foot + vec3(0, 0, hgt), 0.085f, 0.02f, 0.14f, 8, poleCol, poleMat);
        collide(g, foot + vec3(0, 0, 1.5f), vec2(1, 0), vec3(0.13f, 0.13f, 1.5f));
        if (!corner) {
            // bracket arm over the track, a stay below it, a dropper to the wire
            vec3 hold = T.at(Pl.holds[0]) + vec3(0, 0, H);
            vec2 dir = hold.xy() - Pl.pos;
            float reach = length(dir);
            if (reach < 0.5f) continue;
            dir = dir / reach;
            float armZ = hold.z + 0.72f;
            vec3 root(Pl.pos + dir * 0.1f, armZ), tip(hold.xy() + dir * 0.35f, armZ);
            rod(g, root, tip, 0.035f, 6, poleCol, poleMat);
            rod(g, vec3(Pl.pos + dir * 0.1f, armZ - 0.95f), vec3(Pl.pos + dir * (reach * 0.62f), armZ), 0.022f, 5, poleCol, poleMat);
            cyl(g, vec3(Pl.pos + dir * 0.45f, armZ - 0.09f), 0.055f, 0.055f, 0.18f, 6, insCol, insMat);
            rod(g, vec3(hold.xy(), armZ), hold + vec3(0, 0, 0.05f), 0.01f, 4, wireCol, wireMat);
            boxY(g, hold + vec3(0, 0, 0.03f), T.dirAt(Pl.holds[0]), vec3(0.09f, 0.02f, 0.03f), wireCol, wireMat, true);
        } else {
            // corner pole: pull-off wires to the points along the curve
            vec3 head = foot + vec3(0, 0, hgt - 0.35f);
            for (float s : Pl.holds) {
                vec3 hold = T.at(s) + vec3(0, 0, H);
                rod(g, head, hold + vec3(0, 0, 0.02f), 0.008f, 4, wireCol, wireMat);
                cyl(g, lerp(head, hold, 0.12f) - vec3(0, 0, 0.09f), 0.045f, 0.045f, 0.18f, 6, insCol, insMat);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------- stops
// Line map on the shelter's end panel: the loop turned on its side to fill the panel, stops as dots, this one in red
void tramMapPanel(G& g, const TramLine& T, int self, vec3 c, vec3 ax, vec3 ay, vec3 nrm, float hw, float hh) {
    MeshData& m = *g.m;
    m.quadFacing(c - ax * hw - ay * hh - g.org, c + ax * hw - ay * hh - g.org, c + ax * hw + ay * hh - g.org, c - ax * hw + ay * hh - g.org, vec2(0), vec2(1, 0),
                 vec2(1, 1), vec2(0, 1), rgb(0.95f, 0.95f, 0.93f, 0.08f), emMat(), nrm);
    vec2 mn(1e9f), mx(-1e9f);
    for (size_t i = 0; i < T.p.size(); i += 16) {
        mn = vmin(mn, T.p[i].xy());
        mx = vmax(mx, T.p[i].xy());
    }
    // the loop's long axis goes across the panel
    bool tall = (mx.y - mn.y) > (mx.x - mn.x);
    vec2 ext = tall ? vec2(mx.y - mn.y, mx.x - mn.x) : vec2(mx.x - mn.x, mx.y - mn.y);
    float sc = Min((hw * 1.7f) / Max(ext.x, 1.f), (hh * 1.1f) / Max(ext.y, 1.f));
    vec2 mc = (mn + mx) * 0.5f;
    auto mapPt = [&](vec2 w, float lift) {
        vec2 q = w - mc;
        if (tall) q = vec2(-q.y, q.x);   // north to the left
        q = q * sc;
        return c + ax * q.x + ay * (q.y - hh * 0.12f) + nrm * lift;
    };
    u32 lineCol = rgbv(kTramCoral * 1.2f, 0.3f);
    int n = (int)T.p.size();
    for (int i = 0; i < n; i += 12) {
        int j = (i + 12) % n;
        vec3 a = mapPt(T.p[i].xy(), 0.004f), b = mapPt(T.p[j].xy(), 0.004f);
        vec3 dd = b - a;
        if (length2(dd) < 1e-8f) continue;
        vec3 side = cross(nrm, normalize(dd)) * 0.014f;
        m.quadFacing(a - side - g.org, b - side - g.org, b + side - g.org, a + side - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), lineCol, emMat(), nrm);
    }
    for (int i = 0; i < (int)T.stops.size(); i++) {
        vec3 p = mapPt(T.at(T.stops[i].s - tram_dims::kLength * 0.5f).xy(), 0.006f);
        float r = i == self ? 0.04f : 0.022f;
        u32 dc = i == self ? rgb(1.f, 0.12f, 0.08f, 0.5f) : rgb(1.f, 1.f, 1.f, 0.3f);
        m.quadFacing(p - ax * r - ay * r - g.org, p + ax * r - ay * r - g.org, p + ax * r + ay * r - g.org, p - ax * r + ay * r - g.org, vec2(0), vec2(1, 0), vec2(1, 1),
                     vec2(0, 1), dc, emMat(), nrm);
    }
    strokeText(g, m, "SOL BEACH STREETCAR", c - ax * (hw - 0.06f) + ay * (hh - 0.12f) + nrm * 0.004f, ax, ay, 0.07f, 0.011f, rgbv(kTramCoral), M(MAT_PAINT_WHITE));
    std::string nm = upper(T.stops[self].name);
    float th = Min(0.05f, (hw * 1.9f) / Max(textAdvance(nm.c_str(), 1.f), 0.1f));
    strokeText(g, m, nm.c_str(), c - ax * (hw - 0.06f) - ay * (hh - 0.07f) + nrm * 0.004f, ax, ay, th, th * 0.16f, rgb(0.08f, 0.1f, 0.12f), M(MAT_PAINT_WHITE));
}

void genTramStop(const SiteElem& e, G& g) {
    const TramLine* TP = tramLine(e.variant / 256);
    if (!TP) return;
    const TramLine& T = *TP;
    int si = e.variant % 256;
    if (si >= (int)T.stops.size()) return;
    const TramStop& st = T.stops[si];
    MeshData& m = *g.m;
    vec3 al(st.along, 0.f), fc(st.face, 0.f), up(0, 0, 1);   // travel direction, toward the curb, up
    u32 steel = rgb(0.2f, 0.22f, 0.23f), mSteel = M(MAT_METAL_PAINTED), white = rgb(0.93f, 0.93f, 0.91f);
    // ---- tactile boarding strip along the curb (the length of a streetcar)
    {
        float sA = st.s - tram_dims::kLength - 0.8f, sB = st.s + 0.6f;
        int n = Max(1, (int)ceilf((sB - sA) / 2.f));
        for (int k = 0; k < n; k++) {
            float a = sA + (sB - sA) * k / n, b = sA + (sB - sA) * (k + 1) / n;
            vec2 pa = T.at(a).xy() + tramRight(T, a) * (st.curbLat + 0.38f), pb = T.at(b).xy() + tramRight(T, b) * (st.curbLat + 0.38f);
            if (!g.owns((pa + pb) * 0.5f)) continue;
            paintLine(g, pa, pb, 0.42f, st.z + 0.004f, rgb(0.95f, 0.78f, 0.12f), M(MAT_PAINT_YELLOW));
        }
    }
    // ---- stop totem at the front of the platform (where the driver stops the front door)
    vec2 totem = T.at(st.s).xy() + tramRight(T, st.s) * (st.curbLat + 0.75f);
    if (g.owns(totem)) {
        vec3 b(totem, st.z);
        boxY(g, b + vec3(0, 0, 1.3f), st.along, vec3(0.16f, 0.09f, 1.3f), steel, mSteel, true);
        boxY(g, b + vec3(0, 0, 2.95f), st.along, vec3(0.3f, 0.1f, 0.35f), rgbv(kTramCoral), mSteel, true);
        collide(g, b + vec3(0, 0, 1.6f), st.along, vec3(0.3f, 0.1f, 1.6f));
        if (g.detail) {
            for (int fs = -1; fs <= 1; fs += 2) {
                vec3 nrm = fc * (float)fs;
                vec3 right = normalize(cross(up, nrm));
                vec3 face = b + vec3(0, 0, 2.95f) + nrm * 0.102f;
                // white roundel with the streetcar "S"
                float r = 0.2f;
                m.quadFacing(face - right * r - up * r + up * 0.08f - g.org, face + right * r - up * r + up * 0.08f - g.org, face + right * r + up * r + up * 0.08f - g.org,
                             face - right * r + up * r + up * 0.08f - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(1.f, 1.f, 1.f, 0.2f), emMat(EA_NIGHT), nrm);
                float th = 0.26f;
                strokeText(g, m, "S", face + up * 0.08f - right * (textAdvance("S", th) * 0.5f) - up * (th * 0.5f) + nrm * 0.003f, right, up, th, th * 0.18f,
                           rgbv(kTramCoral), M(MAT_PAINT_WHITE));
                float t2 = 0.052f;
                strokeText(g, m, "STREETCAR", face - up * 0.26f - right * (textAdvance("STREETCAR", t2) * 0.5f) + nrm * 0.003f, right, up, t2, t2 * 0.18f,
                           rgb(1.f, 1.f, 1.f, 0.15f), emMat(EA_NIGHT));
                // stop name on the post
                std::string nm = upper(st.name);
                float t3 = Min(0.045f, 0.3f / Max(textAdvance(nm.c_str(), 1.f), 0.1f));
                vec3 pf = b + vec3(0, 0, 2.1f) + nrm * 0.092f;
                strokeText(g, m, nm.c_str(), pf - right * (textAdvance(nm.c_str(), t3) * 0.5f) + nrm * 0.002f, right, up, t3, t3 * 0.16f, white, M(MAT_PAINT_WHITE));
            }
        }
    }
    // ---- shelter
    if (!g.owns(st.pos)) return;
    vec3 c(st.pos, st.z);
    const float L = 3.0f, D = 0.8f, Hr = 2.62f;     // half length, half depth, roof height
    vec3 back = c - fc * D;
    // posts
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2) boxY(g, c + al * (i * (L - 0.06f)) + fc * (j * (D - 0.06f)) + up * (Hr * 0.5f), st.along, vec3(0.045f, 0.045f, Hr * 0.5f), steel, mSteel);
    // roof slab with a coral fascia, slight overhang toward the curb
    boxY(g, c + fc * 0.18f + up * (Hr + 0.06f), st.along, vec3(L + 0.25f, D + 0.4f, 0.06f), white, M(MAT_METAL_PAINTED), true);
    boxY(g, c + fc * (D + 0.58f) + up * (Hr + 0.02f), st.along, vec3(L + 0.25f, 0.025f, 0.14f), rgbv(kTramCoral), mSteel, true);
    collide(g, back + up * 1.2f, st.along, vec3(L, 0.08f, 1.2f));
    if (!g.detail) {
        boxY(g, back + up * 1.3f, st.along, vec3(L, 0.03f, 1.1f), rgb(0.35f, 0.45f, 0.5f), M(MAT_GLASS));
        return;
    }
    // glass back wall and end panels in thin frames
    u32 glass = rgb(0.55f, 0.66f, 0.7f, 0.5f), mGlass = M(MAT_GLASS);
    boxY(g, back + up * 1.3f, st.along, vec3(L - 0.1f, 0.012f, 1.05f), glass, mGlass, true);
    boxY(g, back + up * 0.2f, st.along, vec3(L - 0.1f, 0.03f, 0.05f), steel, mSteel, true);
    boxY(g, back + up * 2.38f, st.along, vec3(L - 0.1f, 0.03f, 0.04f), steel, mSteel, true);
    for (int i = -1; i <= 1; i += 2) {
        vec3 endc = c + al * (i * (L - 0.06f)) - fc * 0.05f;
        boxY(g, endc + up * 1.3f, st.face, vec3(D - 0.2f, 0.012f, 1.05f), glass, mGlass, true);
        collide(g, endc + up * 1.2f, st.face, vec3(D - 0.2f, 0.06f, 1.2f));
    }
    // bench
    boxY(g, back + fc * 0.3f + up * 0.46f, st.along, vec3(1.6f, 0.2f, 0.03f), rgb(0.55f, 0.36f, 0.2f), M(MAT_WOOD), true);
    for (int i = -1; i <= 1; i += 2) boxY(g, back + fc * 0.3f + al * (i * 1.3f) + up * 0.22f, st.along, vec3(0.03f, 0.16f, 0.22f), steel, mSteel);
    // line map on the inside of the upstream end panel
    {
        vec3 endc = c - al * (L - 0.1f) - fc * 0.05f;
        vec3 nrm = al;
        vec3 ax = normalize(cross(up, nrm));
        tramMapPanel(g, T, si, endc + up * 1.45f + nrm * 0.02f, ax, up, nrm, D - 0.26f, 0.48f);
    }
    // stop display hanging under the roof at the front: line colour band, stop name in amber
    {
        vec3 dc = c + fc * (D - 0.1f) + up * (Hr - 0.26f);
        boxY(g, dc, st.along, vec3(0.62f, 0.05f, 0.15f), rgb(0.05f, 0.05f, 0.06f), mSteel, true);
        for (int fs = -1; fs <= 1; fs += 2) {
            vec3 nrm = fc * (float)fs;
            vec3 right = normalize(cross(up, nrm));
            vec3 face = dc + nrm * 0.052f;
            std::string nm = upper(st.name);
            float th = Min(0.07f, 1.1f / Max(textAdvance(nm.c_str(), 1.f), 0.1f));
            strokeText(g, m, nm.c_str(), face - right * (textAdvance(nm.c_str(), th) * 0.5f) - up * (th * 0.5f) + nrm * 0.002f, right, up, th, th * 0.2f,
                       rgb(1.f, 0.62f, 0.12f, 0.45f), emMat());
        }
        rod(g, dc + up * 0.15f + al * 0.5f, dc + up * 0.26f + al * 0.5f, 0.012f, 4, steel, mSteel);
        rod(g, dc + up * 0.15f - al * 0.5f, dc + up * 0.26f - al * 0.5f, 0.012f, 4, steel, mSteel);
    }
    // name band on the fascia (both faces)
    {
        std::string nm = "SOL BEACH STREETCAR";
        float th = 0.11f;
        vec3 nrm = fc;
        vec3 right = normalize(cross(up, nrm));
        vec3 face = c + fc * (D + 0.606f) + up * (Hr + 0.02f);
        strokeText(g, m, nm.c_str(), face - right * (textAdvance(nm.c_str(), th) * 0.5f) - up * (th * 0.5f), right, up, th, th * 0.17f, rgb(1.f, 1.f, 1.f, 0.2f),
                   emMat(EA_NIGHT));
    }
    // ticket machine beside the shelter (downstream end)
    {
        vec3 tm = c + al * (L + 0.75f) - fc * 0.35f;
        boxY(g, tm + up * 0.78f, st.along, vec3(0.26f, 0.2f, 0.78f), rgbv(kTramCoral * 0.85f), mSteel, true);
        vec3 nrm = fc;
        vec3 right = normalize(cross(up, nrm));
        vec3 scr = tm + up * 1.15f + nrm * 0.202f;
        m.quadFacing(scr - right * 0.16f - up * 0.12f - g.org, scr + right * 0.16f - up * 0.12f - g.org, scr + right * 0.16f + up * 0.12f - g.org,
                     scr - right * 0.16f + up * 0.12f - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.4f, 0.8f, 1.f, 0.3f), emMat(), nrm);
        strokeText(g, m, "TICKETS", scr - right * (textAdvance("TICKETS", 0.045f) * 0.5f) + up * 0.17f + nrm * 0.002f, right, up, 0.045f, 0.008f, white,
                   M(MAT_PAINT_WHITE));
        collide(g, tm + up * 0.78f, st.along, vec3(0.26f, 0.2f, 0.78f));
    }
    // canopy downlight and a warm glow strip under the roof
    light(g, c + up * (Hr - 0.1f), vec3(1.f, 0.9f, 0.78f) * 110.f, 8.f, 0);
    {
        vec3 a = c + fc * 0.3f + up * (Hr - 0.005f);
        m.quadFacing(a - al * (L - 0.3f) - fc * 0.04f - g.org, a + al * (L - 0.3f) - fc * 0.04f - g.org, a + al * (L - 0.3f) + fc * 0.04f - g.org,
                     a - al * (L - 0.3f) + fc * 0.04f - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(1.f, 0.92f, 0.8f, 0.5f), emMat(EA_NIGHT), -up);
    }
}

}  // namespace transit_mesh
}  // namespace World
