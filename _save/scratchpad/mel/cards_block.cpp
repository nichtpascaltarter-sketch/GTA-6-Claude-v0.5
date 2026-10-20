// ------------------------------------------------------------------------------------------------
// Strand cards (conventions: CardKind in anim_internal.h). The shell stays underneath for coverage; cards lie on it
// in two layers combed along the style's flow, roots inside the shell and tips on / just above its surface.

// Head surface along (theta, phi) from the grid centre: point and outward normal.
static void headSurf(const BuildCtx& c, float th, float ph, vec3& p, vec3& n) {
    const HeadInfo& H = c.head;
    vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
    float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * c.D->headS);
    p = H.C + dir * t;
    vec3 g = c.sdf.grad(p, MK_HEAD);
    n = length2(g) > 1e-12f ? normalize(g) : dir;
}
// A head grid-like vertex at (theta, phi) for the coverage / thickness functions.
static BVert headProbe(float th, float ph, vec3 p) {
    BVert v;
    v.pa = th < 0.f ? th + kTwoPi : (th >= kTwoPi ? th - kTwoPi : th);
    v.pb = ph;
    v.part = PART_HEAD;
    v.pc = 1.5f;
    v.p = v.bp = p;
    return v;
}
// Step from a scalp point along a tangent direction and land back on the scalp (ray from the grid centre).
static void scalpStep(const BuildCtx& c, vec3 q, vec3 f, float seg, float& th, float& ph, vec3& p, vec3& n) {
    vec3 dir = normalize(q + f * seg - c.head.C);
    th = atan2f(dir.x, dir.y);
    if (th < 0.f) th += kTwoPi;
    ph = asinf(Clamp(dir.z, -1.f, 1.f));
    headSurf(c, th, ph, p, n);
}

// Combing direction of the style at a scalp point (unit, tangent to the scalp).
static vec3 scalpFlow(const BuildCtx& c, const HairParams& h, vec3 p, vec3 n, u32 cardSeed) {
    const HeadInfo& H = c.head;
    vec3 hp = (p - H.origin) / c.D->headS;   // head space: x right, y forward, z up
    float top = sstep(0.1f, 0.16f, hp.z);
    vec3 f;
    switch (h.style) {
        case HAIR_SLICKED: f = vec3(0.f, -1.f, -0.35f - 0.5f * (1.f - top)); break;
        case HAIR_QUIFF: {
            float fr = sstep(0.015f, 0.07f, hp.y) * sstep(0.08f, 0.13f, hp.z);   // front top: up, then back over the top
            f = lerp(vec3(hp.x * 3.f, -1.f, -0.45f), vec3(0.f, -0.3f, 1.f), fr);
            break;
        }
        case HAIR_PONYTAIL: case HAIR_BUN: {
            vec3 tie = h.style == HAIR_PONYTAIL ? vec3(0.f, -0.097f, 0.066f) : vec3(0.f, -0.078f, 0.132f);
            f = tie - hp;
            break;
        }
        case HAIR_LONG: case HAIR_BOB: {
            // falls away from the parting, then straight down
            float side = hp.x >= h.partX ? 1.f : -1.f;
            f = vec3(side * (0.7f + 0.5f * top), -0.12f, -1.f);
            break;
        }
        case HAIR_CURLY: {
            Rng r(hash32(cardSeed * 911u + 3u));
            f = vec3(r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 0.4f));
            break;
        }
        default: {   // natural short: from the crown whorl outwards, gravity on the sides and back
            vec3 whorl(0.012f, -0.05f, 0.158f);
            f = normalize(hp - whorl) + vec3(0.f, 0.f, -0.8f * (1.f - top));
            break;
        }
    }
    f = f - n * dot(f, n);
    return length2(f) > 1e-10f ? normalize(f) : normalize(anyPerp(n));
}

