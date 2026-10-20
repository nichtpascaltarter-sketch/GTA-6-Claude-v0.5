// lmviz: strips of the locomotion scenarios with the real character meshes (preview.cpp's rasterizer): a row of
// tiles at given times, a fixed camera per scenario, the ground as a checkerboard, and the feet's contact marks so far
// (dark = held still, red = slid more than 1 cm per frame over the ground).
// Build: g++ -O2 -std=c++17 -I <tree> lmviz.cpp -o lv
// Run:   lv <scenario> out.ppm [--n tiles] [--t0 s] [--dt s] [--who k] [--w px] [--h px] [--cam x,y,z] [--look x,y,z]
//        scenarios: turn90 turn180 start stop jogstop pass curb hill stairs spot90
#define PREVIEW_NO_MAIN
#include "tests/anim/preview.cpp"
#include <memory>
#include <functional>
#include "locosim.h"

struct Mark {
    vec3 p;
    bool slid;
};

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    const char* sc = argv[1];
    const char* out = argv[2];
    int n = 8, who = 0, tw = 300, th = 420;
    float t0 = 2.9f, dt = 0.1f;
    vec3 camEye(0), camLook(0);
    bool camSet = false;
    for (int i = 3; i < argc; i++) {
        auto nx = [&]() { return i + 1 < argc ? argv[++i] : "0"; };
        if (!strcmp(argv[i], "--n")) n = atoi(nx());
        else if (!strcmp(argv[i], "--t0")) t0 = (float)atof(nx());
        else if (!strcmp(argv[i], "--dt")) dt = (float)atof(nx());
        else if (!strcmp(argv[i], "--who")) who = atoi(nx());
        else if (!strcmp(argv[i], "--w")) tw = atoi(nx());
        else if (!strcmp(argv[i], "--h")) th = atoi(nx());
        else if (!strcmp(argv[i], "--cam")) { sscanf(nx(), "%f,%f,%f", &camEye.x, &camEye.y, &camEye.z); camSet = true; }
        else if (!strcmp(argv[i], "--look")) sscanf(nx(), "%f,%f,%f", &camLook.x, &camLook.y, &camLook.z);
    }
    Terrain T;
    const bool two = !strcmp(sc, "pass");
    if (!strcmp(sc, "curb")) T.kind = 4;
    if (!strcmp(sc, "hill")) T.kind = 1, T.a = 10.f * kPi / 180.f;
    if (!strcmp(sc, "stairs")) T.kind = 3;
    std::unique_ptr<Person> P[2];
    P[0] = makePerson(kPeople[who % kNP], kPeople[who % kNP].seed * 7u + 41u);
    if (two) {
        P[1] = makePerson(kPeople[(who + 1) % kNP], 202u);
        P[0]->pos = vec3(0.f, 0.f, 0.f);
        P[1]->pos = vec3(0.35f, 14.f, 0.f);
        P[1]->yaw = kPi;
        P[0]->vel = vec2(0.f, 1.35f);
        P[1]->vel = vec2(0.f, -1.4f);
    }
    const int np = two ? 2 : 1;
    SkinnedMeshData mesh[2];
    for (int k = 0; k < np; k++) buildCharacterMeshLod(P[k]->d, P[k]->sk, 1, mesh[k]);
    std::vector<Mark> marks;
    FootW prevF[2][2];
    bool prevDown[2][2] = {{false, false}, {false, false}};
    Img img(tw * n, th);
    const float tEnd = t0 + dt * (n - 1) + 1e-3f;
    int tile = 0;
    for (int f = 0; f <= (int)(tEnd / kDt) + 1 && tile < n; f++) {
        float t = f * kDt;
        // the scenario's controls (as in locometer)
        for (int k = 0; k < np; k++) {
            Person& p = *P[k];
            vec2 des(0.f);
            const float* face = nullptr;
            float faceYaw = 0.f;
            if (!strcmp(sc, "turn90") || !strcmp(sc, "turn180")) {
                float a = (!strcmp(sc, "turn90") ? 90.f : 180.f) * kPi / 180.f;
                des = (t < 3.f ? vec2(0.f, 1.f) : vec2(-sinf(a), cosf(a))) * 1.4f;
            } else if (!strcmp(sc, "start")) {
                des = t >= 3.f ? vec2(0.f, 1.4f) : vec2(0.f);
            } else if (!strcmp(sc, "stop")) {
                des = t < 3.f ? vec2(0.f, 1.4f) : vec2(0.f);
            } else if (!strcmp(sc, "jogstop")) {
                des = t < 3.f ? vec2(0.f, 3.f) : vec2(0.f);
            } else if (!strcmp(sc, "spot90")) {
                faceYaw = t >= 3.f ? kHalfPi : 0.f;
                face = &faceYaw;
            } else if (two) {
                vec2 pos(p.pos.x, p.pos.y);
                const Person& o = *P[1 - k];
                float dirY = k ? -1.f : 1.f, lat = k ? 0.35f : 0.f, spd = k ? 1.4f : 1.35f;
                vec2 tgt(lat, p.pos.y + 1.6f * dirY), d = normalize(tgt - pos) * spd;
                vec2 rel = pos - vec2(o.pos.x, o.pos.y);
                float dist = length(rel);
                vec2 push(0.f);
                if (dist < 3.5f && dist > 1e-3f) {
                    vec2 nn = rel / dist;
                    push = push + nn * (1.6f * expf(-(dist - 0.6f) / 0.35f));
                    vec2 rv = d - o.vel;
                    if (dot(rv, -nn) > 0.3f && dist < 2.5f) {
                        vec2 mv = normalize(d + vec2(1e-3f, 0.f));
                        push = push + vec2(mv.y, -mv.x) * 0.45f;
                    }
                }
                d = d + push;
                float m = length(d), cap = Max(spd * 1.35f, 1.8f);
                if (m > cap) d = d * (cap / m);
                des = d;
            } else {
                des = vec2(0.f, Min(1.4f, t * 11.f));
            }
            control(p, des, face, 6.f, T);
        }
        for (int k = 0; k < np; k++) {
            if (two) {
                // the game's passBy (peds.cpp animatePed): the other person closing within a metre
                Person& p = *P[k];
                const Person& o = *P[1 - k];
                p.in.passWeight = 0.f;
                vec2 d2(o.pos.x - p.pos.x, o.pos.y - p.pos.y), rv = o.vel - p.vel;
                float dist = length(d2), rv2 = length2(rv);
                float tca = rv2 > 1e-3f ? Clamp(-dot(d2, rv) / rv2, 0.f, 1.5f) : 0.f;
                if (dist < 2.6f && length(d2 + rv * tca) <= 1.f) {
                    vec2 fwd(-sinf(p.yaw), cosf(p.yaw)), rightV(cosf(p.yaw), sinf(p.yaw));
                    vec3 c = vec3(d2.x, d2.y, 0.f) + rotate(qz(o.yaw), o.m[B_CHEST].c[3].xyz());
                    p.in.passBy = vec3(dot(vec2(c.x, c.y), rightV), dot(vec2(c.x, c.y), fwd), c.z);
                    p.in.passVel = vec2(dot(rv, rightV), dot(rv, fwd));
                    float kk = Saturate((2.4f - dist) / 1.3f);
                    p.in.passWeight = kk * kk * (3.f - 2.f * kk);
                }
            }
            animate(*P[k], T);
            // contact marks
            for (int s = 0; s < 2; s++) {
                FootW fw;
                footWorld(*P[k], T, s, fw);
                bool down = false;
                for (int q = 0; q < 2; q++) down = down || fw.p[q].z - fw.gz[q] < 0.008f;
                if (down) {
                    float mv = prevDown[k][s] ? Min(length(vec2(fw.p[0].x - prevF[k][s].p[0].x, fw.p[0].y - prevF[k][s].p[0].y)),
                                                    length(vec2(fw.p[1].x - prevF[k][s].p[1].x, fw.p[1].y - prevF[k][s].p[1].y)))
                                                : 0.f;
                    if (f % 2 == 0 || mv > 0.01f) marks.push_back({(fw.p[0] + fw.p[1]) * 0.5f, mv > 0.01f});
                }
                prevF[k][s] = fw;
                prevDown[k][s] = down;
            }
        }
        if (t + 1e-4f < t0 + dt * tile) continue;
        // render this tile
        Img im(tw * 2, th * 2);
        Cam cam;
        cam.fov = 34.f;
        vec3 focus = P[0]->pos;
        if (two) focus = (P[0]->pos + P[1]->pos) * 0.5f;
        vec3 look = camLook + vec3(0.f, 0.f, 0.f);
        if (!camSet) {
            cam.target = focus + vec3(0.f, 0.f, 0.85f);
            cam.eye = cam.target + normalize(vec3(1.0f, -0.6f, 0.75f)) * 4.6f;
        } else {
            cam.eye = camEye;
            cam.target = look;
        }
        cam.setup(im.w, im.h);
        // ground: checkerboard around the focus at the terrain's height
        {
            std::vector<vec3> Pp, Nn, Aa;
            std::vector<u32> Mm, Ii;
            const float tl = 0.25f;
            float cx = floorf(focus.x / tl) * tl, cy = floorf(focus.y / tl) * tl;
            for (float y = cy - 3.f; y < cy + 3.f; y += tl)
                for (float x = cx - 3.f; x < cx + 3.f; x += tl) {
                    int kk = (int)floorf(x / tl + 1000.f) + (int)floorf(y / tl + 1000.f);
                    vec3 c = (kk & 1) ? vec3(0.32f, 0.3f, 0.28f) : vec3(0.42f, 0.4f, 0.37f);
                    u32 b = (u32)Pp.size();
                    const float xs[4] = {x, x + tl, x + tl, x}, ys[4] = {y, y, y + tl, y + tl};
                    for (int q = 0; q < 4; q++) {
                        Pp.push_back(vec3(xs[q], ys[q], T.h(xs[q] * 0.999f + x * 0.001f, ys[q] * 0.999f + y * 0.001f)));
                        Nn.push_back(T.n(x + tl * 0.5f, y + tl * 0.5f));
                        Aa.push_back(c);
                        Mm.push_back(MAT_CLOTH);
                    }
                    Ii.push_back(b); Ii.push_back(b + 1); Ii.push_back(b + 2);
                    Ii.push_back(b); Ii.push_back(b + 2); Ii.push_back(b + 3);
                }
            // contact marks: small squares just over the ground
            for (const Mark& mk : marks) {
                u32 b = (u32)Pp.size();
                const float r = 0.018f;
                vec3 c = mk.slid ? vec3(0.9f, 0.08f, 0.05f) : vec3(0.05f, 0.25f, 0.6f);
                const float xs[4] = {-r, r, r, -r}, ys[4] = {-r, -r, r, r};
                for (int q = 0; q < 4; q++) {
                    float x = mk.p.x + xs[q], y = mk.p.y + ys[q];
                    Pp.push_back(vec3(x, y, T.h(x, y) + 0.004f));
                    Nn.push_back(vec3(0, 0, 1));
                    Aa.push_back(c);
                    Mm.push_back(MAT_CLOTH);
                }
                Ii.push_back(b); Ii.push_back(b + 1); Ii.push_back(b + 2);
                Ii.push_back(b); Ii.push_back(b + 2); Ii.push_back(b + 3);
            }
            drawMesh(im, cam, Pp, Nn, Aa, Mm, Ii);
        }
        for (int k = 0; k < np; k++) {
            Person& p = *P[k];
            mat4 ms[B_COUNT], skin[B_COUNT];
            computeMatrices(p.sk, p.an.pose, ms, skin);
            const SkinnedMeshData& me = mesh[k];
            std::vector<vec3> Pv(me.verts.size()), Nv(me.verts.size()), Av(me.verts.size());
            std::vector<u32> Mv(me.verts.size());
            quat qy = qz(p.yaw);
            for (size_t v = 0; v < me.verts.size(); v++) {
                const VtxSkinned& vx = me.verts[v];
                mat4 m;
                for (int c = 0; c < 4; c++) m.c[c] = vec4(0);
                for (int c = 0; c < 4; c++) {
                    float w = vx.weights[c] / 255.f;
                    if (w <= 0) continue;
                    const mat4& s = skin[vx.bones[c]];
                    for (int q = 0; q < 4; q++) m.c[q] = m.c[q] + s.c[q] * w;
                }
                Pv[v] = rotate(qy, transformPoint(m, vx.pos)) + p.pos;
                Nv[v] = rotate(qy, normalize(transformDir(m, unpackNormalOct(vx.normal))));
                Av[v] = matAlbedo(vx.mat, unpackRGBA8(vx.color).xyz());
                Mv[v] = vx.mat;
            }
            drawMesh(im, cam, Pv, Nv, Av, Mv, me.indices);
        }
        img.blit(im.down(2), tile * tw);
        printf("tile %d t=%.2f\n", tile, t);
        tile++;
    }
    img.save(out);
    return 0;
}
