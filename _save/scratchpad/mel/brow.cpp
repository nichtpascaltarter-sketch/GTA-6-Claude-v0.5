#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    CharacterDesc d = randomCharacter(8919, 0);
    Skeleton sk; buildSkeleton(d, sk);
    BodyDims D; computeDims(d, D);
    BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = d.skinTone; c.lipCol = c.skin; c.palmCol = c.skin;
    buildBody(c);
    int shown = 0;
    for (size_t i = 0; i + 5 < c.m.v.size() && shown < 6; i++) {
        const BVert& v = c.m.v[i];
        if (cardKind(v) != CARD_BROW) continue;
        // card: 3 points x 2 edges; root pair i, i+1; tip pair i+4, i+5
        vec3 r = (c.m.v[i].p + c.m.v[i + 1].p) * 0.5f, t = (c.m.v[i + 4].p + c.m.v[i + 5].p) * 0.5f;
        vec3 hr = (r - D.J[B_HEAD]) * 1000.f, ht = (t - D.J[B_HEAD]) * 1000.f;
        printf("card root (%.1f %.1f %.1f) tip (%.1f %.1f %.1f) tangent (%.2f %.2f %.2f)\n", hr.x, hr.y, hr.z, ht.x, ht.y, ht.z, v.t.x, v.t.y, v.t.z);
        shown++;
        i += 5;
    }
}
