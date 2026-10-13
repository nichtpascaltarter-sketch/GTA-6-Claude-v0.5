// Scratch diagnostic: clothing penetration into the visible skin (poke-through) and into the complete body (collapse),
// per pose, with the worst spots.
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include <unordered_map>
#include <map>
#include <cstdio>
#include <cstdlib>

using namespace Anim;
using namespace Anim::detail;

static vec3 skinPt(const mat4* M, const u8* b, const float* w, vec3 p, bool dir) {
    vec3 r(0);
    for (int k = 0; k < 4; k++) {
        if (w[k] <= 0.f) continue;
        const mat4& m = M[b[k]];
        r += (m.c[0].xyz() * p.x + m.c[1].xyz() * p.y + m.c[2].xyz() * p.z + (dir ? vec3(0) : m.c[3].xyz())) * w[k];
    }
    return r;
}
static u64 key(int x, int y, int z) { return ((u64)(u32)(x + 100000) << 40) ^ ((u64)(u32)(y + 100000) << 20) ^ (u64)(u32)(z + 100000); }

struct PoseDef {
    Clip c;
    float t;
    const char* name;
};
static const PoseDef kPoses[] = {{CLIP_IDLE, -1.f, "bind"},    {CLIP_IDLE, 0.3f, "idle"},  {CLIP_WALK, 0.f, "walk0"},  {CLIP_WALK, 0.25f, "walk25"},
                                 {CLIP_WALK, 0.5f, "walk50"},  {CLIP_WALK, 0.75f, "walk75"}, {CLIP_RUN, 0.3f, "run30"}, {CLIP_RUN, 0.8f, "run80"}, {CLIP_RUN, 0.f, "run0"}, {CLIP_RUN, 0.5f, "run50"}, {CLIP_RUN, 0.75f, "run75"},
                                 {CLIP_SIT_BENCH, 0.5f, "sit"}};
const int kNumPoses = sizeof(kPoses) / sizeof(kPoses[0]);

struct Surf {
    std::vector<vec3> P, N, B;   // posed pos, normal, bind pos
    std::vector<int> PB;         // dominant bone
    std::unordered_map<u64, std::vector<u32>> grid;
    void index() {
        grid.clear();
        for (u32 i = 0; i < (u32)P.size(); i++) grid[key((int)floorf(P[i].x / 0.02f), (int)floorf(P[i].y / 0.02f), (int)floorf(P[i].z / 0.02f))].push_back(i);
    }
};
struct Hit {
    float depth;
    u32 vi;
    int si;
};
struct PoseStat {
    int n = 0, overVis = 0, overBody = 0, nSkin = 0, poke = 0, xTris = 0;
    float worstVis = 0, worstBody = 0, worstPoke = 0, worstX = 0;
    std::vector<std::pair<float, u32>> xs;
    std::vector<Hit> vis, body;
    std::vector<std::pair<float, u32>> pokes;   // depth, skin vertex (dressed index)
};

static float tolVis = 0.004f, tolBody = 0.015f, tangMax = 0.012f;

static void query(const Surf& S, const Skeleton& sk, const VtxSkinned& v, vec3 q, int& bi, float& depth) {
    int cx = (int)floorf(q.x / 0.02f), cy = (int)floorf(q.y / 0.02f), cz = (int)floorf(q.z / 0.02f);
    float best = 0.06f * 0.06f;
    bi = -1;
    for (int dz = -3; dz <= 3; dz++)
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                auto it = S.grid.find(key(cx + dx, cy + dy, cz + dz));
                if (it == S.grid.end()) continue;
                for (u32 j : it->second) {
                    int sb = S.PB[j];
                    bool ok = false;
                    for (int k = 0; k < 4 && !ok; k++) {
                        if (v.weights[k] < 13) continue;
                        int cb = v.bones[k];
                        if (sb == cb || sk.parent[sb] == cb || sk.parent[cb] == sb) ok = true;
                    }
                    if (!ok) continue;
                    float d2 = length2(S.P[j] - q);
                    if (d2 < best) {
                        best = d2;
                        bi = (int)j;
                    }
                }
            }
    depth = -1.f;
    if (bi < 0) return;
    depth = -dot(q - S.P[bi], S.N[bi]);
    float tang = length((q - S.P[bi]) + S.N[bi] * depth);
    if (tang > tangMax) depth = -1.f;
}

struct CaseResult {
    PoseStat ps[16];
};


