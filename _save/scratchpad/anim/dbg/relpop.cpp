// Release pops: per-frame jump of a foot's lowest sole point in the frames around the planting release, walking in a
// direction at a speed (after a settle), several characters.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    const float dt = 1.f / 60.f;
    vec2 dir(atof(argv[1]), atof(argv[2]));
    dir = normalize(dir);
    float v = atof(argv[3]);
    int nch = argc > 4 ? atoi(argv[4]) : 6;
    double worst = 0, sum = 0; int n = 0;
    double skSum = 0; int skN = 0;
    double hid = 0, hidSum = 0; int hidN = 0;
    double accMax = 0, accSum = 0; int accN = 0;
    for (int ci = 0; ci < nch; ci++) {
        CharacterDesc d = randomCharacter(100u + ci * 7u, ci & 1);
        Skeleton sk;
        buildSkeleton(d, sk);
        Animator an, raw;
        an.init(&sk, 5u + ci);
        an.setCharacter(d);
        raw.init(&sk, 5u + ci);
        raw.setCharacter(d);
        AnimInput in;
        in.footProbes = true;
        in.speed = v;
        in.localMoveDir = dir;
        vec3 root(0);
        float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * (0.21f / 0.52f);
        float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
        vec3 prev[2][2];
        float zh[2][3] = {{0,0,0},{0,0,0}};
        bool wasPl[2] = {false, false};
        int sinceRel[2] = {99, 99};
        for (int f = 0; f < 480; f++) {
            root = root + vec3(dir.x, dir.y, 0.f) * (v * dt);
            an.update(in, dt);
            raw.update(in, dt, true);
            mat4 m[B_COUNT], mr[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            computeMatrices(sk, raw.pose, mr, nullptr);
            for (int s = 0; s < 2; s++) {
                int fb = s ? B_FOOT_R : B_FOOT_L;
                vec3 hh = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
                vec3 bb = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
                if (wasPl[s] && !an.planted[s]) sinceRel[s] = 0; else sinceRel[s]++;
                {
                    // the raw pose's lowest sole point while this foot is planted: what the pin hides
                    vec3 rh = mr[fb].c[3].xyz() + transformDir(mr[fb], vec3(0.f, -heel, -ankH));
                    vec3 rb = mr[fb].c[3].xyz() + transformDir(mr[fb], vec3(0.f, ball, -ankH));
                    float low = Min(rh.z, rb.z);
                    if (f > 120 && an.planted[s]) { hid = Max(hid, (double)low); hidSum += Max(0.f, low); hidN++; if (low > 0.01f && getenv("WHO")) printf("  ch %d f %d s %d ph %.3f hidden %.4f\n", ci, f, s, an.phase, low); }
                    if (getenv("RAW") && ci == atoi(getenv("RAW")) && s == 0 && f > atoi(getenv("F0")) && f < atoi(getenv("F0")) + 30) {
                        float ph = an.phase; 
                        printf("   f %d ph %.3f pl %d raw heel %.4f ball %.4f  shown heel %.4f ball %.4f\n", f, ph, (int)an.planted[s], rh.z, rb.z, hh.z, bb.z);
                    }
                }
                if (f > 120 && sinceRel[s] <= 1) {
                    // vertical speed of the lower of heel / ball (same point as last frame)
                    bool hl = hh.z < bb.z;
                    vec3 a = hl ? hh : bb, pa = hl ? prev[s][0] : prev[s][1];
                    float vz = (a.z - pa.z) / dt;
                    worst = Max(worst, (double)vz); sum += vz; n++;
                    if (getenv("SHOW")) printf("  ch %d f %d foot %d vz %.2f z %.4f\n", ci, f, s, vz, a.z);
                }
                if (f > 120 && an.planted[s] && wasPl[s]) {
                    bool hl = hh.z < bb.z;
                    vec3 a = hl ? hh : bb, pa = hl ? prev[s][0] : prev[s][1];
                    skSum += length(vec2(a.x - pa.x, a.y - pa.y)) / dt; skN++;
                }
                {
                    // second difference of the lowest sole point's height (a pop: a large one-frame kink)
                    float lowz = Min(hh.z, bb.z);
                    zh[s][0] = zh[s][1]; zh[s][1] = zh[s][2]; zh[s][2] = lowz;
                    if (f > 122 && sinceRel[s] <= 2) {
                        float acc = fabsf(zh[s][2] - 2.f * zh[s][1] + zh[s][0]) / (dt * dt);
                        accMax = Max(accMax, (double)acc); accSum += acc; accN++;
                    }
                }
                prev[s][0] = hh; prev[s][1] = bb;
                wasPl[s] = an.planted[s];
            }
        }
    }
    printf("dir (%.2f %.2f) v %.2f: release vz mean %.2f max %.2f m/s (%d releases); planted skate %.3f m/s; raw lift hidden mean %.4f max %.4f m; release accel mean %.0f max %.0f m/s2\n", dir.x, dir.y, v, n ? sum / n : 0.0, worst, n / 2, skN ? skSum / skN : 0.0, hidN ? hidSum / hidN : 0.0, hid, accN ? accSum / accN : 0.0, accMax);
}
