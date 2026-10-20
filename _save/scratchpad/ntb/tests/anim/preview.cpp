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

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp/preview.ppm";
    u32 seed = 1000;
    int role = -1, count = 1, W = 900, H = 900, clip = -1;
    float t = 0.f, dist = -1.f, yaw = 0.f, fov = 30.f, camZ = -1.f, spacing = 0.9f;
    const char* view = "front";
    bool lineup = false;
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
    }
    Img img(W, H);
    std::vector<Char> chars(count);
    double tb = 0;
    size_t totalTris = 0;
    for (int i = 0; i < count; i++) {
        Char& ch = chars[i];
        u32 sd = lineup ? 1000 + i * 7919 : seed + i * 7919;
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
    cam.eye = target + dir * dd;
    cam.target = target;
    cam.setup(W, H);
    for (int i = 0; i < count; i++) {
        Char& ch = chars[i];
        Pose pose;
        for (int b = 0; b < B_COUNT; b++) pose.rot[b] = quat();
        pose.rootOffset = vec3(0);
#ifdef ANIM_HAVE_CLIPS
        if (clip >= 0) sampleClip(ch.sk, (Clip)clip, t, pose, (u32)i);
#else
        (void)t;
#endif
        mat4 ms[B_COUNT], skin[B_COUNT];
        computeMatrices(ch.sk, pose, ms, skin);
        std::vector<vec3> P(ch.mesh.verts.size()), N(ch.mesh.verts.size()), A(ch.mesh.verts.size());
        std::vector<u32> M(ch.mesh.verts.size());
        vec3 off(i * spacing, 0, 0);
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
    }
    img.save(out);
    return 0;
}