// segment pq against triangle abc
static bool segTri(vec3 p, vec3 q, vec3 a, vec3 b, vec3 c) {
    vec3 d = q - p, e1 = b - a, e2 = c - a;
    vec3 h = cross(d, e2);
    float det = dot(e1, h);
    if (fabsf(det) < 1e-14f) return false;
    float inv = 1.f / det;
    vec3 sv = p - a;
    float u = dot(sv, h) * inv;
    if (u < 0.f || u > 1.f) return false;
    vec3 qv = cross(sv, e1);
    float v = dot(d, qv) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    float t = dot(e2, qv) * inv;
    return t >= 0.f && t <= 1.f;
}
static bool triTri(const vec3* A, const vec3* B) {
    for (int k = 0; k < 3; k++)
        if (segTri(A[k], A[(k + 1) % 3], B[0], B[1], B[2]) || segTri(B[k], B[(k + 1) % 3], A[0], A[1], A[2])) return true;
    return false;
}
static bool compatBones(const Skeleton& sk, const VtxSkinned& x, const VtxSkinned& y) {
    for (int i = 0; i < 4; i++) {
        if (x.weights[i] < 13) continue;
        for (int j = 0; j < 4; j++) {
            if (y.weights[j] < 13) continue;
            int a = x.bones[i], b = y.bones[j];
            if (a == b || sk.parent[a] == b || sk.parent[b] == a) return true;
        }
    }
    return false;
}
// skin / cloth triangle pairs that intersect (keys: skin tri << 32 | cloth tri), with the skin's depth in front of the cloth
static void intersectPairs(const SkinnedMeshData& m, const Skeleton& sk, const std::vector<vec3>& Q, std::unordered_map<u64, float>& out) {
    out.clear();
    auto isClothV = [&](u32 i) {
        u32 mat = m.verts[i].mat & 0xffu;
        return !(mat == MAT_SKIN || mat == MAT_HAIR || mat == MAT_EYE || mat == MAT_CAR_GLASS);
    };
    std::unordered_map<u64, std::vector<u32>> cells;
    const float C = 0.03f;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        u32 a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
        if (!isClothV(a) || !isClothV(b) || !isClothV(c)) continue;
        vec3 lo = vmin(Q[a], vmin(Q[b], Q[c])), hi = vmax(Q[a], vmax(Q[b], Q[c]));
        for (int z = (int)floorf(lo.z / C); z <= (int)floorf(hi.z / C); z++)
            for (int y = (int)floorf(lo.y / C); y <= (int)floorf(hi.y / C); y++)
                for (int x = (int)floorf(lo.x / C); x <= (int)floorf(hi.x / C); x++) cells[key(x, y, z)].push_back((u32)t);
    }
    std::vector<u32> stamp(m.indices.size() / 3 + 1, 0xffffffffu);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        u32 a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
        if ((m.verts[a].mat & 0xffu) != MAT_SKIN || (m.verts[b].mat & 0xffu) != MAT_SKIN || (m.verts[c].mat & 0xffu) != MAT_SKIN) continue;
        vec3 A[3] = {Q[a], Q[b], Q[c]};
        vec3 lo = vmin(A[0], vmin(A[1], A[2])), hi = vmax(A[0], vmax(A[1], A[2]));
        for (int z = (int)floorf(lo.z / C); z <= (int)floorf(hi.z / C); z++)
            for (int y = (int)floorf(lo.y / C); y <= (int)floorf(hi.y / C); y++)
                for (int x = (int)floorf(lo.x / C); x <= (int)floorf(hi.x / C); x++) {
                    auto it = cells.find(key(x, y, z));
                    if (it == cells.end()) continue;
                    for (u32 ct : it->second) {
                        if (stamp[ct / 3] == (u32)t) continue;
                        stamp[ct / 3] = (u32)t;
                        u32 ca = m.indices[ct], cb = m.indices[ct + 1], cc = m.indices[ct + 2];
                        if (!compatBones(sk, m.verts[a], m.verts[ca])) continue;
                        vec3 B[3] = {Q[ca], Q[cb], Q[cc]};
                        if (!triTri(A, B)) continue;
                        vec3 fn = cross(B[1] - B[0], B[2] - B[0]);
                        float depth = 0.f;
                        if (length2(fn) > 1e-14f) {
                            fn = normalize(fn);
                            vec3 nsum = unpackNormalOct(m.verts[ca].normal) + unpackNormalOct(m.verts[cb].normal) + unpackNormalOct(m.verts[cc].normal);
                            if (dot(fn, nsum) < 0.f) fn = -fn;   // (bind normals: orientation only)
                            for (int k = 0; k < 3; k++) depth = Max(depth, dot(A[k] - B[0], fn));
                        }
                        out[((u64)(t / 3) << 32) | (u64)(ct / 3)] = depth;
                    }
                }
    }
}

