#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    u32 seed = (u32)atoi(argv[1]);
    int role = atoi(argv[2]);
    CharacterDesc d = randomCharacter(seed, role);
    if (getenv("TOP")) d.top = atoi(getenv("TOP"));
    if (getenv("OUTER")) d.outer = atoi(getenv("OUTER"));
    Skeleton sk;
    buildSkeleton(d, sk);
    BodyDims D;
    computeDims(d, D);
    BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk;
    bc.skin = d.skinTone;
    bc.lipCol = bc.palmCol = bc.lipInner = bc.skin;
    buildBody(bc);
    // surface height along x at y = 0 (downward ray from above, all masks / torso / neck / arm)
    u32 masks[4] = {0xffffffffu, MK_TORSO, MK_NECK, MK_ARM_R};
    const char* names[4] = {"all", "torso", "neck", "armR"};
    for (float x = 0.04f; x <= 0.24f; x += 0.01f) {
        printf("x %.2f:", x);
        for (int m = 0; m < 4; m++) {
            // march down from z = 1.8 until inside
            float z = 1.8f, hit = -1.f;
            for (; z > 1.0f; z -= 0.001f)
                if (bc.sdf.eval(vec3(x, 0.f, z), masks[m]) < 0.f) { hit = z; break; }
            printf("  %s %.3f", names[m], hit);
        }
        // mesh: highest full-skin vertex within 5 mm of (x, 0)
        float mz = -1.f;
        for (const BVert& v : bc.m.v) if (v.mat == MAT_SKIN && fabsf(v.p.x - x) < 0.005f && fabsf(v.p.y) < 0.008f && v.p.z > 1.2f && v.p.z < 1.7f) mz = Max(mz, v.p.z);
        printf("  mesh %.3f\n", mz);
    }
    for (const BVert& v : bc.m.v)
        if (v.mat == MAT_SKIN && v.p.x > 0.06f && v.p.x < 0.22f && v.p.z > 1.40f && fabsf(v.p.y) < 0.025f)
            printf("  v (%.3f %.3f %.3f) part %d pa %.3f pb %.2f pc %.2f  sdf %.4f\n", v.p.x, v.p.y, v.p.z, v.part, v.pa, v.pb, v.pc, bc.sdf.eval(v.p, 0xffffffffu));
    printf("J clav %.3f %.3f %.3f  upperarm %.3f %.3f %.3f neck %.3f %.3f %.3f\n", D.J[B_CLAVICLE_R].x, D.J[B_CLAVICLE_R].y, D.J[B_CLAVICLE_R].z, D.J[B_UPPERARM_R].x, D.J[B_UPPERARM_R].y, D.J[B_UPPERARM_R].z, D.J[B_NECK].x, D.J[B_NECK].y, D.J[B_NECK].z);
}
