// Native (Linux) character preview: builds characters with the real mesh/animation code and renders them with a
// small software rasterizer (z-buffer, backface culling, simple lighting) into a PPM image.
// Build: g++ -O2 -std=c++17 -I src tests/anim/preview.cpp -o /tmp/preview
// Usage: preview out.ppm [--seed S] [--role R] [--count N] [--view front|side|back|face|three|top] [--clip C] [--t T]
//                        [--w W] [--h H] [--dist D] [--yaw deg] [--height z] [--fov deg] [--mode lineup|single]
#include "../../src/core/math.cpp"
#include "../../src/render/mesh.cpp"
#include "../../src/anim/anim_all.cpp"
#include "../../tools/native_stubs.cpp"

using namespace Anim;
using Anim::detail::mulColor;

struct Img {
    int w, h;
    std::vector<vec3> c;
    std::vector<float> z;
    Img(int W, int H) : w(W), h(H), c((size_t)W * H, vec3(0.62f, 0.7f, 0.8f)), z((size_t)W * H, 1e30f) {
        for (int y = 0; y < h; y++) {
            float t = (float)y / h;
            for (int x = 0; x < w; x++) c[(size_t)y * w + x] = lerp(vec3(0.55f, 0.65f, 0.8f), vec3(0.8f, 0.78f, 0.72f), t);
        }
    }
    void save(const char* path) {
        FILE* f = fopen(path, "wb");
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (size_t i = 0; i < c.size(); i++) {
            vec3 v = c[i];
            unsigned char px[3];
            for (int k = 0; k < 3; k++) {
                float x = Saturate(v[k]);
                x = x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.f / 2.4f) - 0.055f;
                px[k] = (unsigned char)(Saturate(x) * 255.f + 0.5f);
            }
            fwrite(px, 1, 3, f);
        }
        fclose(f);
    }
};

static vec3 matAlbedo(u32 mat, vec3 col) {
    switch (mat & 0xff) {
        case MAT_SKIN: return col;
        case MAT_HAIR: return col * 0.9f;
        case MAT_CLOTH: case MAT_FABRIC: return col * 0.85f;
        case MAT_DENIM: return mulColor(col, vec3(0.1f, 0.155f, 0.31f));
        case MAT_LEATHER: return mulColor(col, vec3(0.11f, 0.065f, 0.037f));
        case MAT_EYE: return col * 0.85f;
        case MAT_RUBBER: return col * 0.03f;
        case MAT_PLASTIC: return col * 0.034f;
        case MAT_METAL_PAINTED: return col * 0.6f;
        case MAT_CHROME: return col * 0.7f;
        case MAT_CAR_GLASS: return vec3(0.02f);
        case MAT_EMISSIVE: return col;
        default: return col * 0.7f;
    }
}

struct Cam {
    vec3 eye, target;
    float fov = 30.f;
    mat4 view, proj;
    void setup(int w, int h) {
        view = lookAtRH(eye, target, vec3(0, 0, 1));
        float f = 1.f / tanf(fov * kDegToRad * 0.5f);
        float a = (float)w / h;
        proj = mat4(vec4(f / a, 0, 0, 0), vec4(0, f, 0, 0), vec4(0, 0, -1, -1), vec4(0, 0, -0.1f, 0));
    }
};

