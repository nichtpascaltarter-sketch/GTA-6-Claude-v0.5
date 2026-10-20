// Ear canal check. usage: earray [p0|p1|index]...  (index = the viewer's people: randomCharacter(1000 + i * 7919,
// i % 7); hats off). The canal = the ring round the ear's cap vertex (the ear vertex that fans out to 20+ triangles).
// Parallel rays from 15 directions (yaw -40..40 from the side, pitch -20/0/20; camera 0.3 m away) on a 0.2 mm grid
// within 3.5 mm of the canal's centre, against every triangle but hair (back faces culled, as skin is drawn). A ray
// whose first hit is not the ear's own surface shows the head (or the background) through the canal. Also prints the
// mean linear albedo luminance of the ear's front against the skin of the cheek in front of it.
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
#ifndef HAVE_CHARACTERS
#define HAVE_CHARACTERS 1
#endif
#include "src/game/protagonists.h"
using namespace Anim;
struct T3 { vec3 a, e1, e2, n; int reg; };
static bool hitTri(const T3& t, vec3 o, vec3 d, float& tt) {
    if (dot(t.n, d) >= 0.f) return false;   // back face (or edge-on): culled
    vec3 p = cross(d, t.e2);
    float det = dot(t.e1, p);
    if (fabsf(det) < 1e-14f) return false;
    float inv = 1.f / det;
    vec3 s = o - t.a;
    float u = dot(s, p) * inv;
    if (u < 0.f || u > 1.f) return false;
    vec3 q = cross(s, t.e1);
    float v = dot(d, q) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    tt = dot(t.e2, q) * inv;
    return tt > 0.f;
}
static float lumOf(u32 c) {
    auto lin = [](float x) { return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f); };
    (void)lin;
    // vertex colours are linear albedo packed RGBA8
    return 0.2126f * (c & 255) / 255.f + 0.7152f * ((c >> 8) & 255) / 255.f + 0.0722f * ((c >> 16) & 255) / 255.f;
}
int main(int argc, char** argv) {
    int allRays = 0, allBad = 0;
    for (int ai = 1; ai < argc; ai++) {
        CharacterDesc d = argv[ai][0] == 'p' ? Game::protagonistDesc(atoi(argv[ai] + 1)) : randomCharacter(1000 + atoi(argv[ai]) * 7919, atoi(argv[ai]) % 7);
        d.hat = -1;
        Skeleton sk;
        buildSkeleton(d, sk);
        SkinnedMeshData m;
        buildCharacterMesh(d, sk, m);
        auto regOf = [&](const VtxSkinned& v) { return (v.mat & 0xffu) == MAT_SKIN ? (int)((v.mat >> 9) & 7u) : -1 - (int)(v.mat & 0xffu); };
        std::vector<T3> tris;
        std::vector<int> fan(m.verts.size(), 0);
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const VtxSkinned &A = m.verts[m.indices[i]], &B = m.verts[m.indices[i + 1]], &C = m.verts[m.indices[i + 2]];
            if ((A.mat & 0xffu) == MAT_HAIR) continue;
            T3 t;
            t.a = A.pos;
            t.e1 = B.pos - A.pos;
            t.e2 = C.pos - A.pos;
            t.n = cross(t.e1, t.e2);
            t.reg = regOf(A);
            tris.push_back(t);
            if (t.reg == 4)
                for (int k = 0; k < 3; k++) fan[m.indices[i + k]]++;
        }
        for (int s = 0; s < 2; s++) {
            const float sx = s ? 1.f : -1.f;
            int capV = -1;
            for (size_t i = 0; i < m.verts.size(); i++)
                if (fan[i] >= 20 && regOf(m.verts[i]) == 4 && m.verts[i].pos.x * sx > 0.f) capV = (int)i;
            if (capV < 0) { printf("%s ear %d: no canal cap found\n", argv[ai], s); continue; }
            // ring 0 = the cap's neighbours
            vec3 canal(0);
            int nr = 0;
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
                for (int k = 0; k < 3; k++)
                    if ((int)m.indices[i + k] == capV) {
                        canal += m.verts[m.indices[i + (k + 1) % 3]].pos;
                        nr++;
                    }
            canal = canal * (1.f / Max(nr, 1));
            vec3 capP = m.verts[capV].pos;
            // ear and cheek albedo (cheek: skin of region 0 on this side 5-8 cm in front of the canal, within 2 cm of
            // its height, the outer half of the face)
            double eS = 0, cS = 0;
            int eN = 0, cN = 0;
            for (const VtxSkinned& v : m.verts) {
                if ((v.mat & 0xffu) != MAT_SKIN || v.pos.x * sx <= 0.f) continue;
                vec3 dq = v.pos - canal;
                if (regOf(v) == 4) { eS += lumOf(v.color); eN++; }
                else if (regOf(v) == 0 && dq.y > 0.05f && dq.y < 0.08f && fabsf(dq.z + 0.01f) < 0.02f && fabsf(v.pos.x) > 0.035f) { cS += lumOf(v.color); cN++; }
            }
            int totRays = 0, totBad = 0, worst = 0, worstMiss = 0;
            char worstTag[64] = "";
            for (int yi = 0; yi < 5; yi++)
                for (int pi = 0; pi < 3; pi++) {
                    float yaw = (-40.f + 20.f * yi) * kDegToRad, pitch = (-20.f + 20.f * pi) * kDegToRad;
                    vec3 cdir(sx * cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch));
                    vec3 dir = -cdir;
                    vec3 u = normalize(cross(vec3(0, 0, 1), cdir));
                    vec3 v = cross(cdir, u);
                    const float R = 0.0035f;
                    std::vector<const T3*> cand;
                    for (const T3& t : tris) {
                        vec3 P[3] = {t.a, t.a + t.e1, t.a + t.e2};
                        float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
                        for (int k = 0; k < 3; k++) {
                            float pu = dot(P[k] - canal, u), pv = dot(P[k] - canal, v);
                            u0 = Min(u0, pu); u1 = Max(u1, pu); v0 = Min(v0, pv); v1 = Max(v1, pv);
                        }
                        if (u1 < -R || u0 > R || v1 < -R || v0 > R) continue;
                        cand.push_back(&t);
                    }
                    int bad = 0, miss = 0;
                    const bool map = getenv("EARRAY_MAP") && yi == atoi(getenv("EARRAY_MAP")) / 10 && pi == atoi(getenv("EARRAY_MAP")) % 10;
                    std::string mapS;
                    for (int gy = 17; gy >= -17; gy--, mapS += map ? "\n" : "")
                        for (int gx = -17; gx <= 17; gx++) {
                            float pu = gx * 0.0002f, pv = gy * 0.0002f;
                            if (pu * pu + pv * pv > R * R) { if (map) mapS += ' '; continue; }
                            vec3 o = canal + cdir * 0.3f + u * pu + v * pv;
                            float best = 1e9f;
                            int reg = 99;
                            for (const T3* t : cand) {
                                float tt;
                                if (hitTri(*t, o, dir, tt) && tt < best) { best = tt; reg = t->reg; }
                            }
                            totRays++;
                            if (reg != 4) { bad++; miss += reg == 99; }
                            if (map) mapS += reg == 4 ? (fabsf(best - 0.3f) < 0.003f ? '.' : ':') : (reg == 99 ? 'o' : '#');
                        }
                    if (map) printf("%s ear %d yaw %+.0f pitch %+.0f (u right, v up; '.' ear within 3 mm of the canal's depth, ':' ear, '#' other):\n%s", argv[ai], s, yaw * kRadToDeg, pitch * kRadToDeg, mapS.c_str());
                    totBad += bad;
                    if (bad > worst) { worst = bad; worstMiss = miss; snprintf(worstTag, sizeof worstTag, "yaw %+.0f pitch %+.0f", yaw * kRadToDeg, pitch * kRadToDeg); }
                }
            allRays += totRays;
            allBad += totBad;
            printf("%-4s ear %d: cap %.1f mm off the canal ring's centre; head through the canal %5.2f%% of rays (worst %s: %d, %d miss); albedo ear/cheek %.2f (%d / %d verts)\n",
                   argv[ai], s, 1000.f * length(capP - canal), 100.f * totBad / Max(totRays, 1), worst ? worstTag : "-", worst, worstMiss, (eS / Max(eN, 1)) / Max(cS / Max(cN, 1), 1e-6), eN, cN);
        }
    }
    printf("all: head through the canal %.2f%% of %d rays\n", 100.f * allBad / Max(allRays, 1), allRays);
}
