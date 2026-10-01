// Opening side doors for the lofted cars (included from vehicle_models.cpp after the car details, before the car
// assembly). The full-detail body is built closed as before; buildCar then cuts every door out of it along its shut
// lines (DoorLines, the same lines the panel seams are drawn on):
//  - the shell, glass, window trim, seams, stripes and liveries are split exactly on the door's outline;
//  - small separate parts (handle, mirror, armrest, release handle, speaker, lettering) go whole to the side their
//    centre is on, and the cabin structure (PART_FIXED: floor, seats, dash, headliner...) never moves;
//  - the opening gets painted jambs, a sill with a scuff plate, the B-pillar between two doors and rubber seals; the
//    door gets its shut faces and an inner panel down to its bottom edge.
// A closed door drawn with the body's transform is the closed body of before, vertex for vertex.
namespace Vehicles {
namespace detail {

// Shell grid point of column j at an arbitrary station y (linear between the bracketing rows)
inline vec3 shellAt(const CarBody& b, float y, int j) {
    int n = b.nr;
    if (y <= b.rows[0]) return b.G[j];
    if (y >= b.rows[n - 1]) return b.G[(n - 1) * b.NP + j];
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (b.rows[mid] <= y) lo = mid;
        else hi = mid;
    }
    float t = (y - b.rows[lo]) / Max(b.rows[hi] - b.rows[lo], 1e-6f);
    return lerp(b.G[lo * b.NP + j], b.G[hi * b.NP + j], t);
}
inline float shellLiftAt(const CarBody& b, float y) {
    int i = b.rowAt(y);
    return b.rowL[i];
}
// Height of a door's top edge at y: a little way up the roof-rail rounding above the side glass (closed cabins), or
// just over the belt (open tops and stations without a greenhouse)
inline float doorTopZ(const CarBody& b, float y) {
    float zb = shellAt(b, y, b.pGh0).z;
    if (b.s.openTop || shellLiftAt(b, y) < 0.02f) return zb + 0.012f;
    float zs = shellAt(b, y, b.pRail0).z, ze = shellAt(b, y, b.pRail0 + b.NR).z;
    return Max(zs + (ze - zs) * 0.3f, zb + 0.012f);
}

enum JambKind : u8 { JK_FRONT = 0, JK_SILL, JK_REAR, JK_TOP, JK_PILLAR };

// One door's outline on the right side, as a closed polygon in (y, z); kind[i] tells what the edge P[i] -> P[i+1]
// closes against (hinge pillar, sill, rear jamb, roof rail, or the B-pillar shared with the other door)
struct DoorOutline {
    std::vector<vec2> P;
    std::vector<u8> kind;
    vec2 mn, mx;
    float yF = 0.f, yR = 0.f;   // front and rear edge at the belt
    float yTop = 0.f;           // rear end of the top edge
};

inline bool insidePoly(const std::vector<vec2>& P, vec2 q) {
    bool in = false;
    size_t n = P.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if ((P[i].y > q.y) != (P[j].y > q.y)) {
            float x = (P[j].x - P[i].x) * (q.y - P[i].y) / (P[j].y - P[i].y) + P[i].x;
            if (q.x < x) in = !in;
        }
    }
    return in;
}

inline DoorOutline doorOutline(const CarBody& b, const DoorLines& DL, int k) {
    const CarSpec& s = b.s;
    DoorOutline o;
    bool last = k == DL.perSide - 1;
    float yF = k == 0 ? DL.yD0 : DL.yB;
    float yR = last ? DL.yRear : DL.yB;
    o.yF = yF;
    o.yR = yR;
    u8 frontKind = k == 0 ? JK_FRONT : JK_PILLAR, rearKind = last ? JK_REAR : JK_PILLAR;
    auto add = [&](vec2 p, u8 kind) {   // kind: of the edge leaving p
        o.P.push_back(p);
        o.kind.push_back(kind);
    };
    // bottom edge, front to rear
    add(vec2(yF - 0.01f, DL.zLow), JK_SILL);
    float R2 = DL.archR;
    bool arc = last && DL.archCut && yR - b.yWr < R2 * cosf(0.25f);
    if (arc) {
        // the lower rear corner around the rear wheel arch (as the seam): up from the sill, round the arch's offset
        // circle to the door's rear edge
        add(vec2(b.yWr + R2, DL.zLow), rearKind);
        float a0 = acosf(Clamp((yR - b.yWr) / R2, -1.f, 1.f));
        for (int i = 6; i >= 1; i--) {
            float a = lerp(a0, 0.25f, i / 6.f);
            add(vec2(b.yWr + cosf(a) * R2, s.wheelR + sinf(a) * R2), rearKind);
        }
        add(vec2(yR, s.wheelR + sinf(a0) * R2), rearKind);
    } else {
        add(vec2(yR - 0.01f, DL.zLow), rearKind);
    }
    // rear edge up to the belt, then the window frame's rear edge: straight up beside the B-pillar, or along the
    // C-pillar (the side glass's rear edge 2 cm behind) on the last door
    add(vec2(yR, b.beltZAt(yR) - 0.012f), rearKind);
    float yT = yR;
    if (last && !s.openTop) yT = Max(yR, s.dloRearTop + 0.02f);
    yT = Min(yT, yF - 0.1f);
    o.yTop = yT;
    // top edge from the rear to the front along the roof rail (at every shell row in between)
    add(vec2(yT, doorTopZ(b, yT)), JK_TOP);
    for (int i = 0; i < b.nr; i++) {
        float y = b.rows[i];
        if (y > yT + 0.004f && y < yF - 0.004f) add(vec2(y, doorTopZ(b, y)), JK_TOP);
    }
    // front edge down from the rail to the belt and on to the bottom
    add(vec2(yF, doorTopZ(b, yF)), frontKind);
    add(vec2(yF, b.beltZAt(yF) - 0.012f), frontKind);
    o.mn = vec2(1e9f, 1e9f);
    o.mx = vec2(-1e9f, -1e9f);
    for (const vec2& p : o.P) {
        o.mn = vmin(o.mn, p);
        o.mx = vmax(o.mx, p);
    }
    return o;
}