static void drawMesh(Img& img, const Cam& cam, const std::vector<vec3>& P, const std::vector<vec3>& N, const std::vector<vec3>& A,
                     const std::vector<u32>& mats, const std::vector<u32>& idx) {
    std::vector<vec3> sp(P.size());
    std::vector<float> vz(P.size());
    for (size_t i = 0; i < P.size(); i++) {
        vec4 v = cam.view * vec4(P[i], 1.f);
        vec4 c = cam.proj * v;
        float iw = 1.f / c.w;
        sp[i] = vec3((c.x * iw * 0.5f + 0.5f) * img.w, (0.5f - c.y * iw * 0.5f) * img.h, -v.z);
        vz[i] = -v.z;
    }
    vec3 L1 = normalize(vec3(0.35f, 0.75f, 0.65f)), L2 = normalize(vec3(-0.6f, 0.3f, 0.2f));
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        u32 i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        if (vz[i0] < 0.1f || vz[i1] < 0.1f || vz[i2] < 0.1f) continue;
        vec3 a = sp[i0], b = sp[i1], c = sp[i2];
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (area >= 0.f) continue;   // backface (screen y down => CCW front has negative area)
        int x0 = Max(0, (int)floorf(Min(a.x, Min(b.x, c.x)))), x1 = Min(img.w - 1, (int)ceilf(Max(a.x, Max(b.x, c.x))));
        int y0 = Max(0, (int)floorf(Min(a.y, Min(b.y, c.y)))), y1 = Min(img.h - 1, (int)ceilf(Max(a.y, Max(b.y, c.y))));
        if (x0 > x1 || y0 > y1) continue;
        if (getenv("PREVIEW_DEBUG") && (x1 - x0 > 150 || y1 - y0 > 150))
            printf("big tri %zu: (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) mat %u\n", t / 3, P[i0].x, P[i0].y, P[i0].z, P[i1].x, P[i1].y, P[i1].z, P[i2].x, P[i2].y, P[i2].z, mats[i0] & 0xff);
        float ia = 1.f / area;
        u32 mat = mats[i0];
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * ia;
                float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * ia;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
                float z = w0 * a.z + w1 * b.z + w2 * c.z;
                size_t o = (size_t)y * img.w + x;
                if (z >= img.z[o]) continue;
                img.z[o] = z;
                vec3 n = normalize(N[i0] * w0 + N[i1] * w1 + N[i2] * w2);
                vec3 alb = A[i0] * w0 + A[i1] * w1 + A[i2] * w2;
                vec3 pos = P[i0] * w0 + P[i1] * w1 + P[i2] * w2;
                vec3 V = normalize(cam.eye - pos);
                bool skinM = (mat & 0xff) == MAT_SKIN;
                float wrapK = skinM ? 0.45f : 0.15f;
                float d1 = Max(0.f, (dot(n, L1) + wrapK) / (1.f + wrapK)), d2 = Max(0.f, dot(n, L2));
                float amb = 0.42f + 0.18f * n.z;
                vec3 col = alb * (d1 * 1.0f + d2 * 0.25f + amb);
                if (skinM) col += mulColor(alb, vec3(0.12f, 0.03f, 0.02f)) * (1.f - Max(0.f, dot(n, L1)));
                vec3 H = normalize(L1 + V);
                float gloss = (mat & 0xff) == MAT_EYE || (mat & 0xff) == MAT_CAR_GLASS ? 200.f : ((mat & 0xff) == MAT_CHROME ? 60.f : 24.f);
                float ks = skinM ? 0.04f : ((mat & 0xff) == MAT_EYE || (mat & 0xff) == MAT_CAR_GLASS ? 0.5f : 0.03f);
                if ((mat & 0xff) == MAT_CHROME) ks = 0.5f;
                col += vec3(powf(Max(0.f, dot(n, H)), gloss) * ks * (dot(n, L1) > 0 ? 1.f : 0.f));
                float rim = powf(1.f - Max(0.f, dot(n, V)), 3.f) * 0.08f;
                col += vec3(rim);
                img.c[o] = col;
            }
    }
}

struct Char {
    CharacterDesc d;
    Skeleton sk;
    SkinnedMeshData mesh;
};

// Held weapon (bat: 1, knife: 2) along the right hand's grip (Anim::handGrip), as a simple lathe / blade mesh.
static void drawWeapon(Img& img, const Cam& cam, int kind, vec3 G, vec3 D, vec3 P) {
    std::vector<vec3> Ps, Ns, As;
    std::vector<u32> Ms, Is;
    D = normalize(D);
    vec3 X = normalize(P - D * dot(P, D)), Y = cross(D, X);
    auto lathe = [&](float a0, float a1, float r0, float r1, vec3 col, int segs) {
        u32 b = (u32)Ps.size();
        for (int k = 0; k <= segs; k++) {
            float th = kTwoPi * k / segs;
            vec3 rd = X * cosf(th) + Y * sinf(th);
            Ps.push_back(G + D * a0 + rd * r0); Ns.push_back(rd); As.push_back(col); Ms.push_back(MAT_PLASTIC);
            Ps.push_back(G + D * a1 + rd * r1); Ns.push_back(rd); As.push_back(col); Ms.push_back(MAT_PLASTIC);
        }
        for (int k = 0; k < segs; k++) {
            u32 i0 = b + k * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
            Is.push_back(i0); Is.push_back(i2); Is.push_back(i1);
            Is.push_back(i1); Is.push_back(i2); Is.push_back(i3);
        }
    };
    if (kind == 1) {
        vec3 wood(0.55f, 0.36f, 0.18f), grip(0.08f, 0.08f, 0.09f);
        lathe(-0.15f, -0.14f, 0.026f, 0.026f, wood, 12);
        lathe(-0.14f, 0.14f, 0.016f, 0.016f, grip, 12);
        lathe(0.14f, 0.36f, 0.016f, 0.02f, wood, 12);
        lathe(0.36f, 0.6f, 0.02f, 0.032f, wood, 12);
        lathe(0.6f, 0.68f, 0.032f, 0.034f, wood, 12);
        lathe(0.68f, 0.69f, 0.034f, 0.0f, wood, 12);
    } else {
        vec3 hcol(0.05f, 0.05f, 0.05f), steel(0.7f, 0.72f, 0.75f);
        lathe(-0.05f, 0.06f, 0.011f, 0.011f, hcol, 8);
        // flat blade in the plane of the handle and the finger side (edge towards -X)
        u32 b = (u32)Ps.size();
        vec3 q[4] = {G + D * 0.06f + X * 0.004f, G + D * 0.06f - X * 0.024f, G + D * 0.19f, G + D * 0.06f};
        for (int k = 0; k < 3; k++) { Ps.push_back(q[k]); Ns.push_back(Y); As.push_back(steel); Ms.push_back(MAT_CHROME); }
        for (int k = 0; k < 3; k++) { Ps.push_back(q[k]); Ns.push_back(-Y); As.push_back(steel); Ms.push_back(MAT_CHROME); }
        Is.push_back(b); Is.push_back(b + 1); Is.push_back(b + 2);
        Is.push_back(b + 3); Is.push_back(b + 5); Is.push_back(b + 4);
    }
    drawMesh(img, cam, Ps, Ns, As, Ms, Is);
}

