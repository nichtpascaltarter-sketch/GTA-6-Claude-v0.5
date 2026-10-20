// scratch: the seated pose against the seat (backrest front plane, cushion top) and the head under the headliner
#define main carview_main
#include "/home/user/GTA-6-Claude-v0.5/tests/anim/carview.cpp"
#undef main
int main(int argc, char** argv) {
    int model = argc > 1 ? atoi(argv[1]) : 3, seat = argc > 2 ? atoi(argv[2]) : 0;
    VehicleModel vm;
    buildModel(model, vm);
    const SeatSpec& ss = vm.seats[seat];
    const float recline = seat < 2 ? 0.32f : 0.36f;
    const vec3 n(0, cosf(recline), sinf(recline));   // the backrest's front normal (forward-up)
    const vec3 B0 = ss.pos + vec3(0, -0.05f + 0.07f * cosf(recline), 0.02f + 0.07f * sinf(recline));
    const float heights[5] = {1.58f, 1.68f, 1.76f, 1.84f, 1.94f};
    for (int hi = 0; hi < 5; hi++) {
        CarChar ch;
        ch.d = randomCharacter(1000u + (u32)hi * 31u, 0);
        ch.d.height = heights[hi];
        ch.d.hat = -1;
        if (ch.d.bottom == Anim::detail::BOT_SKIRT) ch.d.bottom = Anim::detail::BOT_JEANS;
        buildSkeleton(ch.d, ch.sk);
        buildCharacterMesh(ch.d, ch.sk, ch.mesh);
        Animator an;
        an.init(&ch.sk, 77u);
        an.setCharacter(ch.d);
        for (int f = 0; f < 60; f++) {
            AnimInput in;
            in.stance = ss.driver ? 1 : 2;
            seatInputs(vm, seat, in);
            an.update(in, 1.f / 60.f);
        }
        mat4 ms[B_COUNT], skin[B_COUNT];
        computeMatrices(ch.sk, an.pose, ms, skin);
        vec3 root = ss.pos - vec3(0, 0, 0.5f);
        float backPen = 0.f, backZ = 0.f, topZ = 0.f;
        float minSeatGap = 1e9f;
        for (const VtxSkinned& vx : ch.mesh.verts) {
            mat4 m;
            for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
            for (int k = 0; k < 4; k++) {
                float w = vx.weights[k] / 255.f;
                if (w <= 0) continue;
                for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + skin[vx.bones[k]].c[c] * w;
            }
            vec3 p = root + transformPoint(m, vx.pos);
            topZ = Max(topZ, p.z);
            if (fabsf(p.x - ss.pos.x) < 0.2f && p.z > ss.pos.z + 0.12f && p.z < ss.pos.z + 0.62f) {
                float d = dot(p - B0, n);
                if (-d > backPen) { backPen = -d; backZ = p.z - ss.pos.z; }
            }
        }
        vec3 hips = (ms[B_THIGH_L].c[3].xyz() + ms[B_THIGH_R].c[3].xyz()) * 0.5f + root;
        vec3 head = ms[B_HEAD].c[3].xyz() + root, neck = ms[B_NECK].c[3].xyz() + root;
        vec3 kn = ms[B_CALF_L].c[3].xyz() + root;
        printf("h %.2f: back into the backrest %.3f (at hip+%.2f)  head top %+.3f under headliner  hips (dy %+.3f dz %+.3f)  head joint dy %+.3f  knee z %.3f\n",
               heights[hi], backPen, backZ, ss.headZ - topZ, hips.y - ss.pos.y, hips.z - ss.pos.z, head.y - ss.pos.y, kn.z);
    }
}