// ------------------------------------------------------------------------------------------------
// Cutting a finished mesh into the body and its doors.
struct DoorCutter {
    struct Region {
        int door;      // VehicleModel::doors index
        int k;         // 0 front, 1 rear
        float side;    // +1 right, -1 left
    };
    std::vector<DoorOutline> out;      // per k (right side)
    std::vector<Region> reg;
    // inner limit: nothing deeper than this inside the body side can belong to a door (|x| >= xLim(y))
    std::vector<float> xLim;
    float xLimY0 = 0.f, xLimStep = 0.01f;

    float limitAt(float y) const {
        int i = Clamp((int)((y - xLimY0) / xLimStep + 0.5f), 0, (int)xLim.size() - 1);
        return xLim[i];
    }
    int regionOf(vec3 p) const {
        for (int r = 0; r < (int)reg.size(); r++) {
            const DoorOutline& o = out[reg[r].k];
            if (p.x * reg[r].side < limitAt(p.y)) continue;
            vec2 q(p.y, p.z);
            if (q.x < o.mn.x || q.x > o.mx.x || q.y < o.mn.y || q.y > o.mx.y) continue;
            if (insidePoly(o.P, q)) return r;
        }
        return -1;
    }
};

// Clip vertex: a mesh vertex (orig >= 0) or a point made on a cut, with unpacked attributes
struct ClipV {
    vec3 p, n, t;
    vec2 uv;
    int orig = -1;
};
inline bool lexLess(vec3 a, vec3 b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}
// Point where the line f = 0 crosses the edge (u, v) (f values fu, fv of opposite signs), computed from the edge's
// lexicographically smaller end so both triangles sharing the edge make the identical point
inline ClipV clipEdge(const ClipV& u, float fu, const ClipV& v, float fv) {
    const ClipV* a = &u;
    const ClipV* c = &v;
    float fa = fu, fc = fv;
    if (lexLess(v.p, u.p)) {
        a = &v;
        c = &u;
        fa = fv;
        fc = fu;
    }
    float t = fa / (fa - fc);
    ClipV r;
    r.p = lerp(a->p, c->p, t);
    r.n = normalize(lerp(a->n, c->n, t));
    r.t = lerp(a->t, c->t, t);
    r.uv = lerp(a->uv, c->uv, t);
    return r;
}

struct MeshSink {
    MeshData* m = nullptr;
    std::vector<u32> remap;    // source vertex -> this mesh (0xffffffff: not yet)
    struct Key {
        u32 w[9];
        bool operator==(const Key& o) const { return memcmp(w, o.w, sizeof(w)) == 0; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            u32 h = 2166136261u;
            for (int i = 0; i < 9; i++) h = (h ^ k.w[i]) * 16777619u;
            return h;
        }
    };
    std::unordered_map<Key, u32, KeyHash> made;
    u32 orig(const MeshData& src, u32 i) {
        u32& r = remap[i];
        if (r == 0xffffffffu) {
            r = (u32)m->verts.size();
            m->verts.push_back(src.verts[i]);
            m->bounds.add(src.verts[i].pos);
        }
        return r;
    }
    u32 vert(const MeshData& src, const ClipV& c, u32 color, u32 mat) {
        if (c.orig >= 0) return orig(src, (u32)c.orig);
        VtxStatic v;
        v.pos = c.p;
        v.normal = packNormalOct(c.n);
        vec3 t = c.t - c.n * dot(c.t, c.n);
        v.tangent = packNormalOct(length2(t) > 1e-12f ? normalize(t) : anyPerp(c.n));
        v.uv = c.uv;
        v.color = color;
        v.mat = mat;
        Key k;
        memcpy(&k.w[0], &v.pos, 12);
        k.w[3] = v.normal;
        k.w[4] = v.tangent;
        memcpy(&k.w[5], &v.uv, 8);
        k.w[7] = v.color;
        k.w[8] = v.mat;
        auto it = made.find(k);
        if (it != made.end()) return it->second;
        u32 id = (u32)m->verts.size();
        m->verts.push_back(v);
        m->bounds.add(v.pos);
        made.emplace(k, id);
        return id;
    }
};

