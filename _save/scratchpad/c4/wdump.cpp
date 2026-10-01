#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    u32 seed = (u32)atoi(argv[1]);
    int role = atoi(argv[2]);
    float x0 = (float)atof(argv[3]), y0 = (float)atof(argv[4]), z0 = (float)atof(argv[5]), r = argc > 6 ? (float)atof(argv[6]) : 0.02f;
    CharacterDesc d = randomCharacter(seed, role);
    if (getenv("TOP")) d.top = atoi(getenv("TOP"));
    if (getenv("BOTTOM")) d.bottom = atoi(getenv("BOTTOM"));
    if (getenv("OUTER")) d.outer = atoi(getenv("OUTER"));
    if (getenv("BAG")) d.bag = atoi(getenv("BAG"));
    Skeleton sk;
    buildSkeleton(d, sk);
    SkinnedMeshData m;
    buildCharacterMesh(d, sk, m);
    printf("ankle joint L (%.3f %.3f %.3f)\n", 0.f, 0.f, 0.f);
    for (size_t i = 0; i < m.verts.size(); i++) {
        const VtxSkinned& v = m.verts[i];
        if (length(v.pos - vec3(x0, y0, z0)) > r) continue;
        printf("%s (%.3f %.3f %.3f) mat %u p %x col %08x bones %d:%d %d:%d %d:%d\n", (v.mat & 0xff) == MAT_SKIN ? "SKIN" : "    ", v.pos.x, v.pos.y, v.pos.z, v.mat & 0xffu, v.mat >> 8, v.color, v.bones[0], v.weights[0], v.bones[1], v.weights[1], v.bones[2], v.weights[2]);
    }
}
