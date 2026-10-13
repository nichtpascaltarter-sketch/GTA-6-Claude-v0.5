// scratch: the seated head (every head / hair vertex) against the cabin's real ceiling (headliner, visor, rails:
// the lowest surface over it), per seat and height
#define main carview_main
#include "/home/user/GTA-6-Claude-v0.5/tests/anim/carview.cpp"
#undef main
struct Ceil {
    float x0, y0, cs = 0.01f;
    int nx, ny;
    std::vector<float> z, top;
    std::vector<u8> mat;
    void build(const MeshData& m, vec3 c, float zmin) {
        x0 = c.x - 0.3f; y0 = c.y - 0.45f; nx = 60; ny = 90;
        z.assign(nx * ny, 9.f);
        top.assign(nx * ny, -9.f);
        mat.assign(nx * ny, 0);
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const VtxStatic& va = m.verts[m.indices[i]];
            u32 mt = va.mat & 0xff;
            if (!getenv("WITHGLASS") && (mt == MAT_CAR_WINDOW || mt == MAT_CAR_GLASS)) continue;
            if (mt == MAT_LEATHER || (mt == MAT_FABRIC && (va.color & 0xffu) <= 100u)) continue;   // (the seats: headrests)
            vec3 a = va.pos, b = m.verts[m.indices[i + 1]].pos, cc = m.verts[m.indices[i + 2]].pos;
            if (Max(a.z, Max(b.z, cc.z)) < zmin) continue;
            float d = (b.y - cc.y) * (a.x - cc.x) + (cc.x - b.x) * (a.y - cc.y);
            if (fabsf(d) < 1e-10f) continue;
            int ix0 = Max(0, (int)((Min(a.x, Min(b.x, cc.x)) - x0) / cs)), ix1 = Min(nx - 1, (int)((Max(a.x, Max(b.x, cc.x)) - x0) / cs) + 1);
            int iy0 = Max(0, (int)((Min(a.y, Min(b.y, cc.y)) - y0) / cs)), iy1 = Min(ny - 1, (int)((Max(a.y, Max(b.y, cc.y)) - y0) / cs) + 1);
            for (int iy = iy0; iy <= iy1; iy++)
                for (int ix = ix0; ix <= ix1; ix++) {
                    float x = x0 + (ix + 0.5f) * cs, y = y0 + (iy + 0.5f) * cs;
                    float l1 = ((b.y - cc.y) * (x - cc.x) + (cc.x - b.x) * (y - cc.y)) / d;
                    float l2 = ((cc.y - a.y) * (x - cc.x) + (a.x - cc.x) * (y - cc.y)) / d;
                    float l3 = 1.f - l1 - l2;
                    if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) continue;
                    float zz = l1 * a.z + l2 * b.z + l3 * cc.z;
                    if (zz > zmin && zz < z[iy * nx + ix]) { z[iy * nx + ix] = zz; mat[iy * nx + ix] = (u8)mt; }
                    if (zz > zmin) top[iy * nx + ix] = Max(top[iy * nx + ix], zz);
                }
        }
    }
    // the clearance over a point: to the lowest surface over it - or, when it is above every surface there (the head
    // through the roof), minus how far above the highest
    float at(float x, float y, float pz, int* m = nullptr) const {
        int ix = (int)((x - x0) / cs), iy = (int)((y - y0) / cs);
        if (ix < 0 || iy < 0 || ix >= nx || iy >= ny) return 9.f;
        if (m) *m = mat[iy * nx + ix];
        return z[iy * nx + ix] - pz;   // (the lowest surface over it: the headliner, the glass - a head through the roof is under it too)
    }
};
int main(int argc, char** argv) {
    int model = argc > 1 ? atoi(argv[1]) : 3, seat = argc > 2 ? atoi(argv[2]) : 0;
    bool verbose = getenv("V") != nullptr;
    VehicleModel vm;
    buildModel(model, vm);
    if (seat >= (int)vm.seats.size()) return 0;
    const SeatSpec& ss = vm.seats[seat];
    Ceil cl;
    cl.build(vm.body, ss.pos, ss.pos.z + 0.5f);
    const float heights[5] = {1.58f, 1.68f, 1.76f, 1.84f, 1.94f};
    printf("%-16s seat %d:", vm.name.c_str(), seat);
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
        float worst = 9.f;
        vec3 wp;
        int wm = 0, wb = -1;
        for (const VtxSkinned& vx : ch.mesh.verts) {
            int b = vx.bones[0];
            for (int k = 1; k < 4; k++)
                if (vx.weights[k] > vx.weights[0]) b = vx.bones[k];
            if (partOf(b) != 0) continue;
            mat4 m;
            for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
            for (int k = 0; k < 4; k++) {
                float w = vx.weights[k] / 255.f;
                if (w <= 0) continue;
                for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + skin[vx.bones[k]].c[c] * w;
            }
            vec3 p = root + transformPoint(m, vx.pos);
            int mt = 0;
            float c = cl.at(p.x, p.y, p.z, &mt);
            if (c < worst) { worst = c; wp = p; wm = mt; wb = b; }
        }
        vec3 hips = (ms[B_THIGH_L].c[3].xyz() + ms[B_THIGH_R].c[3].xyz()) * 0.5f + root;
        if (verbose) printf("\n  h %.2f: %+.3f at (%+.2f %+.2f z+%.2f) m%d b%d hair %d  hips dy %+.3f", heights[hi], worst, wp.x - ss.pos.x, wp.y - ss.pos.y, wp.z - ss.pos.z, wm, wb, ch.d.hairStyle, hips.y - ss.pos.y);
        else printf(" %+.3f", worst);
    }
    printf("\n");
}
