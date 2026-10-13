// Hand clearance from the body per standing posture / fidget, per body type and weight side.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
struct Body {
    Sdf part[3];
    void build(const CharacterDesc& d, const Skeleton& sk) {
        static BodyDims D;
        computeDims(d, D);
        static BuildCtx bc;
        bc = BuildCtx();
        bc.d = &d; bc.D = &D; bc.sk = &sk;
        addBodyPrims(bc);
        for (const Prim& q : bc.sdf.prims) {
            if (q.mask & MK_TORSO) part[0].prims.push_back(q);
            else if (q.mask & MK_LEG_L) part[1].prims.push_back(q);
            else if (q.mask & MK_LEG_R) part[2].prims.push_back(q);
        }
    }
    float dist(const Skeleton& sk, const mat4* m, vec3 p, int* which) const {
        float best = 1e9f;
        for (int k = 0; k < 3; k++) {
            int bone = k == 0 ? B_PELVIS : (k == 1 ? B_THIGH_L : B_THIGH_R);
            const mat4& M = m[bone];
            vec3 dd = p - M.c[3].xyz();
            vec3 local(dot(dd, M.c[0].xyz()), dot(dd, M.c[1].xyz()), dot(dd, M.c[2].xyz()));
            vec3 J = -sk.invBindModel[bone].c[3].xyz();
            float v = part[k].eval(J + local, MK_ALL);
            if (v < best) best = v, *which = k;
        }
        return best;
    }
};
int main(int argc, char** argv) {
    struct B { const char* n; int fem; float h, wt, mu; } bodies[] = {
        {"refM", 0, 1.78f, 0.45f, 0.45f}, {"refF", 1, 1.65f, 0.45f, 0.3f}, {"heavyM", 0, 1.76f, 0.9f, 0.4f}, {"heavyF", 1, 1.63f, 0.9f, 0.3f},
        {"thinF", 1, 1.70f, 0.12f, 0.2f}, {"buffM", 0, 1.84f, 0.5f, 0.95f}};
    const int ids[] = {IC_IDLE_PHONE, IC_IDLE_CROSSARMS, IC_IDLE_POCKETS, IC_IDLE_HIP, IC_IDLE_BEHIND, IC_IDLE_CLASP, IC_FIDGET_WATCH, IC_FIDGET_SCRATCH,
                       IC_FIDGET_TUG, IC_FIDGET_CHIN, IC_FIDGET_YAWN, IC_FIDGET_ARMS, IC_FIDGET_TAP, IC_FIDGET_ROCK, IC_IDLE_STRETCH, -1};
    const char* names[] = {"phone", "crossarms", "pockets", "hip", "behind", "clasp", "watch", "scratch", "tug", "chin", "yawn", "arms", "tap", "rock", "neckroll", "plain"};
    int onlyId = argc > 1 ? atoi(argv[1]) : -2;
    printf("%-10s", "");
    for (auto& b : bodies) printf(" %12s", b.n);
    printf("\n");
    for (int k = 0; k < 16; k++) {
        if (onlyId > -2 && ids[k] != onlyId) continue;
        printf("%-10s", names[k]);
        for (auto& bd : bodies) {
            CharacterDesc d = randomCharacter(777u, 0);
            d.gender = bd.fem ? FEMALE : MALE;
            d.height = bd.h; d.weight = bd.wt; d.muscle = bd.mu; d.age = 0.3f;
            Skeleton sk;
            buildSkeleton(d, sk);
            Body body;
            body.build(d, sk);
            float worst = 1e9f;
            int wk = -1, ws = -1;
            float wt = 0.f, wsw = 0.f;
            for (int side = 0; side < 3; side++) {
                Animator an;
                an.init(&sk, 11u);
                an.setCharacter(d);
                an.standW = an.standTarget = side * 0.5f;
                an.standNext = 1e9f;
                an.fidgetNext = 1e9f;
                an.idleNext = 1e9f;
                bool fid = ids[k] >= IC_FIDGET_WATCH || ids[k] == IC_IDLE_STRETCH;
                float dur = ids[k] >= 0 ? clipInfoId(ids[k]).duration : 4.f;
                if (ids[k] >= 0) {
                    if (fid) { an.fidgetVar = ids[k]; an.fidgetT = 0.f; an.fidgetDur = dur; an.fidgetW = 1.f; }
                    else { an.idleVar = ids[k]; an.idleVarT = 0.f; an.idleVarDur = 1e9f; an.idleVarW = 1.f; }
                }
                AnimInput in;
                in.footProbes = true;
                for (int f = 0; f < (int)(dur * 60.f); f++) {
                    an.update(in, 1.f / 60.f);
                    if (f % 2) continue;
                    mat4 m[B_COUNT];
                    computeMatrices(sk, an.pose, m, nullptr);
                    for (int s = 0; s < 2; s++) {
                        vec3 wr = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
                        vec3 fingD = normalize(transformDir(m[s ? B_HAND_R : B_HAND_L], sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                        vec3 palm = wr + fingD * (0.45f * length(sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                        int which = -1;
                        float dd = body.dist(sk, m, palm, &which) - sk.boneRadius[s ? B_HAND_R : B_HAND_L] * 0.75f;
                        if (dd < worst) worst = dd, wk = which, ws = s, wt = f / 60.f, wsw = side * 0.5f;
                    }
                }
            }
            printf("  %6.3f %c%d@%.1f", worst, ws ? 'R' : 'L', wk, wt);
        }
        printf("\n");
    }
}