static void analyze(const CharacterDesc& d, CaseResult& R, const char* onlyPose) {
    Skeleton sk;
    buildSkeleton(d, sk);
    SkinnedMeshData dressed;
    buildCharacterMesh(d, sk, dressed);
    BodyDims D;
    computeDims(d, D);
    BuildCtx bc;
    bc.d = &d;
    bc.D = &D;
    bc.sk = &sk;
    bc.skin = d.skinTone;
    bc.lipCol = bc.palmCol = bc.lipInner = bc.skin;
    buildBody(bc);
    std::unordered_map<u64, float> bindPairs;
    {
        std::vector<vec3> QB(dressed.verts.size());
        for (size_t i = 0; i < QB.size(); i++) QB[i] = dressed.verts[i].pos;
        intersectPairs(dressed, sk, QB, bindPairs);
    }
    for (int pi = 0; pi < kNumPoses; pi++) {
        const PoseDef& pd = kPoses[pi];
        if (onlyPose && strcmp(onlyPose, pd.name)) continue;
        Pose pose;
        sampleClip(sk, pd.c, Max(pd.t, 0.f) * clipInfo(pd.c).duration, pose, getenv("VSEED") ? (u32)atoi(getenv("VSEED")) : d.seed);
        if (pd.t < 0.f) {
            for (int b = 0; b < B_COUNT; b++) pose.rot[b] = sk.bindLocalRot[b];
            pose.rootOffset = vec3(0);
        }
        mat4 model[B_COUNT], sm[B_COUNT];
        computeMatrices(sk, pose, model, sm);
        Surf vis, body;
        for (u32 i = 0; i < (u32)dressed.verts.size(); i++) {
            const VtxSkinned& v = dressed.verts[i];
            if ((v.mat & 0xffu) != MAT_SKIN) continue;
            float w[4];
            int bb = 0;
            for (int k = 0; k < 4; k++) {
                w[k] = v.weights[k] / 255.f;
                if (w[k] > w[bb]) bb = k;
            }
            vis.P.push_back(skinPt(sm, v.bones, w, v.pos, false));
            vis.N.push_back(normalize(skinPt(sm, v.bones, w, unpackNormalOct(v.normal), true)));
            vis.PB.push_back(v.bones[bb]);
            vis.B.push_back(v.pos);
        }
        for (u32 i = 0; i < (u32)bc.m.v.size(); i++) {
            const BVert& v = bc.m.v[i];
            if (v.mat != MAT_SKIN || v.part == PART_EYE || v.part == PART_FACEDETAIL || v.part == PART_MOUTH) continue;
            float w[4];
            int bb = 0;
            for (int k = 0; k < 4; k++) {
                w[k] = v.sw.w[k];
                if (w[k] > w[bb]) bb = k;
            }
            body.P.push_back(skinPt(sm, v.sw.b, w, v.p, false));
            body.N.push_back(normalize(skinPt(sm, v.sw.b, w, v.n, true)));
            body.PB.push_back(v.sw.b[bb]);
            body.B.push_back(v.p);
        }
        vis.index();
        body.index();
        // full-body skin vertices that are shown on the dressed mesh (a dressed skin vertex within 6 mm in bind space)
        std::vector<u8> shown(body.P.size(), 0);
        {
            std::unordered_map<u64, std::vector<u32>> bg;
            for (u32 i = 0; i < (u32)vis.B.size(); i++) bg[key((int)floorf(vis.B[i].x / 0.02f), (int)floorf(vis.B[i].y / 0.02f), (int)floorf(vis.B[i].z / 0.02f))].push_back(i);
            for (u32 j = 0; j < (u32)body.B.size(); j++) {
                vec3 b = body.B[j];
                int cx = (int)floorf(b.x / 0.02f), cy = (int)floorf(b.y / 0.02f), cz = (int)floorf(b.z / 0.02f);
                for (int dz = -1; dz <= 1 && !shown[j]; dz++)
                    for (int dy = -1; dy <= 1 && !shown[j]; dy++)
                        for (int dx = -1; dx <= 1 && !shown[j]; dx++) {
                            auto it = bg.find(key(cx + dx, cy + dy, cz + dz));
                            if (it == bg.end()) continue;
                            for (u32 i : it->second)
                                if (length2(vis.B[i] - b) < 0.006f * 0.006f) {
                                    shown[j] = 1;
                                    break;
                                }
                        }
            }
        }
        PoseStat& st = R.ps[pi];
        std::vector<vec3> Q(dressed.verts.size()), QN(dressed.verts.size());
        std::vector<u8> isCloth(dressed.verts.size(), 0);
        for (u32 vi = 0; vi < (u32)dressed.verts.size(); vi++) {
            const VtxSkinned& v = dressed.verts[vi];
            u32 mat = v.mat & 0xffu;
            float w[4];
            for (int k = 0; k < 4; k++) w[k] = v.weights[k] / 255.f;
            Q[vi] = skinPt(sm, v.bones, w, v.pos, false);
            QN[vi] = normalize(skinPt(sm, v.bones, w, unpackNormalOct(v.normal), true));
            isCloth[vi] = !(mat == MAT_SKIN || mat == MAT_HAIR || mat == MAT_EYE || mat == MAT_CAR_GLASS);
        }
        {
            std::unordered_map<u64, float> pairs;
            intersectPairs(dressed, sk, Q, pairs);
            std::unordered_map<u32, float> skinTris;   // skin triangles newly through cloth, deepest
            std::unordered_map<u32, u32> clothOf;
            for (auto& kv : pairs) {
                if (bindPairs.count(kv.first)) continue;
                u32 stt = (u32)(kv.first >> 32);
                float& dref = skinTris[stt];
                if (kv.second >= dref) clothOf[stt] = (u32)(kv.first & 0xffffffffu);
                dref = Max(dref, kv.second);
            }
            for (auto& kv : skinTris) {
                if (kv.second < 0.002f) continue;
                st.xTris++;
                st.worstX = Max(st.worstX, kv.second);
                st.xs.push_back({kv.second, kv.first});
            }
            if (getenv("SHOW") && !st.xs.empty()) {
                std::sort(st.xs.begin(), st.xs.end(), [](const std::pair<float, u32>& x, const std::pair<float, u32>& y) { return x.first > y.first; });
                printf("   %s X: %zu skin tris through cloth\n", pd.name, st.xs.size());
                std::vector<vec3> taken;
                int shownN = 0;
                for (auto& pk : st.xs) {
                    const VtxSkinned& v = dressed.verts[dressed.indices[pk.second * 3]];
                    bool near = false;
                    for (vec3 t : taken)
                        if (length(t - v.pos) < 0.04f) near = true;
                    if (near) continue;
                    taken.push_back(v.pos);
                    const VtxSkinned& cv = dressed.verts[dressed.indices[clothOf[pk.second] * 3]];
                    if (getenv("TRIS")) {
                        for (int e = 0; e < 3; e++) {
                            const VtxSkinned& a = dressed.verts[dressed.indices[pk.second * 3 + e]];
                            printf("        skin v (%.3f %.3f %.3f) bones %d:%d %d:%d %d:%d\n", a.pos.x, a.pos.y, a.pos.z, a.bones[0], a.weights[0], a.bones[1], a.weights[1], a.bones[2], a.weights[2]);
                        }
                        for (int e = 0; e < 3; e++) {
                            const VtxSkinned& a = dressed.verts[dressed.indices[clothOf[pk.second] * 3 + e]];
                            printf("        cloth v (%.3f %.3f %.3f) bones %d:%d %d:%d %d:%d\n", a.pos.x, a.pos.y, a.pos.z, a.bones[0], a.weights[0], a.bones[1], a.weights[1], a.bones[2], a.weights[2]);
                        }
                    }
                    printf("     %5.1f mm skin tri %u bind (%.3f %.3f %.3f) bones %d:%d %d:%d | cloth (%.3f %.3f %.3f) mat %u p %x col %08x bones %d:%d %d:%d %d:%d\n", pk.first * 1000.f, pk.second, v.pos.x, v.pos.y, v.pos.z, v.bones[0], v.weights[0], v.bones[1], v.weights[1],
                           cv.pos.x, cv.pos.y, cv.pos.z, cv.mat & 0xffu, cv.mat >> 8, cv.color, cv.bones[0], cv.weights[0], cv.bones[1], cv.weights[1], cv.bones[2], cv.weights[2]);
                    if (++shownN >= atoi(getenv("SHOW"))) break;
                }
            }
        }
        auto test = [&](u32 vi, vec3 q, vec3 gn) {
            st.n++;
            int bi;
            float depth;
            query(body, sk, dressed.verts[vi], q, bi, depth);
            if (bi < 0) return;
            if (dot(gn, body.N[bi]) < 0.2f) return;   // facings, linings, caps: meant to be inside
            if (depth > tolVis && shown[bi]) {
                st.overVis++;
                st.vis.push_back({depth, vi, bi});
                st.worstVis = Max(st.worstVis, depth);
            }
            if (depth > tolBody) {
                st.overBody++;
                st.body.push_back({depth, vi, bi});
                st.worstBody = Max(st.worstBody, depth);
            }
        };
        for (u32 vi = 0; vi < (u32)dressed.verts.size(); vi++)
            if (isCloth[vi]) test(vi, Q[vi], QN[vi]);
        // triangle centroids: a knee can poke through the middle of a large skirt quad without reaching a vertex
        if (!getenv("NOCENT"))
            for (size_t t = 0; t + 2 < dressed.indices.size(); t += 3) {
                u32 a = dressed.indices[t], b = dressed.indices[t + 1], c2 = dressed.indices[t + 2];
                if (!isCloth[a] || !isCloth[b] || !isCloth[c2]) continue;
                vec3 fn = cross(Q[b] - Q[a], Q[c2] - Q[a]);
                if (length2(fn) < 1e-12f) continue;
                fn = normalize(fn);
                if (dot(fn, QN[a] + QN[b] + QN[c2]) < 0.f) fn = -fn;
                test(a, (Q[a] + Q[b] + Q[c2]) / 3.f, fn);
            }
        // skin through cloth: a visible skin vertex just in front of a cloth triangle that it was just behind in the
        // bind pose (the bind plane test keeps arms beside a shirt and hands in front of a skirt out; the posed
        // projection must fall inside the triangle, so skin beyond a hem is not counted; bone-compatible pairs only)
        {
            std::unordered_map<u64, std::vector<u32>> tg;   // cloth triangles by posed centroid (3 cm cells)
            for (size_t t = 0; t + 2 < dressed.indices.size(); t += 3) {
                u32 a = dressed.indices[t], b = dressed.indices[t + 1], c2 = dressed.indices[t + 2];
                if (!isCloth[a] || !isCloth[b] || !isCloth[c2]) continue;
                vec3 ce = (Q[a] + Q[b] + Q[c2]) / 3.f;
                tg[key((int)floorf(ce.x / 0.03f), (int)floorf(ce.y / 0.03f), (int)floorf(ce.z / 0.03f))].push_back((u32)t);
            }
            const float tolPoke = getenv("TOLP") ? (float)atof(getenv("TOLP")) : 0.003f;
            for (u32 si = 0; si < (u32)dressed.verts.size(); si++) {
                const VtxSkinned& sv = dressed.verts[si];
                if ((sv.mat & 0xffu) != MAT_SKIN) continue;
                st.nSkin++;
                vec3 sp = Q[si], sn = QN[si];
                int sb = 0;
                for (int k = 1; k < 4; k++)
                    if (sv.weights[k] > sv.weights[sb]) sb = k;
                int sbone = sv.bones[sb];
                int cx = (int)floorf(sp.x / 0.03f), cy = (int)floorf(sp.y / 0.03f), cz = (int)floorf(sp.z / 0.03f);
                float worst = 0.f;
                for (int dz = -2; dz <= 2; dz++)
                    for (int dy = -2; dy <= 2; dy++)
                        for (int dx = -2; dx <= 2; dx++) {
                            auto it = tg.find(key(cx + dx, cy + dy, cz + dz));
                            if (it == tg.end()) continue;
                            for (u32 t : it->second) {
                                u32 tv[3] = {dressed.indices[t], dressed.indices[t + 1], dressed.indices[t + 2]};
                                bool ok = false;
                                for (int e = 0; e < 3 && !ok; e++) {
                                    const VtxSkinned& cv = dressed.verts[tv[e]];
                                    for (int k = 0; k < 4 && !ok; k++) {
                                        if (cv.weights[k] < 13) continue;
                                        int cb = cv.bones[k];
                                        if (sbone == cb || sk.parent[sbone] == cb || sk.parent[cb] == sbone) ok = true;
                                    }
                                }
                                if (!ok) continue;
                                u32 a = tv[0], b = tv[1], c2 = tv[2];
                                vec3 fn = cross(Q[b] - Q[a], Q[c2] - Q[a]);
                                if (length2(fn) < 1e-14f) continue;
                                fn = normalize(fn);
                                if (dot(fn, QN[a] + QN[b] + QN[c2]) < 0.f) fn = -fn;
                                if (dot(fn, sn) < 0.f) continue;   // linings / inner surfaces
                                float dd = dot(sp - Q[a], fn);
                                if (dd <= tolPoke || dd > 0.03f) continue;
                                vec3 pp = sp - fn * dd;
                                vec3 v0 = Q[b] - Q[a], v1 = Q[c2] - Q[a], v2 = pp - Q[a];
                                float d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
                                float den = d00 * d11 - d01 * d01;
                                if (fabsf(den) < 1e-18f) continue;
                                float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den, bu = 1.f - bv - bw;
                                if (bu < -0.01f || bv < -0.01f || bw < -0.01f) continue;
                                const vec3 A = dressed.verts[a].pos, B = dressed.verts[b].pos, C = dressed.verts[c2].pos;
                                vec3 bn = cross(B - A, C - A);
                                if (length2(bn) < 1e-14f) continue;
                                bn = normalize(bn);
                                if (dot(bn, unpackNormalOct(dressed.verts[a].normal) + unpackNormalOct(dressed.verts[b].normal) +
                                                unpackNormalOct(dressed.verts[c2].normal)) < 0.f)
                                    bn = -bn;
                                float db = dot(sv.pos - A, bn);
                                if (db >= 0.f || db < -0.08f) continue;
                                worst = Max(worst, dd);
                            }
                        }
                if (worst > 0.f) {
                    st.poke++;
                    st.worstPoke = Max(st.worstPoke, worst);
                    st.pokes.push_back({worst, si});
                }
            }
            if (getenv("SHOW") && !st.pokes.empty()) {
                std::sort(st.pokes.begin(), st.pokes.end(), [](const std::pair<float, u32>& x, const std::pair<float, u32>& y) { return x.first > y.first; });
                printf("   %s POKE: %zu\n", pd.name, st.pokes.size());
                std::vector<vec3> taken;
                int shownN = 0;
                for (auto& pk : st.pokes) {
                    const VtxSkinned& v = dressed.verts[pk.second];
                    bool near = false;
                    for (vec3 t : taken)
                        if (length(t - v.pos) < 0.04f) near = true;
                    if (near) continue;
                    taken.push_back(v.pos);
                    printf("     %5.1f mm skin %u bind (%.3f %.3f %.3f) bones %d:%d %d:%d\n", pk.first * 1000.f, pk.second, v.pos.x, v.pos.y, v.pos.z, v.bones[0], v.weights[0], v.bones[1], v.weights[1]);
                    if (++shownN >= atoi(getenv("SHOW"))) break;
                }
            }
            if (getenv("FLIPS")) {
                int flips = 0;
                for (size_t t = 0; t + 2 < dressed.indices.size(); t += 3) {
                    u32 a = dressed.indices[t], b = dressed.indices[t + 1], c2 = dressed.indices[t + 2];
                    if (!isCloth[a] || !isCloth[b] || !isCloth[c2]) continue;
                    vec3 fn = cross(Q[b] - Q[a], Q[c2] - Q[a]);
                    vec3 bn = cross(dressed.verts[b].pos - dressed.verts[a].pos, dressed.verts[c2].pos - dressed.verts[a].pos);
                    vec3 nsum = unpackNormalOct(dressed.verts[a].normal) + unpackNormalOct(dressed.verts[b].normal) + unpackNormalOct(dressed.verts[c2].normal);
                    bool bindOk = dot(bn, nsum) > 0.f;
                    bool poseOk = dot(fn, QN[a] + QN[b] + QN[c2]) > 0.f;
                    if (bindOk && !poseOk) {
                        flips++;
                        if (flips <= 12) printf("   flipped tri %zu bind (%.3f %.3f %.3f) mat %u bones %d:%d %d:%d\n", t, dressed.verts[a].pos.x, dressed.verts[a].pos.y, dressed.verts[a].pos.z, dressed.verts[a].mat & 0xffu, dressed.verts[a].bones[0], dressed.verts[a].weights[0], dressed.verts[a].bones[1], dressed.verts[a].weights[1]);
                    }
                }
                printf("   %s: %d flipped cloth triangles\n", pd.name, flips);
            }
            if (getenv("DBGKNEE")) {
                // brute force: skin near the knee front vs every cloth triangle
                for (u32 si = 0; si < (u32)dressed.verts.size(); si++) {
                    const VtxSkinned& sv = dressed.verts[si];
                    if ((sv.mat & 0xffu) != MAT_SKIN || sv.pos.z < 0.0f) continue;
                    vec3 sp = Q[si];
                    float bestD = -1.f; int bestT = -1; float bestBind = 0.f, bu0 = 0, bv0 = 0, bw0 = 0; float nd = 0;
                    for (size_t t = 0; t + 2 < dressed.indices.size(); t += 3) {
                        u32 a = dressed.indices[t], b = dressed.indices[t + 1], c2 = dressed.indices[t + 2];
                        if (!isCloth[a] || !isCloth[b] || !isCloth[c2]) continue;
                        vec3 fn = cross(Q[b] - Q[a], Q[c2] - Q[a]);
                        if (length2(fn) < 1e-14f) continue;
                        fn = normalize(fn);
                        if (dot(fn, QN[a] + QN[b] + QN[c2]) < 0.f) fn = -fn;
                        float dd = dot(sp - Q[a], fn);
                        vec3 pp = sp - fn * dd;
                        vec3 v0 = Q[b] - Q[a], v1 = Q[c2] - Q[a], v2 = pp - Q[a];
                        float d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
                        float den = d00 * d11 - d01 * d01;
                        if (fabsf(den) < 1e-18f) continue;
                        float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den, bu = 1.f - bv - bw;
                        if (bu < -0.02f || bv < -0.02f || bw < -0.02f) continue;
                        if (fabsf(dd) > 0.08f || dot(fn, QN[si]) < 0.f) continue;
                        if (dd > bestD) { bestD = dd; bestT = (int)t; bu0 = bu; bv0 = bv; bw0 = bw; nd = dot(fn, QN[si]);
                            const vec3 A = dressed.verts[a].pos, B = dressed.verts[b].pos, C = dressed.verts[c2].pos;
                            vec3 bn = normalize(cross(B - A, C - A));
                            if (dot(bn, unpackNormalOct(dressed.verts[a].normal) + unpackNormalOct(dressed.verts[b].normal) + unpackNormalOct(dressed.verts[c2].normal)) < 0.f) bn = -bn;
                            bestBind = dot(sv.pos - A, bn); }
                    }
                    if (bestD > 0.002f) printf("   knee skin %u bind (%.3f %.3f %.3f) bone %d: in front of tri %d by %.1f mm (bind %.1f mm, n.n %.2f)\n", si, sv.pos.x, sv.pos.y, sv.pos.z, sv.bones[0], bestT, bestD * 1000.f, bestBind * 1000.f, nd);
                }
            }
            if (getenv("SHOW") && !st.pokes.empty()) {
                std::sort(st.pokes.begin(), st.pokes.end(), [](const std::pair<float, u32>& x, const std::pair<float, u32>& y) { return x.first > y.first; });
                printf("   %s POKE: %zu\n", pd.name, st.pokes.size());
                std::vector<vec3> taken;
                int shownN = 0;
                for (auto& pk : st.pokes) {
                    const VtxSkinned& v = dressed.verts[pk.second];
                    bool near = false;
                    for (vec3 t : taken)
                        if (length(t - v.pos) < 0.04f) near = true;
                    if (near) continue;
                    taken.push_back(v.pos);
                    printf("     %5.1f mm skin bind (%.3f %.3f %.3f) bones %d:%d %d:%d\n", pk.first * 1000.f, v.pos.x, v.pos.y, v.pos.z, v.bones[0], v.weights[0], v.bones[1], v.weights[1]);
                    if (++shownN >= atoi(getenv("SHOW"))) break;
                }
            }
        }
        // print worst spots
        if (getenv("SHOW")) {
            int show = atoi(getenv("SHOW"));
            for (int mode = 0; mode < 2; mode++) {
                std::vector<Hit>& hits = mode ? st.body : st.vis;
                const Surf& S = body;
                std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.depth > b.depth; });
                if (hits.empty()) continue;
                printf("   %s %s: %zu\n", pd.name, mode ? "BODY" : "VIS", hits.size());
                std::vector<vec3> taken;
                int shown = 0;
                for (const Hit& h : hits) {
                    const VtxSkinned& v = dressed.verts[h.vi];
                    bool near = false;
                    for (vec3 t : taken)
                        if (length(t - v.pos) < 0.04f) near = true;
                    if (near) continue;
                    taken.push_back(v.pos);
                    int cnt = 0;
                    for (const Hit& h2 : hits)
                        if (length(dressed.verts[h2.vi].pos - v.pos) < 0.04f) cnt++;
                    printf("     %5.1f mm bind (%.3f %.3f %.3f) mat %u param %x col %08x bones %d:%d %d:%d %d:%d | skin bone %d bind (%.3f %.3f %.3f) [%d]\n",
                           h.depth * 1000.f, v.pos.x, v.pos.y, v.pos.z, v.mat & 0xffu, v.mat >> 8, v.color, v.bones[0], v.weights[0], v.bones[1],
                           v.weights[1], v.bones[2], v.weights[2], S.PB[h.si], S.B[h.si].x, S.B[h.si].y, S.B[h.si].z, cnt);
                    if (++shown >= show) break;
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    if (getenv("TOLV")) tolVis = (float)atof(getenv("TOLV"));
    if (getenv("TOLB")) tolBody = (float)atof(getenv("TOLB"));
    if (getenv("TANG")) tangMax = (float)atof(getenv("TANG"));
    const char* onlyPose = getenv("POSE");
    struct Case {
        u32 seed;
        int role;
        int top, bottom, outer, bag;
        float weight;
        const char* what;
    };
    std::vector<Case> cases;
    if (argc > 1) {
        cases.push_back({(u32)atoi(argv[1]), argc > 2 ? atoi(argv[2]) : 0, argc > 3 ? atoi(argv[3]) : -2, argc > 4 ? atoi(argv[4]) : -2,
                         argc > 5 ? atoi(argv[5]) : -2, argc > 6 ? atoi(argv[6]) : -2, argc > 7 ? (float)atof(argv[7]) : -1.f, "arg"});
    } else {
        for (int role = 0; role < 7; role++)
            for (int k = 0; k < 3; k++) cases.push_back({7000u + (u32)role * 131u + (u32)k * 977u, role, -2, -2, -2, -2, -1.f, "random"});
        const int outerTops[6] = {TOP_TSHIRT, TOP_TSHIRT, TOP_BLOUSE, TOP_TSHIRT, TOP_TANK, TOP_DRESS_SHIRT};
        for (int oc = 0; oc < OUT_COUNT; oc++) cases.push_back({8100u + (u32)oc * 17u, 0, outerTops[oc], BOT_JEANS, oc, -1, -1.f, "outer"});
        for (int b = 0; b < BAG_COUNT; b++) cases.push_back({8300u + (u32)b * 29u, 0, TOP_TSHIRT, BOT_SHORTS, -1, b, -1.f, "bag"});
        for (int k = 0; k < 6; k++) cases.push_back({8500u + (u32)k * 53u, 0, k < 3 ? TOP_TSHIRT : TOP_SUNDRESS, BOT_SKIRT, -1, -1, -1.f, "skirt"});
        for (int k = 0; k < 4; k++)
            cases.push_back({8700u + (u32)k * 71u, k & 1 ? 3 : 0, k < 2 ? TOP_TSHIRT : TOP_DRESS_SHIRT, k & 1 ? BOT_SLACKS : BOT_JEANS, -2, -2, 0.95f, "heavy"});
    }
    long tn = 0, tv = 0, tb = 0;
    for (const Case& cs : cases) {
        CharacterDesc d = randomCharacter(cs.seed, cs.role);
        if (cs.top != -2) d.top = cs.top;
        if (cs.bottom != -2) d.bottom = cs.bottom;
        if (cs.outer != -2) d.outer = cs.outer;
        if (cs.bag != -2) d.bag = cs.bag;
        if (cs.weight >= 0.f) d.weight = cs.weight;
        if (getenv("MUSCLE")) d.muscle = (float)atof(getenv("MUSCLE"));
        if (getenv("AGE")) d.age = (float)atof(getenv("AGE"));
        if (getenv("GENDER")) d.gender = atoi(getenv("GENDER")) ? FEMALE : MALE;
        if (d.top == TOP_SUNDRESS || d.bottom == BOT_SKIRT) d.gender = FEMALE;
        printf("%-6s seed %u role %d top %d bottom %d outer %d bag %d w %.2f m %.2f h %.2f g %d shoes %d hat %d:", cs.what, cs.seed, cs.role, d.top, d.bottom,
               outerFits(d) ? d.outer : -1, d.bag, d.weight, d.muscle, d.height, (int)d.gender, d.shoes, d.hat);
        if (getenv("SHOW")) printf("\n");
        CaseResult R;
        analyze(d, R, onlyPose);
        for (int pi = 0; pi < kNumPoses; pi++) {
            const PoseStat& st = R.ps[pi];
            if (!st.n) continue;
            printf(" %s %d/%d/%d(%.0f,%.0f,%.0f)", kPoses[pi].name, st.overVis, st.overBody, st.xTris, st.worstVis * 1000.f, st.worstBody * 1000.f, st.worstX * 1000.f);
            if (strcmp(kPoses[pi].name, "sit")) {
                tn += st.n;
                tv += st.overVis;
                tb += st.overBody;
            }
        }
        printf("\n");
    }
    printf("TOTAL %ld tests: visible poke-through > %.0f mm: %.3f%%, body collapse > %.0f mm: %.3f%%\n", tn, tolVis * 1000.f, 100.0 * tv / Max(tn, 1L),
           tolBody * 1000.f, 100.0 * tb / Max(tn, 1L));
    return 0;
}