// Scripted animator inputs for transition checks.
static void runScenario(int sc, float t, AnimInput& in) {
    switch (sc) {
        case 1:   // speed ramp 0 -> 7 -> 0 m/s
            in.speed = t < 7.f ? t : Max(0.f, 14.f - t);
            break;
        case 2:   // walk, stop, aim pistol, fire, reload, walk while aiming (strafe)
            in.speed = t < 2.f ? 1.4f : (t < 6.f ? 0.f : 1.3f);
            in.localMoveDir = t < 6.f ? vec2(0, 1) : vec2(1, 0);
            in.weaponKind = 1;
            in.aiming = t > 2.5f;
            in.firing = t > 3.5f && t < 4.2f;
            in.reloading = t > 4.5f && t < 5.5f;
            in.aimPitch = 0.3f * sinf(t);
            break;
        case 3:   // enter car at 0.5 s (seated at 1.55 s), drive, exit at 4 s
            in.action = t >= 0.5f && t < 0.52f ? CLIP_ENTER_CAR_L : (t >= 4.f && t < 4.02f ? CLIP_EXIT_CAR_L : -1);
            in.stance = t >= 1.55f && t < 4.f ? 1 : 0;
            break;
        case 4:   // jog, jump, fall, land
            in.speed = 3.f;
            in.action = t >= 1.f && t < 1.02f ? CLIP_JUMP_START : (t >= 1.8f && t < 1.82f ? CLIP_LAND : -1);
            in.inAir = t > 1.15f && t < 1.8f;
            break;
        case 5:   // walking with a rifle (carry), then aim
            in.speed = 1.5f;
            in.weaponKind = 2;
            in.aiming = t > 2.f;
            break;
        case 6:   // phone while walking, then standing talking
            in.stance = t < 3.f ? 8 : 7;
            in.speed = t < 3.f ? 1.3f : 0.f;
            break;
        case 7:   // hit reactions while walking, then punches standing
            in.speed = t < 2.f ? 1.4f : 0.f;
            in.action = (t >= 0.8f && t < 0.82f) ? CLIP_HIT_FRONT : ((t >= 2.5f && t < 2.52f) ? CLIP_PUNCH_R : -1);
            break;
        case 9:   // turning on the spot
            in.turnRate = t < 3.f ? 2.5f : -2.5f;
            break;
        case 8:   // driving with steering input in localMoveDir.x
            in.stance = 1;
            in.localMoveDir = vec2(sinf(t * 1.5f), 1.f);
            break;
        case 10:   // fighting guard standing: hook at 0.5 s, uppercut at 1.6 s
            in.stance = 19;
            in.action = (t >= 0.5f && t < 0.52f) ? CLIP_HOOK : ((t >= 1.6f && t < 1.62f) ? CLIP_UPPERCUT : -1);
            break;
        case 11:   // fighting guard strafing right, hook at 0.8 s (upper body over the strafe)
            in.stance = 19;
            in.speed = 1.3f;
            in.localMoveDir = vec2(1.f, 0.f);
            in.action = (t >= 0.8f && t < 0.82f) ? CLIP_HOOK : -1;
            break;
        case 12:   // block (stance 20) until 1.2 s, counter, back to the guard
            in.stance = t < 1.2f ? 20 : 19;
            in.action = (t >= 1.2f && t < 1.22f) ? CLIP_COUNTER : -1;
            break;
        case 13:   // bat: guard, swing at 0.5 s, overhead at 2.0 s
            in.stance = 19;
            in.meleeKind = 2;
            in.weaponKind = 3;
            in.action = (t >= 0.5f && t < 0.52f) ? CLIP_BAT_SWING : ((t >= 2.f && t < 2.02f) ? CLIP_BAT_OVERHEAD : -1);
            break;
        case 15:   // speaking with beat pulses every 0.45 s, then a phone call while walking from 4 s
            in.speaking = t < 4.f;
            in.beat = t < 4.f ? Max(0.f, sinf(kTwoPi * t / 0.45f)) : 0.f;
            in.phoneCall = t >= 4.f;
            in.speed = t >= 4.f ? 1.3f : 0.f;
            break;
        case 16:   // listening
            in.listening = true;
            break;
        case 17:   // browsing a phone standing, then walking from 2.5 s
            in.phoneBrowse = true;
            in.speed = t >= 2.5f ? 1.4f : 0.f;
            break;
        case 14:   // knocked out from the guard at 0.3 s
            in.stance = 19;
            in.action = (t >= 0.3f && t < 0.32f) ? CLIP_KNOCKOUT : -1;
            break;
        default: break;
    }
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp/preview.ppm";
    u32 seed = 1000;
    int role = -1, count = 1, W = 900, H = 900, clip = -1;
    float t = 0.f, dist = -1.f, yaw = 0.f, fov = 30.f, camZ = -1.f, spacing = 0.9f;
    int scenario = 0;
    const char* view = "front";
    bool lineup = false, strip = false, floorOn = false, rootMotion = false, pair = false;
    int weapon = 0, melee = -1;
    bool visemes = false;
    int lodSel = -1;
    float stripDt = -1.f;
    std::vector<int> clipList;
    for (int i = 2; i < argc; i++) {
        auto nx = [&]() { return i + 1 < argc ? argv[++i] : "0"; };
        if (!strcmp(argv[i], "--seed")) seed = (u32)atoi(nx());
        else if (!strcmp(argv[i], "--role")) role = atoi(nx());
        else if (!strcmp(argv[i], "--count")) count = atoi(nx());
        else if (!strcmp(argv[i], "--view")) view = nx();
        else if (!strcmp(argv[i], "--clip")) clip = atoi(nx());
        else if (!strcmp(argv[i], "--t")) t = (float)atof(nx());
        else if (!strcmp(argv[i], "--w")) W = atoi(nx());
        else if (!strcmp(argv[i], "--h")) H = atoi(nx());
        else if (!strcmp(argv[i], "--dist")) dist = (float)atof(nx());
        else if (!strcmp(argv[i], "--yaw")) yaw = (float)atof(nx());
        else if (!strcmp(argv[i], "--fov")) fov = (float)atof(nx());
        else if (!strcmp(argv[i], "--height")) camZ = (float)atof(nx());
        else if (!strcmp(argv[i], "--spacing")) spacing = (float)atof(nx());
        else if (!strcmp(argv[i], "--lineup")) lineup = true;
        else if (!strcmp(argv[i], "--strip")) strip = true;
        else if (!strcmp(argv[i], "--dt")) stripDt = (float)atof(nx());
        else if (!strcmp(argv[i], "--floor")) floorOn = true;
        else if (!strcmp(argv[i], "--scenario")) scenario = atoi(nx());
        else if (!strcmp(argv[i], "--rm")) rootMotion = true;          // apply the clip's root motion
        else if (!strcmp(argv[i], "--pair")) pair = true;              // takedown pair: victim + attacker 0.55 m behind
        else if (!strcmp(argv[i], "--weapon")) { const char* w = nx(); weapon = !strcmp(w, "bat") ? 1 : (!strcmp(w, "knife") ? 2 : 0); }
        else if (!strcmp(argv[i], "--melee")) melee = atoi(nx());      // AnimInput::meleeKind for scenarios
        else if (!strcmp(argv[i], "--visemes")) visemes = true;        // one character per viseme (0..14)
        else if (!strcmp(argv[i], "--lod")) lodSel = atoi(nx());        // render this LOD (buildCharacterMeshLods)
        else if (!strcmp(argv[i], "--clips")) {
            // comma separated clip list, one per character
            const char* c = nx();
            while (*c) {
                clipList.push_back(atoi(c));
                while (*c && *c != ',') c++;
                if (*c == ',') c++;
            }
        }
    }
    if (pair) {
        // takedown pair: character 0 = victim, character 1 = attacker 0.55 m behind it (same seed variations)
        clipList = {CLIP_TAKEDOWN_VICTIM, CLIP_TAKEDOWN_ATTACKER};
    }
    if (!clipList.empty()) count = (int)clipList.size();
    if (visemes) count = 15;
    Img img(W, H);
    std::vector<Char> chars(count);
    double tb = 0;
    size_t totalTris = 0;
    for (int i = 0; i < count; i++) {
        Char& ch = chars[i];
        u32 sd = lineup ? 1000 + i * 7919 : (pair ? seed + i * 7919 : (strip || visemes || !clipList.empty() ? seed : seed + i * 7919));
        int rl = role >= 0 ? role : (lineup ? i % 7 : 0);
        ch.d = randomCharacter(sd, rl);
        buildSkeleton(ch.d, ch.sk);
        double t0 = TimeSeconds();
        if (getenv("PREVIEW_PARTS")) {
            // debug: raw body build, vertex colors by part (alpha channel keeps nothing)
            detail::BodyDims D;
            detail::computeDims(ch.d, D);
            detail::BuildCtx bc;
            bc.d = &ch.d; bc.D = &D; bc.sk = &ch.sk; bc.skin = ch.d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
            detail::buildBody(bc);
            const vec3 pc[] = {vec3(0.8f, 0.3f, 0.3f), vec3(0.3f, 0.8f, 0.3f), vec3(0.3f, 0.3f, 0.8f), vec3(0.8f, 0.8f, 0.3f), vec3(0.8f, 0.3f, 0.8f),
                               vec3(0.3f, 0.8f, 0.8f), vec3(0.9f, 0.6f, 0.2f), vec3(0.5f, 0.5f, 0.9f), vec3(0.6f, 0.9f, 0.5f), vec3(1, 1, 1),
                               vec3(0.2f, 0.2f, 0.2f), vec3(0.5f), vec3(0.7f), vec3(0.4f), vec3(0.6f)};
            for (auto& v : bc.m.v) { v.col = pc[v.part % 15]; v.mat = MAT_SKIN; }
            detail::emitMesh(bc.m, ch.mesh);
        } else if (lodSel >= 0) {
            SkinnedMeshData lods[3];
            buildCharacterMeshLods(ch.d, ch.sk, lods, 3);
            printf("lods: %zu / %zu / %zu tris\n", lods[0].indices.size() / 3, lods[1].indices.size() / 3, lods[2].indices.size() / 3);
            ch.mesh = lods[Clamp(lodSel, 0, 2)];
        } else
            buildCharacterMesh(ch.d, ch.sk, ch.mesh);
        tb += TimeSeconds() - t0;
        totalTris += ch.mesh.indices.size() / 3;
        printf("char %d: seed %u role %d gender %d h %.2f w %.2f m %.2f age %.2f hair %d top %d bottom %d shoes %d hat %d glasses %d fh %d: %zu verts %zu tris\n",
               i, sd, rl, ch.d.gender, ch.d.height, ch.d.weight, ch.d.muscle, ch.d.age, ch.d.hairStyle, ch.d.top, ch.d.bottom, ch.d.shoes,
               ch.d.hat, ch.d.glasses, ch.d.facialHair, ch.mesh.verts.size(), ch.mesh.indices.size() / 3);
    }
    printf("mesh build avg %.2f ms, avg tris %zu\n", tb * 1000.0 / count, totalTris / count);
    // camera
    float cx = (count - 1) * spacing * 0.5f;
    Cam cam;
    cam.fov = fov;
    float Hc = chars[0].d.height;
    float zc = camZ > 0 ? camZ : Hc * 0.52f;
    float dd = dist > 0 ? dist : Max(Hc * 0.56f / tanf(fov * 0.5f * kDegToRad), (count * spacing * 0.55f) / (tanf(fov * 0.5f * kDegToRad) * W / H));
    vec3 target(cx, 0, zc);
    vec3 dir(0, 1, 0);
    if (!strcmp(view, "side")) dir = vec3(1, 0, 0);
    else if (!strcmp(view, "back")) dir = vec3(0, -1, 0);
    else if (!strcmp(view, "three")) dir = normalize(vec3(0.7f, 1, 0.1f));
    else if (!strcmp(view, "top")) dir = normalize(vec3(0.0f, 0.3f, 1.0f));
    else if (!strcmp(view, "upper") || !strcmp(view, "upperside") || !strcmp(view, "upperback")) {
        dir = !strcmp(view, "upper") ? vec3(0, 1, 0) : (!strcmp(view, "upperside") ? vec3(1, 0, 0) : vec3(0, -1, 0));
        target = vec3(cx, 0, camZ > 0 ? camZ : Hc * 0.75f);
        dd = dist > 0 ? dist : 1.5f;
    } else if (!strcmp(view, "face") || !strcmp(view, "face3") || !strcmp(view, "faceside")) {
        dir = !strcmp(view, "face") ? vec3(0, 1, 0) : (!strcmp(view, "face3") ? normalize(vec3(0.8f, 1, 0)) : vec3(1, 0, 0));
        target = vec3(cx, 0.03f, camZ > 0 ? camZ : Hc * 0.93f);
        dd = dist > 0 ? dist : 0.75f;
    } else if (!strcmp(view, "shoulder") || !strcmp(view, "shoulderback") || !strcmp(view, "shouldertop")) {
        dir = !strcmp(view, "shoulder") ? normalize(vec3(0.5f, 1, 0.1f)) : (!strcmp(view, "shoulderback") ? normalize(vec3(0.5f, -1, 0.1f)) : normalize(vec3(0.3f, 0.3f, 1)));
        target = vec3(cx + 0.2f * Hc / 1.78f, 0.0f, camZ > 0 ? camZ : Hc * 0.8f);
        dd = dist > 0 ? dist : 0.7f;
    } else if (!strcmp(view, "throat")) {
        dir = normalize(vec3(0.3f, 1, -0.5f));
        target = vec3(cx, 0.03f, camZ > 0 ? camZ : Hc * 0.87f);
        dd = dist > 0 ? dist : 0.45f;
    } else if (!strcmp(view, "hand")) {
        dir = normalize(vec3(0.6f, 1, -0.2f));
        target = vec3(cx + 0.55f * Hc / 1.78f, 0.0f, camZ > 0 ? camZ : Hc * 0.43f);
        dd = dist > 0 ? dist : 0.55f;
    } else if (!strcmp(view, "feet")) {
        dir = normalize(vec3(0.5f, 1, 0.4f));
        target = vec3(cx, 0.05f, camZ > 0 ? camZ : 0.1f);
        dd = dist > 0 ? dist : 0.9f;
    }
    dir = rotate(quatAxisAngle(vec3(0, 0, 1), yaw * kDegToRad), dir);
    // screen-right direction (the camera looks along -dir)
    vec3 sideAxis = length(vec3(dir.x, dir.y, 0.f)) < 1e-3f ? vec3(-1, 0, 0) : normalize(vec3(-dir.y, dir.x, 0.f));
    if (getenv("PREVIEW_ALONGX")) sideAxis = vec3(1, 0, 0);
    cam.eye = target + dir * dd;
    cam.target = target;
    cam.setup(W, H);
    for (int i = 0; i < count; i++) {
        Char& ch = chars[i];
        Pose pose;
        for (int b = 0; b < B_COUNT; b++) pose.rot[b] = quat();
        pose.rootOffset = vec3(0);
#ifdef ANIM_HAVE_CLIPS
        int ci = !clipList.empty() ? clipList[i] : clip;
        float ti = t;
        if (strip && ci >= 0) ti = t + i * (stripDt > 0.f ? stripDt : clipInfo((Clip)ci).duration / Max(count - (clipInfo((Clip)ci).loop ? 0 : 1), 1));
        if (strip) printf("frame %d: t=%.3f\n", i, ti);
        if (ci >= 0) sampleClip(ch.sk, (Clip)ci, ti, pose, (u32)i);
        if (const char* ic = getenv("PREVIEW_ICLIP")) {
            // internal clips: base id + character index (CLIP_COUNT + n, see anim_internal.h)
            detail::sampleClipId(ch.sk, atoi(ic) + (strip ? 0 : i), ti, pose, (u32)i);
        }
        if (pair && i == 1 && getenv("PREVIEW_PAIRIK")) {
            // attacker through the animator with its choke arm IK'd onto the victim's (character 0's) actual neck
            Animator an;
            an.init(&ch.sk, 5u);
            const float dt = 1.f / 60.f;
            for (float tt = 0.f; tt < ti; tt += dt) {
                AnimInput in;
                in.action = tt < dt * 0.5f ? CLIP_TAKEDOWN_ATTACKER : -1;
                if (tt < dt * 0.5f) in.action = CLIP_TAKEDOWN_ATTACKER;
                Pose vp;
                sampleClip(chars[0].sk, CLIP_TAKEDOWN_VICTIM, tt, vp, 0u);
                mat4 vm[B_COUNT];
                computeMatrices(chars[0].sk, vp, vm, nullptr);
                vec3 neckW = vm[B_NECK].c[3].xyz() + clipRootMotion(chars[0].sk, CLIP_TAKEDOWN_VICTIM, tt);
                vec3 attRoot = vec3(0.f, -0.55f, 0.f) + clipRootMotion(ch.sk, CLIP_TAKEDOWN_ATTACKER, tt);
                in.grabTarget = neckW - attRoot;
                in.grabWeight = 1.f;
                an.update(in, dt);
            }
            pose = an.pose;
        }
        if (scenario > 0) {
            // run the Animator with scripted inputs up to time ti (character i: ti = t + i * dt)
            Animator an;
            an.init(&ch.sk, 7u);
            float T = strip ? t + i * (stripDt > 0.f ? stripDt : 0.5f) : t;
            const float dt = 1.f / 60.f;
            for (float tt = 0.f; tt < T; tt += dt) {
                AnimInput in;
                runScenario(scenario, tt, in);
                if (melee >= 0) in.meleeKind = melee;
                an.update(in, dt);
            }
            pose = an.pose;
            printf("scenario %d t=%.2f action %d stance %d\n", scenario, T, an.action, an.stance);
        }
#else
        (void)t;
#endif
        if (const char* ex = getenv("PREVIEW_EXPR")) {
            // facial expression through the animator (idle, 1 s to settle)
            Animator an;
            an.init(&ch.sk, 7u);
            AnimInput in;
            in.expression = atoi(ex);
            for (int f = 0; f < 60; f++) an.update(in, 1.f / 60.f);
            an.blinkT = -1.f;
            an.blinkNext = 5.f;
            an.update(in, 1.f / 60.f);
            pose = an.pose;
        }
        if (visemes || getenv("PREVIEW_VISEME")) {
            int vi = visemes ? i : atoi(getenv("PREVIEW_VISEME"));
            float shape[6];
            detail::visemeShape(vi, 1.f, shape);
            detail::applyMouthShape(pose, shape, shape[0]);
        }
        if (const char* ep = getenv("PREVIEW_EYEPITCH")) {
            // debug: pitch both eye bones (the upper lids ride on them: -0.95 closes the eyes)
            float a = (float)atof(ep);
            pose.rot[B_EYE_L] = normalize(pose.rot[B_EYE_L] * quatAxisAngle(vec3(1, 0, 0), a));
            pose.rot[B_EYE_R] = normalize(pose.rot[B_EYE_R] * quatAxisAngle(vec3(1, 0, 0), a));
        }
        if (const char* ey = getenv("PREVIEW_EYEYAW")) {
            float a = (float)atof(ey);
            pose.rot[B_EYE_L] = normalize(pose.rot[B_EYE_L] * quatAxisAngle(vec3(0, 0, 1), a));
            pose.rot[B_EYE_R] = normalize(pose.rot[B_EYE_R] * quatAxisAngle(vec3(0, 0, 1), a));
        }
        mat4 ms[B_COUNT], skin[B_COUNT];
        computeMatrices(ch.sk, pose, ms, skin);
        std::vector<vec3> P(ch.mesh.verts.size()), N(ch.mesh.verts.size()), A(ch.mesh.verts.size());
        std::vector<u32> M(ch.mesh.verts.size());
        // characters are lined up across the view direction
        vec3 off = sideAxis * ((i - (count - 1) * 0.5f) * spacing) + vec3(cx, 0, 0);
#ifdef ANIM_HAVE_CLIPS
        if (pair) off = vec3(cx, i == 1 ? -0.55f : 0.f, 0.f);
        if ((rootMotion || pair) && ci >= 0) off = off + clipRootMotion(ch.sk, (Clip)ci, ti);
#endif
        for (size_t v = 0; v < ch.mesh.verts.size(); v++) {
            const VtxSkinned& vx = ch.mesh.verts[v];
            mat4 m;
            for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
            for (int k = 0; k < 4; k++) {
                float w = vx.weights[k] / 255.f;
                if (w <= 0) continue;
                const mat4& s = skin[vx.bones[k]];
                for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + s.c[c] * w;
            }
            P[v] = transformPoint(m, vx.pos) + off;
            N[v] = normalize(transformDir(m, unpackNormalOct(vx.normal)));
            vec4 cc = unpackRGBA8(vx.color);
            A[v] = matAlbedo(vx.mat, cc.xyz());
            if (getenv("PREVIEW_MATS")) {
                u32 mm = vx.mat & 0xff;
                A[v] = mm == MAT_EYE ? vec3(0, 1, 0) : (mm == MAT_HAIR ? vec3(1, 0, 0) : (mm == MAT_SKIN ? vec3(0.5f) : vec3(0, 0, 1)));
            }
            M[v] = vx.mat;
        }
        drawMesh(img, cam, P, N, A, M, ch.mesh.indices);
        if (getenv("PREVIEW_PHONE")) {
            mat4 msp[B_COUNT];
            for (int b = 0; b < B_COUNT; b++) {
                msp[b] = ms[b];
                msp[b].c[3] = vec4(ms[b].c[3].xyz() + off, 1.f);
            }
            vec3 pp, la, sc;
            phoneFrame(ch.sk, msp, pp, la, sc);
            vec3 wx = normalize(cross(la, sc));
            std::vector<vec3> Ps, Ns, As;
            std::vector<u32> Ms, Is;
            vec3 hl = la * 0.073f, hw = wx * 0.035f, ht = sc * 0.004f;
            vec3 corners[8];
            for (int k = 0; k < 8; k++) corners[k] = pp + hl * ((k & 1) ? 1.f : -1.f) + hw * ((k & 2) ? 1.f : -1.f) + ht * ((k & 4) ? 1.f : -1.f);
            const int faces[6][4] = {{4, 5, 7, 6}, {0, 2, 3, 1}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
            for (int f = 0; f < 6; f++) {
                u32 b0 = (u32)Ps.size();
                vec3 fc(0);
                for (int k = 0; k < 4; k++) fc += corners[faces[f][k]];
                vec3 fn = normalize(fc * 0.25f - pp);
                for (int k = 0; k < 4; k++) {
                    Ps.push_back(corners[faces[f][k]]);
                    Ns.push_back(fn);
                    As.push_back(f == 0 ? vec3(0.1f, 0.25f, 0.5f) : vec3(0.05f));
                    Ms.push_back(MAT_PLASTIC);
                }
                vec3 e1 = Ps[b0 + 1] - Ps[b0], e2 = Ps[b0 + 2] - Ps[b0];
                bool flip = dot(cross(e1, e2), fn) < 0.f;
                u32 q[6] = {b0, b0 + 1, b0 + 2, b0, b0 + 2, b0 + 3};
                if (flip) { std::swap(q[1], q[2]); std::swap(q[4], q[5]); }
                for (u32 x : q) Is.push_back(x);
            }
            drawMesh(img, cam, Ps, Ns, As, Ms, Is);
        }
        if (weapon) {
            mat4 msw[B_COUNT];
            for (int b = 0; b < B_COUNT; b++) {
                msw[b] = ms[b];
                msw[b].c[3] = vec4(ms[b].c[3].xyz() + off, 1.f);
            }
            vec3 gp, ga, gpalm;
            handGrip(ch.sk, msw, true, gp, ga, gpalm);
            drawWeapon(img, cam, weapon, gp, ga, gpalm);
        }
    }
    if (floorOn) {
        // checkerboard floor (0.25 m tiles) at z = 0 to judge ground contact
        std::vector<vec3> P, N, A;
        std::vector<u32> M, I;
        float ext = (count - 1) * spacing * 0.5f + 1.5f;
        float x0 = cx - ext, x1 = cx + ext, y0 = -ext - 1.f, y1 = ext + 1.f, tile = 0.25f;
        for (float y = y0; y < y1 - 1e-4f; y += tile)
            for (float x = x0; x < x1 - 1e-4f; x += tile) {
                int k = (int)floorf(x / tile + 1000.f) + (int)floorf(y / tile + 1000.f);
                vec3 c = (k & 1) ? vec3(0.32f, 0.3f, 0.28f) : vec3(0.42f, 0.4f, 0.37f);
                u32 b = (u32)P.size();
                P.push_back(vec3(x, y, 0)); P.push_back(vec3(x + tile, y, 0)); P.push_back(vec3(x + tile, y + tile, 0)); P.push_back(vec3(x, y + tile, 0));
                for (int q = 0; q < 4; q++) { N.push_back(vec3(0, 0, 1)); A.push_back(c); M.push_back(MAT_CLOTH); }
                I.push_back(b); I.push_back(b + 1); I.push_back(b + 2);
                I.push_back(b); I.push_back(b + 2); I.push_back(b + 3);
            }
        drawMesh(img, cam, P, N, A, M, I);
    }
    img.save(out);
    return 0;
}