// Splits `full` (with a part tag per triangle) into `body` and doors[r] (one per cutter region).
inline void cutDoors(const MeshData& full, const std::vector<u8>& part, const DoorCutter& C, MeshData& body, std::vector<MeshData>& doors) {
    const size_t nv = full.verts.size(), nt = full.indices.size() / 3;
    const int nr = (int)C.reg.size();
    body.clear();
    doors.assign(nr, MeshData());
    std::vector<MeshSink> sink(nr + 1);
    for (int r = 0; r <= nr; r++) {
        sink[r].m = r < nr ? &doors[r] : &body;
        sink[r].remap.assign(nv, 0xffffffffu);
    }
    MeshSink& bodySink = sink[nr];
    // region of every vertex
    std::vector<i8> vreg(nv);
    for (size_t i = 0; i < nv; i++) vreg[i] = (i8)C.regionOf(full.verts[i].pos);
    // connected parts (by welded position) among the movable triangles: small ones move whole
    std::vector<u32> parent(nt);
    for (size_t t = 0; t < nt; t++) parent[t] = (u32)t;
    auto find = [&](u32 x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    {
        std::unordered_map<WeldKey, u32, WeldKeyHash> first;
        first.reserve(nt * 2 + 16);
        for (size_t t = 0; t < nt; t++) {
            if (part[t] == PART_FIXED) continue;
            for (int c = 0; c < 3; c++) {
                vec3 p = full.verts[full.indices[t * 3 + c]].pos;
                WeldKey key{(i32)floorf(p.x * 2000.f + 0.5f), (i32)floorf(p.y * 2000.f + 0.5f), (i32)floorf(p.z * 2000.f + 0.5f), 0u};
                auto it = first.find(key);
                if (it == first.end()) first.emplace(key, (u32)t);
                else {
                    u32 a = find((u32)t), b2 = find(it->second);
                    if (a != b2) parent[a] = b2;
                }
            }
        }
    }
    std::vector<vec3> cmn(nt, vec3(1e9f)), cmx(nt, vec3(-1e9f)), csum(nt, vec3(0.f));
    std::vector<float> carea(nt, 0.f);
    for (size_t t = 0; t < nt; t++) {
        if (part[t] == PART_FIXED) continue;
        u32 r = find((u32)t);
        vec3 a = full.verts[full.indices[t * 3]].pos, b2 = full.verts[full.indices[t * 3 + 1]].pos, c = full.verts[full.indices[t * 3 + 2]].pos;
        cmn[r] = vmin(cmn[r], vmin(a, vmin(b2, c)));
        cmx[r] = vmax(cmx[r], vmax(a, vmax(b2, c)));
        float ar = length(cross(b2 - a, c - a)) * 0.5f + 1e-9f;
        csum[r] += (a + b2 + c) * (ar / 3.f);
        carea[r] += ar;
    }
    std::vector<i8> wholeTo(nt, -2);   // per component root: -2 cut per triangle, -1 body, r door
    for (size_t t = 0; t < nt; t++) {
        if (part[t] == PART_FIXED || find((u32)t) != (u32)t) continue;
        if (length(cmx[t] - cmn[t]) < 0.5f) wholeTo[t] = (i8)C.regionOf(csum[t] / carea[t]);
    }
    // outline edges per region side, for the straddle test
    auto unpackV = [&](u32 i) {
        const VtxStatic& v = full.verts[i];
        ClipV c;
        c.p = v.pos;
        c.n = unpackNormalOct(v.normal);
        c.t = unpackNormalOct(v.tangent);
        c.uv = v.uv;
        c.orig = (int)i;
        return c;
    };
    std::vector<std::vector<ClipV>> pieces, next;
    for (size_t t = 0; t < nt; t++) {
        u32 i0 = full.indices[t * 3], i1 = full.indices[t * 3 + 1], i2 = full.indices[t * 3 + 2];
        int dest = -1;
        bool split = false;
        if (part[t] != PART_FIXED) {
            i8 w = wholeTo[find((u32)t)];
            if (w != -2) dest = w;
            else {
                vec3 a = full.verts[i0].pos, b2 = full.verts[i1].pos, c = full.verts[i2].pos;
                int r0 = vreg[i0], r1 = vreg[i1], r2 = vreg[i2];
                // does any outline edge on this side pass through the triangle's (y, z) box?
                vec2 mn(Min(a.y, Min(b2.y, c.y)), Min(a.z, Min(b2.z, c.z))), mx(Max(a.y, Max(b2.y, c.y)), Max(a.z, Max(b2.z, c.z)));
                float sx = a.x + b2.x + c.x;
                bool edgeHit = false;
                for (int r = 0; r < nr && !edgeHit; r++) {
                    if (sx * C.reg[r].side <= 0.f) continue;
                    const DoorOutline& o = C.out[C.reg[r].k];
                    if (mx.x < o.mn.x || mn.x > o.mx.x || mx.y < o.mn.y || mn.y > o.mx.y) continue;
                    size_t n = o.P.size();
                    for (size_t e = 0; e < n && !edgeHit; e++) {
                        vec2 p = o.P[e], q = o.P[(e + 1) % n];
                        if (Max(p.x, q.x) < mn.x || Min(p.x, q.x) > mx.x || Max(p.y, q.y) < mn.y || Min(p.y, q.y) > mx.y) continue;
                        edgeHit = true;
                    }
                }
                if (!edgeHit && r0 == r1 && r1 == r2) dest = r0;
                else if (!edgeHit) dest = C.regionOf((a + b2 + c) / 3.f);
                else split = true;
            }
        }
        u32 color = full.verts[i0].color, mat = full.verts[i0].mat;
        if (!split) {
            MeshSink& s = dest >= 0 ? sink[dest] : bodySink;
            s.m->tri(s.orig(full, i0), s.orig(full, i1), s.orig(full, i2));
            continue;
        }
        // split along every outline edge crossing the triangle, then send each convex piece where its centre is
        pieces.clear();
        pieces.push_back({unpackV(i0), unpackV(i1), unpackV(i2)});
        vec3 a = full.verts[i0].pos, b2 = full.verts[i1].pos, c = full.verts[i2].pos;
        float sx = a.x + b2.x + c.x;
        vec2 tmn(Min(a.y, Min(b2.y, c.y)), Min(a.z, Min(b2.z, c.z))), tmx(Max(a.y, Max(b2.y, c.y)), Max(a.z, Max(b2.z, c.z)));
        for (int r = 0; r < nr; r++) {
            if (sx * C.reg[r].side <= 0.f) continue;
            const DoorOutline& o = C.out[C.reg[r].k];
            size_t n = o.P.size();
            for (size_t e = 0; e < n; e++) {
                vec2 p = o.P[e], q = o.P[(e + 1) % n];
                if (Max(p.x, q.x) < tmn.x || Min(p.x, q.x) > tmx.x || Max(p.y, q.y) < tmn.y || Min(p.y, q.y) > tmx.y) continue;
                vec2 d = q - p;
                float dl = length(d);
                if (dl < 1e-6f) continue;
                vec2 nn = perp(d) / dl;
                next.clear();
                for (std::vector<ClipV>& pc : pieces) {
                    size_t m = pc.size();
                    float f[16];
                    float fmn = 1e9f, fmx = -1e9f;
                    for (size_t i = 0; i < m && i < 16; i++) {
                        f[i] = dot(nn, vec2(pc[i].p.y, pc[i].p.z) - p);
                        fmn = Min(fmn, f[i]);
                        fmx = Max(fmx, f[i]);
                    }
                    if (m > 16 || fmn > -1e-6f || fmx < 1e-6f) {
                        next.push_back(pc);
                        continue;
                    }
                    // only where the edge itself (not its extension) passes through the piece
                    float s0 = 1e9f, s1 = -1e9f;
                    for (size_t i = 0; i < m; i++) {
                        size_t j = (i + 1) % m;
                        if ((f[i] < 0.f) == (f[j] < 0.f)) continue;
                        float tt = f[i] / (f[i] - f[j]);
                        vec2 x = lerp(vec2(pc[i].p.y, pc[i].p.z), vec2(pc[j].p.y, pc[j].p.z), tt);
                        float sp = dot(x - p, d) / (dl * dl);
                        s0 = Min(s0, sp);
                        s1 = Max(s1, sp);
                    }
                    float marg = 1e-4f / dl;
                    if (s1 < -marg || s0 > 1.f + marg) {
                        next.push_back(pc);
                        continue;
                    }
                    std::vector<ClipV> pos, neg;
                    for (size_t i = 0; i < m; i++) {
                        size_t j = (i + 1) % m;
                        if (f[i] >= 0.f) pos.push_back(pc[i]);
                        if (f[i] <= 0.f) neg.push_back(pc[i]);
                        if ((f[i] > 0.f && f[j] < 0.f) || (f[i] < 0.f && f[j] > 0.f)) {
                            ClipV x = clipEdge(pc[i], f[i], pc[j], f[j]);
                            pos.push_back(x);
                            neg.push_back(x);
                        }
                    }
                    if (pos.size() >= 3) next.push_back(pos);
                    if (neg.size() >= 3) next.push_back(neg);
                }
                pieces.swap(next);
            }
        }
        for (const std::vector<ClipV>& pc : pieces) {
            vec3 ctr(0.f);
            for (const ClipV& v : pc) ctr += v.p;
            ctr = ctr / (float)pc.size();
            int r = C.regionOf(ctr);
            MeshSink& s = r >= 0 ? sink[r] : bodySink;
            u32 v0 = s.vert(full, pc[0], color, mat);
            for (size_t i = 1; i + 1 < pc.size(); i++) {
                vec3 e1 = pc[i].p - pc[0].p, e2 = pc[i + 1].p - pc[0].p;
                if (length2(cross(e1, e2)) < 1e-14f) continue;
                s.m->tri(v0, s.vert(full, pc[i], color, mat), s.vert(full, pc[i + 1], color, mat));
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Ray caster over the shell's triangles (the surface the jambs and shut faces start from)
struct ShellCaster {
    struct Tri { vec3 a, b, c; };
    std::vector<Tri> tris;
    void build(const MeshData& full, const std::vector<u8>& part, float side, vec2 mn, vec2 mx) {
        tris.clear();
        size_t nt = full.indices.size() / 3;
        for (size_t t = 0; t < nt; t++) {
            if (part[t] != PART_SHELL) continue;
            vec3 a = full.verts[full.indices[t * 3]].pos, b = full.verts[full.indices[t * 3 + 1]].pos, c = full.verts[full.indices[t * 3 + 2]].pos;
            if ((a.x + b.x + c.x) * side <= 0.f) continue;
            if (Max(a.y, Max(b.y, c.y)) < mn.x || Min(a.y, Min(b.y, c.y)) > mx.x) continue;
            if (Max(a.z, Max(b.z, c.z)) < mn.y || Min(a.z, Min(b.z, c.z)) > mx.y) continue;
            vec3 n = cross(b - a, c - a);
            float l = length(n);
            if (l < 1e-10f || fabsf(n.x) < 0.2f * l) continue;   // walls along the ray (glass seals)
            tris.push_back(Tri{a, b, c});
        }
    }
    // outermost surface point at (y, z) on the given side
    bool at(float side, float y, float z, vec3& p) const {
        vec3 o(side * 3.f, y, z), d(-side, 0.f, 0.f);
        float best = 1e30f;
        for (const Tri& t : tris) {
            float tt, u, v;
            if (Projector::hitTri(o, d, t.a, t.b, t.c, tt, u, v) && tt < best) best = tt;
        }
        if (best > 1e29f) return false;
        p = o + d * best;
        return true;
    }
};

// Samples along an outline edge (at most `step` apart, both ends included)
inline void edgeSamples(vec2 a, vec2 b, float step, std::vector<vec2>& out) {
    out.clear();
    int n = Max(1, (int)ceilf(length(b - a) / step));
    for (int i = 0; i <= n; i++) out.push_back(lerp(a, b, (float)i / n));
}

// The opening's finish (right side; side = -1 mirrors it to the left): painted jambs round the outline, the sill
// from the rocker's top edge in to the cabin floor with a scuff plate, and a rubber seal.
inline void buildJambs(PMesh& m, const CarBody& b, const InteriorLayout& I, const DoorOutline& o, const ShellCaster& sc,
                       float side, bool pillarBox) {
    const size_t n = o.P.size();
    // polygon orientation: the in-plane normal pointing into the opening
    float area = 0.f;
    for (size_t i = 0; i < n; i++) {
        vec2 p = o.P[i], q = o.P[(i + 1) % n];
        area += p.x * q.y - q.x * p.y;
    }
    float inward = area > 0.f ? 1.f : -1.f;   // CCW: perp(d) points inside
    std::vector<vec2> S;
    m.newGroup(35.f);
    for (size_t e = 0; e < n; e++) {
        u8 kind = o.kind[e];
        if (kind == JK_PILLAR && pillarBox) continue;   // the B-pillar box closes it
        vec2 a = o.P[e], c = o.P[(e + 1) % n];
        vec2 d = c - a;
        if (length2(d) < 1e-8f) continue;
        vec2 in2 = perp(normalize(d)) * inward;
        vec3 facing(0.f, in2.x, in2.y);
        edgeSamples(a, c, 0.03f, S);
        std::vector<vec3> outer(S.size());
        std::vector<u8> ok(S.size());
        for (size_t i = 0; i < S.size(); i++) ok[i] = sc.at(side, S[i].x, S[i].y, outer[i]) ? 1 : 0;
        for (size_t i = 0; i + 1 < S.size(); i++) {
            if (!ok[i] || !ok[i + 1]) continue;
            vec3 p0 = outer[i], p1 = outer[i + 1];
            if (kind == JK_SILL) {
                // sill: from the rocker's top edge in to the cabin floor (the floor's outer edge tucks under it)
                auto inner = [&](vec3 p) {
                    float fx = Min(b.beltXAt(p.y) - 0.09f, fabsf(p.x) - 0.06f);
                    return vec3(side * fx, p.y, I.zFloor + 0.006f);
                };
                vec3 q0 = inner(p0), q1 = inner(p1);
                m.use(MAT_CARPAINT, kCol1);
                vec3 s0 = lerp(p0, q0, 0.35f), s1 = lerp(p1, q1, 0.35f);
                m.quadFacing(m.add(p0), m.add(p1), m.add(s1), m.add(s0), vec3(0, 0, 1));
                // brushed scuff plate on the inner part
                m.use(MAT_METAL_BRUSHED, col(0.55f, 0.55f, 0.56f));
                m.quadFacing(m.add(s0 + vec3(0, 0, 0.002f)), m.add(s1 + vec3(0, 0, 0.002f)), m.add(q1), m.add(q0), vec3(0, 0, 1));
                // the sill's inner face down to under the floor
                m.use(MAT_INTERIOR, kCol1);
                m.quadFacing(m.add(q0), m.add(q1), m.add(q1 - vec3(0, 0, 0.06f)), m.add(q0 - vec3(0, 0, 0.06f)), vec3(-side, 0, 0));
                continue;
            }
            float depth = kind == JK_TOP ? 0.05f : (Max(S[i].y, S[i + 1].y) > b.beltZAt(S[i].x) ? 0.06f : 0.1f);
            vec3 q0 = p0 - vec3(side * depth, 0, 0), q1 = p1 - vec3(side * depth, 0, 0);
            m.use(MAT_CARPAINT, kCol1);
            m.quadFacing(m.add(p0), m.add(p1), m.add(q1), m.add(q0), facing);
            // rubber seal standing off the jamb half way in
            vec3 r0 = lerp(p0, q0, 0.45f), r1 = lerp(p1, q1, 0.45f), w = vec3(side * 0.011f, 0, 0);
            vec3 up = facing * 0.007f;
            m.use(MAT_RUBBER, kCol1);
            m.quadFacing(m.add(r0 + up), m.add(r1 + up), m.add(r1 + up - w), m.add(r0 + up - w), facing);
            m.quadFacing(m.add(r0), m.add(r1), m.add(r1 + up), m.add(r0 + up), vec3(side, 0, 0));
        }
    }
}

// The B-pillar between the front and the rear opening of a four-door body (right side; side = -1 mirrors it): a box
// following the shut line between the doors from the sill to the roof rail, its painted outer face a little under
// the doors' skins (they cover it when shut), trimmed inside.
inline void buildBPillar(PMesh& m, const CarBody& b, const DoorLines& DL, const ShellCaster& sc, float side) {
    const float w = Max(b.s.bPillarW * 0.5f, 0.035f);
    float zTop = doorTopZ(b, DL.yB) - 0.01f;
    float zBelt = b.beltZAt(DL.yB) - 0.012f;
    std::vector<float> zs;
    for (float z = DL.zLow; z < zTop; z += 0.04f) zs.push_back(z);
    zs.push_back(zBelt);
    zs.push_back(zTop);
    std::sort(zs.begin(), zs.end());
    struct Ring { vec3 of, orr, inf, inr; bool ok; };
    std::vector<Ring> R;
    float lastZ = -1e9f;
    for (float z : zs) {
        if (z - lastZ < 0.004f) continue;
        lastZ = z;
        // the shut line: 1 cm further back at the bottom (as the seam)
        float yc = z < zBelt ? lerp(DL.yB - 0.01f, DL.yB, (z - DL.zLow) / Max(zBelt - DL.zLow, 1e-3f)) : DL.yB;
        Ring r;
        vec3 p;
        r.ok = sc.at(side, yc, z, p);
        float xo = fabsf(p.x) - 0.02f;
        float depth = z > zBelt ? 0.075f : 0.11f;
        r.of = vec3(side * xo, yc + w, z);
        r.orr = vec3(side * xo, yc - w, z);
        r.inf = vec3(side * (xo - depth), yc + w, z);
        r.inr = vec3(side * (xo - depth), yc - w, z);
        R.push_back(r);
    }
    m.newGroup(35.f);
    for (size_t i = 0; i + 1 < R.size(); i++) {
        const Ring &a = R[i], &c = R[i + 1];
        if (!a.ok || !c.ok) continue;
        m.use(MAT_CARPAINT, kCol1);
        m.quadFacing(m.add(a.orr), m.add(a.of), m.add(c.of), m.add(c.orr), vec3(side, 0, 0));     // outer face
        m.quadFacing(m.add(a.of), m.add(a.inf), m.add(c.inf), m.add(c.of), vec3(0, 1, 0));        // front face
        m.quadFacing(m.add(a.orr), m.add(a.inr), m.add(c.inr), m.add(c.orr), vec3(0, -1, 0));     // rear face
        m.use(MAT_FABRIC, col(0.55f, 0.55f, 0.53f));
        m.quadFacing(m.add(a.inr), m.add(a.inf), m.add(c.inf), m.add(c.inr), vec3(-side, 0, 0));  // cabin side
    }
}

// Shut faces of a door (right side; mirrored with side = -1): round its outline from the skin to the door card's
// plane (a thin frame round the window), and the inner panel under the door card down to the door's bottom edge.
inline void buildDoorFaces(PMesh& m, const CarBody& b, const InteriorLayout& I, const DoorOutline& o, const ShellCaster& sc, float side) {
    const size_t n = o.P.size();
    float area = 0.f;
    for (size_t i = 0; i < n; i++) {
        vec2 p = o.P[i], q = o.P[(i + 1) % n];
        area += p.x * q.y - q.x * p.y;
    }
    float inward = area > 0.f ? 1.f : -1.f;
    // the door card's lower edge and plane (as buildInterior makes it)
    auto cardLoZ = [&](float y) { return Min(I.zFloor, b.beltZAt(y) - 0.06f); };
    auto innerX = [&](float y, float z, float xo) {
        float bx = b.beltXAt(y) - 0.045f, bz = b.beltZAt(y) - 0.01f, zl = cardLoZ(y);
        if (z <= zl) return bx - 0.03f;
        if (z <= bz) return lerp(bx - 0.03f, bx, (z - zl) / Max(bz - zl, 1e-3f));
        return Max(xo - 0.028f, bx);
    };
    std::vector<vec2> S;
    m.newGroup(35.f);
    for (size_t e = 0; e < n; e++) {
        vec2 a = o.P[e], c = o.P[(e + 1) % n];
        vec2 d = c - a;
        if (length2(d) < 1e-8f) continue;
        vec2 out2 = perp(normalize(d)) * -inward;
        vec3 facing(0.f, out2.x, out2.y);
        edgeSamples(a, c, 0.03f, S);
        std::vector<vec3> outer(S.size());
        std::vector<u8> ok(S.size());
        for (size_t i = 0; i < S.size(); i++) ok[i] = sc.at(side, S[i].x, S[i].y, outer[i]) ? 1 : 0;
        for (size_t i = 0; i + 1 < S.size(); i++) {
            if (!ok[i] || !ok[i + 1]) continue;
            vec3 p0 = outer[i], p1 = outer[i + 1];
            vec3 q0(side * innerX(p0.y, p0.z, fabsf(p0.x)), p0.y, p0.z), q1(side * innerX(p1.y, p1.z, fabsf(p1.x)), p1.y, p1.z);
            m.use(MAT_CARPAINT, kCol1);
            m.quadFacing(m.add(p0), m.add(p1), m.add(q1), m.add(q0), facing);
        }
    }
    // inner panel below the door card, from the door's bottom edge (or the arch cut) up to the card
    {
        float y0 = o.mx.x, y1 = o.mn.x;
        for (size_t i = 0; i < n; i++) {
            y0 = Min(y0, o.P[i].x);
            y1 = Max(y1, o.P[i].x);
        }
        int ns = Max(2, (int)ceilf((y1 - y0) / 0.03f));
        std::vector<vec2> span(ns + 1);
        std::vector<u8> ok(ns + 1, 0);
        for (int i = 0; i <= ns; i++) {
            float y = lerp(y0 + 0.004f, y1 - 0.004f, (float)i / ns);
            float lo = 1e9f;
            for (size_t e = 0; e < n; e++) {   // lowest crossing of the outline at this station
                vec2 a = o.P[e], c = o.P[(e + 1) % n];
                if ((a.x - y) * (c.x - y) > 0.f || fabsf(c.x - a.x) < 1e-7f) continue;
                lo = Min(lo, lerp(a.y, c.y, (y - a.x) / (c.x - a.x)));
            }
            float hi = cardLoZ(y) + 0.002f;
            span[i] = vec2(lo, hi);
            ok[i] = lo < hi - 0.002f ? 1 : 0;
        }
        m.use(MAT_INTERIOR, kCol1);
        for (int i = 0; i < ns; i++) {
            if (!ok[i] || !ok[i + 1]) continue;
            float ya = lerp(y0 + 0.004f, y1 - 0.004f, (float)i / ns), yb = lerp(y0 + 0.004f, y1 - 0.004f, (float)(i + 1) / ns);
            float xa = side * (b.beltXAt(ya) - 0.075f), xb = side * (b.beltXAt(yb) - 0.075f);
            m.quadFacing(m.add(vec3(xa, ya, span[i].x)), m.add(vec3(xb, yb, span[i + 1].x)), m.add(vec3(xb, yb, span[i + 1].y)),
                         m.add(vec3(xa, ya, span[i].y)), vec3(-side, 0, 0));
        }
    }
}

}  // namespace detail
}  // namespace Vehicles

namespace Vehicles {
namespace detail {

inline void meshAppend(MeshData& dst, const MeshData& src) {
    u32 base = (u32)dst.verts.size();
    for (const VtxStatic& v : src.verts) {
        dst.verts.push_back(v);
        dst.bounds.add(v.pos);
    }
    for (u32 i : src.indices) dst.indices.push_back(base + i);
}

// Cuts the opening side doors out of the finished full-detail car `m` into out.body and out.doors (with their hinge,
// handles and the opening's measurements), and links the seats to their doors. Returns the closed car's bounds.
inline AABB buildCarDoors(const CarBody& b, const InteriorLayout& I, const PMesh& m, VehicleModel& out) {
    MeshData full;
    std::vector<u8> part;
    finalizeMesh(m, full, &part);
    const DoorLines DL = doorLines(b);
    DoorCutter C;
    for (int k = 0; k < DL.perSide; k++) C.out.push_back(doorOutline(b, DL, k));
    for (int k = 0; k < DL.perSide; k++)
        for (int sd = 0; sd < 2; sd++) C.reg.push_back(DoorCutter::Region{k * 2 + sd, k, sd ? 1.f : -1.f});
    C.xLimY0 = b.yR;
    C.xLimStep = 0.01f;
    for (float y = b.yR; y <= b.yF + 0.01f; y += C.xLimStep) C.xLim.push_back(shellAt(b, y, b.pGh0).x - 0.26f);
    std::vector<MeshData> doorMesh;
    cutDoors(full, part, C, out.body, doorMesh);
    out.doors.assign(C.reg.size(), DoorSpec());
    for (size_t r = 0; r < C.reg.size(); r++) {
        const DoorCutter::Region& R = C.reg[r];
        const DoorOutline& o = C.out[R.k];
        float side = R.side;
        ShellCaster sc;
        sc.build(full, part, side, o.mn - vec2(0.15f, 0.15f), o.mx + vec2(0.15f, 0.15f));
        {
            PMesh jm;
            buildJambs(jm, b, I, o, sc, side, DL.perSide == 2);
            if (DL.perSide == 2 && R.k == 0) buildBPillar(jm, b, DL, sc, side);
            MeshData jd;
            finalizeMesh(jm, jd);
            meshAppend(out.body, jd);
        }
        {
            PMesh dm;
            buildDoorFaces(dm, b, I, o, sc, side);
            MeshData dd;
            finalizeMesh(dm, dd);
            meshAppend(doorMesh[r], dd);
        }
        DoorSpec& D = out.doors[r];
        D.mesh = std::move(doorMesh[r]);
        D.left = side < 0.f;
        D.front = R.k == 0;
        D.maxAngle = DL.perSide == 1 ? 1.08f : (R.k == 0 ? 1.15f : 1.22f);
        // hinge: along the door's front edge, just outside the skin so the door's edge swings clear of the fender
        float zb = b.beltZAt(o.yF);
        vec3 h0, h1;
        if (!sc.at(side, o.yF - 0.008f, DL.zLow + 0.12f, h0)) h0 = vec3(side * b.s.halfW, o.yF - 0.008f, DL.zLow + 0.12f);
        if (!sc.at(side, o.yF - 0.001f, zb - 0.06f, h1)) h1 = vec3(side * b.s.halfW, o.yF - 0.001f, zb - 0.06f);
        h0.x += side * 0.004f;
        h1.x += side * 0.004f;
        D.hinge = h0;
        vec3 ax = normalize(h1 - h0);
        // a right door opens with a positive turn about +Z: the left door's axis points down (mirror image)
        D.axis = side > 0.f ? ax : -ax;
        D.outward = vec3(side, 0.f, 0.f);
        // outer handle as carSideDetails places it (fingers behind the grip)
        float yh, zh;
        if (DL.perSide == 2 && R.k == 0) {
            yh = DL.yB + 0.10f;
            zh = b.beltZAt(DL.yB) - 0.085f;
        } else {
            yh = DL.yRear + (DL.perSide == 2 ? 0.12f : 0.14f);
            zh = b.beltZAt(DL.yRear + 0.1f) - 0.085f;
        }
        vec3 hp;
        if (!sc.at(side, yh, zh, hp)) hp = vec3(side * b.s.halfW, yh, zh);
        D.handle = hp + vec3(side * 0.016f, 0.f, 0.f);
        // inner release handle (cabinFurniture)
        float yHip = R.k == 0 ? I.yHipF + 0.05f : I.yHipR + 0.05f;
        DoorCardLayout dc = doorCardLayout(b, I, DL, R.k, yHip);
        D.handleIn = vec3(side * (dc.pull.x - 0.012f), dc.pull.y, dc.pull.z);
        // grip on the top of the door near its rear edge
        float yg = o.yTop + 0.07f;
        vec3 gp;
        float zg = doorTopZ(b, yg) - 0.03f;
        if (!sc.at(side, yg, zg, gp)) gp = vec3(side * b.s.halfW, yg, zg);
        D.grip = gp;
        // the opening
        D.yFront = o.yF;
        D.yRear = o.yR;
        D.sillZ = I.zFloor;
        vec3 sp;
        float ym = (o.yF + o.yR) * 0.5f;
        D.sillX = sc.at(side, ym, DL.zLow + 0.01f, sp) ? fabsf(sp.x) : b.s.halfW;
        float ys = R.k == 0 ? I.yHipF : I.yHipR;
        D.roofZ = doorTopZ(b, Clamp(ys, o.yTop + 0.05f, o.yF - 0.05f)) - 0.03f;
    }
    return full.bounds;
}

// Seats to their doors (two-door bodies: the rear seats get in through the front doors): the front pair are seats 0
// and 1, the rear pair 2 and 3; the door on the seat's own side.
inline void linkSeatDoors(VehicleModel& out) {
    int perSide = (int)out.doors.size() / 2;
    for (size_t i = 0; i < out.seats.size(); i++) {
        SeatSpec& s = out.seats[i];
        int k = (perSide == 2 && i >= 2) ? 1 : 0;
        s.door = k * 2 + (s.pos.x > 0.f ? 1 : 0);
        if (s.door >= (int)out.doors.size()) s.door = -1;
    }
}

}  // namespace detail
}  // namespace Vehicles