// Scalp cards over the shell (shellTop(v) = the shell's height above the scalp at a grid-like vertex).
static void buildScalpCards(OutfitCtx& o, const HairParams& h, float shellFrac) {
    BuildCtx& c = o.c;
    const HeadInfo& H = c.head;
    const float hs = c.D->headS;
    float len0, len1, w0, spacing, dens = 1.f, stand = 0.f;
    int NS;
    switch (h.style) {
        case HAIR_SHORT: len0 = 0.026f; len1 = 0.04f; w0 = 0.014f; spacing = 0.0125f; NS = 3; break;
        case HAIR_SLICKED: len0 = 0.05f; len1 = 0.075f; w0 = 0.015f; spacing = 0.013f; NS = 4; break;
        case HAIR_QUIFF: len0 = 0.03f; len1 = 0.05f; w0 = 0.014f; spacing = 0.0125f; NS = 4; break;
        case HAIR_PONYTAIL: case HAIR_BUN: len0 = 0.05f; len1 = 0.08f; w0 = 0.015f; spacing = 0.013f; NS = 4; break;
        case HAIR_LONG: case HAIR_BOB: len0 = 0.055f; len1 = 0.085f; w0 = 0.016f; spacing = 0.0135f; NS = 4; break;
        case HAIR_CURLY: len0 = 0.016f; len1 = 0.026f; w0 = 0.012f; spacing = 0.0135f; NS = 3; stand = 1.f; dens = 0.85f; break;
        default: return;   // bald, buzz cut, cornrows: the shell and scalp tint carry them
    }
    MeshB m;
    Rng r(hash32(c.d->seed * 6131u + 17u));
    const float R0 = 0.095f * hs;
    const vec3 colRoot = h.col * 0.5f, colTip = h.col * 1.08f;
    CardPt pts[8];
    for (int layer = 0; layer < 2; layer++) {
        float sp = spacing * hs * (layer ? 1.2f : 1.f);
        float dph = sp / R0;
        float hRoot = layer ? 0.9f : 0.62f, hTip = layer ? 1.12f : 0.98f;
        for (float ph = -42.f * kDegToRad + dph * 0.5f * layer; ph < 87.f * kDegToRad; ph += dph) {
            float circ = kTwoPi * R0 * Max(cosf(ph), 0.05f);
            int nth = Max(3, (int)(circ / sp));
            float off = r.f();
            for (int i = 0; i < nth; i++) {
                float th = kTwoPi * (i + off + 0.4f * (r.f() - 0.5f)) / nth;
                float php = ph + dph * 0.4f * (r.f() - 0.5f);
                if (th >= kTwoPi) th -= kTwoPi;
                vec3 q, nq;
                headSurf(c, th, php, q, nq);
                BVert pr = headProbe(th, php, q);
                if (hairCoverage(c, h, pr) < 0.002f) continue;
                u32 seed = r.next();
                float len = r.range(len0, len1) * hs;
                if (h.style == HAIR_QUIFF && (q - H.origin).y > 0.02f * hs && (q - H.origin).z > 0.1f * hs) len *= 1.35f;
                float w = w0 * hs * r.range(0.85f, 1.15f);
                float seg = len / NS;
                int np = 0;
                float thq = th, phq = php;
                for (int sgi = 0; sgi <= NS; sgi++) {
                    float u = (float)sgi / NS;
                    BVert pq = headProbe(thq, phq, q);
                    float cq = hairCoverage(c, h, pq);
                    if (sgi > 0 && cq < -0.006f) break;   // tips may fall 6 mm past the hairline, no further
                    float T = styleThickness(c, h, pq) * sstep(-0.006f, 0.012f, cq);
                    float hgt = 0.0008f + T * Lerp(hRoot, hTip, sstep(0.f, 0.5f, u)) * (np == 0 ? 1.f : 1.f);
                    if (stand > 0.f) hgt += T * 0.25f * u;   // curls stand off the afro surface
                    if (hgt < T * shellFrac && sgi > 0 && layer == 0) hgt = T * shellFrac + 0.0006f;
                    pts[np].p = q + nq * hgt;
                    pts[np].n = nq;
                    pts[np].w = w * (1.f - 0.35f * u);
                    pts[np].sw = skin1(B_HEAD);
                    np++;
                    if (sgi == NS) break;
                    vec3 f = scalpFlow(c, h, q, nq, seed);
                    vec3 q1, n1;
                    scalpStep(c, q, f, seg, thq, phq, q1, n1);
                    q = q1;
                    nq = n1;
                }
                if (np >= 2) emitCard(m, pts, np, CARD_SCALP, seed, colRoot, colTip, dens * (layer ? 0.8f : 1.f), PART_HEAD, &H);
            }
        }
    }
    size_t t0 = o.out.idx.size() / 3;
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    OutfitCtx::Layer L;
    L.cov = [](const BVert&) { return 1.f; };
    L.margin = 0.f;
    L.t0 = t0;
    L.t1 = o.out.idx.size() / 3;
    o.layers.push_back(L);   // hats hide the cards under them like the shell
}

