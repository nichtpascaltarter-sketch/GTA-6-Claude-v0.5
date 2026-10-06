// Escort check: a cuffed suspect (stance 25) walking, an officer behind and to the right holding the suspect's left
// upper arm with the right hand (grabTarget with no clip), rendered side by side from three views.
#define PREVIEW_NO_MAIN
#include "preview.cpp"

struct Ped {
    CharacterDesc d;
    Skeleton sk;
    SkinnedMeshData mesh;
    Animator an;
    vec3 root;
};
static void addPed(std::vector<vec3>& P, std::vector<vec3>& N, std::vector<vec3>& A, std::vector<u32>& M, std::vector<u32>& I, const Ped& p) {
    mat4 ms[B_COUNT], skin[B_COUNT];
    computeMatrices(p.sk, p.an.pose, ms, skin);
    u32 base = (u32)P.size();
    for (const VtxSkinned& vx : p.mesh.verts) {
        mat4 m;
        for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
        for (int k = 0; k < 4; k++) {
            float w = vx.weights[k] / 255.f;
            if (w <= 0) continue;
            for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + skin[vx.bones[k]].c[c] * w;
        }
        P.push_back(p.root + transformPoint(m, vx.pos));
        N.push_back(normalize(transformDir(m, unpackNormalOct(vx.normal))));
        vec4 cc = unpackRGBA8(vx.color);
        A.push_back(matAlbedo(vx.mat, cc.xyz()));
        M.push_back((vx.mat & 0xffu) == MAT_HAIR ? (u32)MAT_CLOTH : vx.mat);
    }
    for (u32 i : p.mesh.indices) I.push_back(base + i);
}
int main(int argc, char** argv) {
    float behind = argc > 2 ? (float)atof(argv[2]) : 0.45f, side = argc > 3 ? (float)atof(argv[3]) : 0.28f;
    float T = argc > 4 ? (float)atof(argv[4]) : 3.f, speed = argc > 5 ? (float)atof(argv[5]) : 1.2f;
    static Ped s, o;
    s.d = randomCharacter(51u, 0);
    s.d.hat = -1;
    o.d = randomCharacter(77u, 6);   // (role: police if the role table has it)
    o.d.hat = -1;
    for (Ped* p : {&s, &o}) {
        buildSkeleton(p->d, p->sk);
        buildCharacterMesh(p->d, p->sk, p->mesh);
        p->an.init(&p->sk, 7u);
        p->an.setCharacter(p->d);
    }
    const float dt = 1.f / 60.f;
    float worstMiss = 0.f;
    for (float t = 0.f; t < T; t += dt) {
        float v = Min(speed, t * 3.f);
        s.root.y += v * dt;
        o.root = s.root + vec3(side, -behind, 0.f);
        AnimInput a, b;
        a.stance = 25;
        a.speed = v;
        a.localMoveDir = vec2(0, 1);
        s.an.update(a, dt);
        // the suspect's left upper arm, half way down, in the officer's model space
        mat4 ms[B_COUNT];
        computeMatrices(s.sk, s.an.pose, ms, nullptr);
        int sb = getenv("ESC_LEFTARM") ? 0 : 4;   // the suspect's right upper arm (nearest the officer behind-right)
        vec3 arm = s.root + (ms[B_UPPERARM_L + sb].c[3].xyz() * 0.55f + ms[B_FOREARM_L + sb].c[3].xyz() * 0.45f) - o.root;
        b.speed = v;
        b.localMoveDir = vec2(0, 1);
        b.grabTarget = arm;
        b.grabWeight = t > 0.5f ? 1.f : 0.f;
        o.an.update(b, dt);
        if (t > 1.f) {
            mat4 mo[B_COUNT];
            computeMatrices(o.sk, o.an.pose, mo, nullptr);
            vec3 gp, ga, gpalm;
            handGrip(o.sk, mo, o.an.holdSide == 1, gp, ga, gpalm);
            worstMiss = Max(worstMiss, length(gp - arm));
        }
    }
    printf("hand-arm worst %.3f m (behind %.2f side %.2f)\n", worstMiss, behind, side);
    const char* out = argc > 1 ? argv[1] : "/tmp/escort.ppm";
    int W = 1500, H = 560;
    Img img(W * 2, H * 2);
    std::vector<vec3> P, N, A;
    std::vector<u32> M, I;
    addPed(P, N, A, M, I, s);
    addPed(P, N, A, M, I, o);
    const float views[3] = {-90.f, -150.f, 160.f};
    for (int k = 0; k < 3; k++) {
        Img sub(W * 2 / 3, H * 2);
        Cam cam;
        vec3 ctr = s.root + vec3(0.1f, -0.2f, 1.0f);
        float ya = views[k] * kDegToRad, pa = 10.f * kDegToRad;
        cam.eye = ctr + vec3(sinf(ya) * cosf(pa), cosf(ya) * cosf(pa), sinf(pa)) * 3.6f;
        cam.target = ctr;
        cam.fov = 36.f;
        cam.setup(sub.w, sub.h);
        drawMesh(sub, cam, P, N, A, M, I);
        for (int y = 0; y < sub.h; y++)
            for (int x = 0; x < sub.w; x++) img.c[(size_t)y * img.w + x + k * sub.w] = sub.c[(size_t)y * sub.w + x];
    }
    img.down(2).save(out);
    return 0;
}
