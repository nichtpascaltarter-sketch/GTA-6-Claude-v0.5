#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    u32 sd = argc > 1 ? atoi(argv[1]) : 1u;
    float rateT = argc > 2 ? atof(argv[2]) : 1.6f;
    CharacterDesc d = randomCharacter(sd * 313u, (int)(sd % 5));
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, sd + 50u);
    an.setCharacter(d);
    const float dt = 1.f / 60.f;
    AnimInput in;
    in.footProbes = true;
    float yaw = 0.f;
    float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * 0.21f / 0.52f;
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
    vec3 pH[2], pB[2];
    for (int f = 0; f < 240; f++) {
        float t = f * dt;
        in.turnRate = t > 0.5f && t < 3.5f ? rateT : 0.f;
        yaw += in.turnRate * dt;
        an.update(in, dt);
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        quat q = quatAxisAngle(vec3(0, 0, 1), yaw);
        printf("f %3d lag %6.3f |", f, an.bodyLag);
        for (int s = 0; s < 2; s++) {
            int fb = s ? B_FOOT_R : B_FOOT_L;
            vec3 H = rotate(q, m[fb].c[3].xyz() + transformDir(m[fb], vec3(0, -heel, -ankH)));
            vec3 B = rotate(q, m[fb].c[3].xyz() + transformDir(m[fb], vec3(0, ball, -ankH)));
            float vh = length(vec2(H.x - pH[s].x, H.y - pH[s].y)) / dt, vb = length(vec2(B.x - pB[s].x, B.y - pB[s].y)) / dt;
            vec3 Hm = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0, -heel, -ankH));
            float err = an.planted[s] ? length(vec2(Hm.x - an.plantP[s].x, Hm.y - an.plantP[s].y)) : -1.f;
            printf(" %c pl %d st %5.2f cy %6.3f hz %.3f bz %.3f vh %.3f vb %.3f herr %.3f on %.2f |", s ? 'R' : 'L', an.planted[s], an.stepT[s], an.corrYaw[s], H.z, B.z, vh, vb, err, an.plantOn);
            vec3 PW = rotate(q, an.plantP[s]);
            if (getenv("PW")) printf(" pw(%.4f,%.4f) hw(%.4f,%.4f) sink %.4f shift %.4f", PW.x, PW.y, H.x, H.y, an.legSink, an.stepShift);
            bool hl = H.z < B.z;
            float vl = hl ? vh : vb;
            if ((hl ? H.z : B.z) < 0.004f && (hl ? pH[s].z : pB[s].z) < 0.004f && vl > 0.04f && f > 36) printf(" SLIDE%c %.3f", s ? 'R' : 'L', vl);
            pH[s] = H;
            pB[s] = B;
        }
        printf("\n");
    }
}
