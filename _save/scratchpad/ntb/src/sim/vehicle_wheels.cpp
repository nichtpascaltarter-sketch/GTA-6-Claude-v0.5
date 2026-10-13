// Procedural wheels: tire (rounded sidewalls, tread grooves and blocks) + rim designs + brake disc.
// One wheel centered at the origin, axle along +X, outer (show) face towards +X.
namespace Vehicles {
namespace detail {

enum RimStyle : u8 { RIM_SPOKE = 0, RIM_MESH, RIM_TURBINE, RIM_STEEL, RIM_TRUCK, RIM_DISH, RIM_BIKE, RIM_WIRE, RIM_CLASSIC };

struct WheelDesign {
    float R = 0.33f;        // tire outer radius
    float W = 0.22f;        // tire width
    float rimR = 0.216f;    // bead seat radius
    int seg = 30;           // angular segments of the tire
    bool offroad = false;   // chunky shoulder blocks
    bool moto = false;      // round motorcycle profile
    bool whitewall = false;
    RimStyle style = RIM_SPOKE;
    int spokes = 5;
    bool split = false;     // spokes in pairs
    float spokeHub = 0.030f, spokeRim = 0.024f;  // spoke half widths at hub / rim
    float dish = 0.018f;    // face recess behind the lip
    float concave = 0.012f; // face concavity
    float winIn = 0.36f, winOut = 0.90f;  // window radial extent (fraction of rim radius)
    vec3 faceTint = vec3(1, 1, 1);
    u8 faceMat = MAT_RIM;
    vec3 lipTint = vec3(1, 1, 1);
    int lugs = 5;
    vec3 capTint = vec3(0.12f, 0.12f, 0.13f);
    bool disc = true;
    bool hubcap = false;    // plastic full cover on steel wheels
};

// Builds the tire as a closed-ring grid so the tread can be modulated per angle.
inline void buildTire(PMesh& m, const WheelDesign& d) {
    float w = d.W * 0.5f, R = d.R, rr = d.rimR, h = R - rr;
    std::vector<vec2> prof;       // (a, r), outside on the left
    std::vector<int> kind;        // 0 sidewall, 1 shoulder, 2 tread, 3 groove bottom
    auto P = [&](float a, float r, int k) { prof.push_back(vec2(a, r)); kind.push_back(k); };
    if (d.moto) {
        // Motorcycle: nearly circular crown
        P(-w * 0.80f, rr - 0.008f, 0);
        P(-w * 0.86f, rr + 0.010f, 0);
        P(-w * 0.96f, rr + h * 0.35f, 0);
        P(-w * 1.00f, rr + h * 0.55f, 0);
        int n = 10;
        for (int i = 0; i <= n; i++) {
            float t = -1.f + 2.f * i / n;
            float ang = t * 1.25f;
            float a = sinf(ang) * w * 0.98f, r = R - (1.f - cosf(ang)) * h * 0.55f;
            P(a, r, fabsf(t) < 0.75f ? 2 : 1);
        }
        P(w * 1.00f, rr + h * 0.55f, 0);
        P(w * 0.96f, rr + h * 0.35f, 0);
        P(w * 0.86f, rr + 0.010f, 0);
        P(w * 0.80f, rr - 0.008f, 0);
    } else {
        // inner sidewall (faces the car, rarely seen): coarser than the outer one
        P(-w * 0.86f, rr + 0.002f, 0);
        P(-w * 1.00f, rr + h * 0.45f, 0);
        P(-w * 0.90f, R - h * 0.06f, 1);
        // tread with circumferential grooves
        float t0 = -w * 0.78f, t1 = w * 0.78f;
        int grooves = 2;
        float gw = 0.005f, gd = d.offroad ? 0.014f : 0.008f;
        P(t0, R, 2);
        for (int g = 0; g < grooves; g++) {
            float ga = t0 + (t1 - t0) * (g + 1) / (grooves + 1);
            P(ga - gw, R, 2);
            P(ga, R - gd, 3);
            P(ga + gw, R, 2);
        }
        P(t1, R, 2);
        P(w * 0.90f, R - h * 0.06f, 1);
        P(w * 0.97f, rr + h * 0.78f, 0);
        P(w * 1.00f, rr + h * 0.52f, 0);
        P(w * 0.96f, rr + h * 0.25f, 0);
        P(w * 0.86f, rr + 0.002f, 0);
    }
    int rows = (int)prof.size(), cols = d.seg;
    std::vector<float> arc(rows, 0.f);
    for (int i = 1; i < rows; i++) arc[i] = arc[i - 1] + length(prof[i] - prof[i - 1]);
    std::vector<u32> id(rows * cols);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            float th = kTwoPi * j / cols;
            float r = prof[i].y, a = prof[i].x;
            // tread blocks: alternate shoulder lugs, staggered centre blocks
            bool odd = (j & 1) != 0;
            if (kind[i] == 1) r += odd ? -(d.offroad ? 0.010f : 0.0035f) : 0.f;
            if (d.offroad && kind[i] == 2) {
                bool side = a < 0.f;
                if (odd == side) r -= 0.006f;
            }
            vec3 p(a, cosf(th) * r, sinf(th) * r);
            id[i * cols + j] = m.add(p, vec2(th * R, arc[i]));
        }
    m.uvMode = UV_EXPLICIT;
    // Winding: lathe convention (outside on the left of the walking direction)
    for (int i = 0; i + 1 < rows; i++)
        for (int j = 0; j < cols; j++) {
            int j1 = (j + 1) % cols;
            u32 a = id[i * cols + j], b = id[(i + 1) * cols + j], c = id[(i + 1) * cols + j1], dd = id[i * cols + j1];
            bool white = d.whitewall && prof[i].x > 0.f && prof[i].y > rr + h * 0.2f && prof[i + 1].y > rr + h * 0.2f &&
                         prof[i].y < rr + h * 0.62f && prof[i + 1].y < rr + h * 0.62f;
            if (white) m.use(MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.92f));
            else m.use(MAT_TIRE, kCol1);
            m.quad(a, dd, c, b);
        }
    m.uvMode = UV_BOX;
}

// Rim face in polar coordinates with windows between spokes; `ax` = face plane position along +X.
inline void buildSpokeFace(PMesh& m, const WheelDesign& d, float rOut, float aLip) {
    int N = d.spokes;
    int units = d.split ? N * 2 : N;
    float rr = d.rimR;
    float rHub = rr * 0.30f;
    float rIn = rr * d.winIn, rWo = rr * d.winOut;
    rWo = Min(rWo, rOut - 0.006f);
    // rings (radii)
    std::vector<float> ring;
    ring.push_back(rr * 0.12f);
    ring.push_back(rHub);
    int nw = 3;
    for (int k = 0; k <= nw; k++) ring.push_back(lerp(rIn, rWo, (float)k / nw));
    ring.push_back(rOut);
    int R = (int)ring.size();
    // face depth profile: hub proud, concave towards the rim
    auto faceA = [&](float r) {
        float t = Saturate((r - rHub) / Max(rOut - rHub, 1e-3f));
        float a = aLip - d.dish - d.concave * sinf(t * kPi) * (1.f - t * 0.3f);
        if (r < rHub) a += 0.012f * (1.f - r / rHub);
        return a;
    };
    // angular columns: per unit 4 cells [spoke L half, spoke R half, window, window]
    int cols = units * 4;
    std::vector<float> ang(R * cols);
    for (int k = 0; k < R; k++) {
        float r = ring[k];
        float t = Saturate((r - rIn) / Max(rWo - rIn, 1e-3f));
        float hw = lerp(d.spokeHub, d.spokeRim, t);
        if (d.style == RIM_TURBINE) hw = r * (kPi / units) * 0.62f;
        float alpha = asinf(Clamp(hw / Max(r, 1e-3f), 0.f, 0.95f));
        alpha = Min(alpha, kPi / units * 0.9f);
        for (int u = 0; u < units; u++) {
            float c;
            if (d.split) {
                int s = u / 2;
                float pairGap = asinf(Clamp((hw * 1.3f) / Max(r, 1e-3f), 0.f, 0.9f));
                pairGap = Min(pairGap, kPi / N * 0.4f);
                c = kTwoPi * s / N + ((u & 1) ? pairGap : -pairGap);
            } else {
                c = kTwoPi * u / units;
            }
            float cNext;
            if (d.split) {
                int u2 = u + 1;
                int s2 = u2 / 2;
                float pairGap = asinf(Clamp((hw * 1.3f) / Max(r, 1e-3f), 0.f, 0.9f));
                pairGap = Min(pairGap, kPi / N * 0.4f);
                cNext = kTwoPi * s2 / N + ((u2 & 1) ? pairGap : -pairGap);
            } else {
                cNext = kTwoPi * (u + 1) / units;
            }
            float e0 = c - alpha, e1 = c + alpha, e2 = cNext - alpha;
            if (e2 < e1 + 0.002f) e2 = e1 + 0.002f;
            ang[k * cols + u * 4 + 0] = e0;
            ang[k * cols + u * 4 + 1] = c;
            ang[k * cols + u * 4 + 2] = e1;
            ang[k * cols + u * 4 + 3] = (e1 + e2) * 0.5f;
        }
    }
    // Cell open? (window cells inside the window ring range)
    auto open = [&](int k, int c) {  // cell between ring k and k+1, column c..c+1
        int within = c % 4;
        if (within < 2) return false;
        float r0 = ring[k], r1 = ring[k + 1];
        return r0 >= rIn - 1e-4f && r1 <= rWo + 1e-4f;
    };
    std::vector<u32> id(R * cols);
    for (int k = 0; k < R; k++)
        for (int c = 0; c < cols; c++) {
            float r = ring[k], th = ang[k * cols + c];
            float a = faceA(r);
            int within = c % 4;
            if (within == 1) a += 0.004f;  // spoke crown
            id[k * cols + c] = m.add(vec3(a, cosf(th) * r, sinf(th) * r), vec2(th * r, r));
        }
    m.uvMode = UV_EXPLICIT;
    m.use(d.faceMat, colv(d.faceTint));
    for (int k = 0; k + 1 < R; k++)
        for (int c = 0; c < cols; c++) {
            if (open(k, c)) continue;
            int c1 = (c + 1) % cols;
            u32 a = id[k * cols + c], b = id[(k + 1) * cols + c], cc = id[(k + 1) * cols + c1], dd = id[k * cols + c1];
            m.quadFacing(a, b, cc, dd, vec3(1, 0, 0));
        }
    // Side walls of the windows (depth back towards -X)
    float depth = 0.035f;
    auto wall = [&](u32 i0, u32 i1, vec3 facing) {
        vec3 p0 = m.P[i0], p1 = m.P[i1];
        u32 b0 = m.add(p0 - vec3(depth, 0, 0), vec2(p0.y, 0)), b1 = m.add(p1 - vec3(depth, 0, 0), vec2(p1.y, depth));
        u32 a0 = m.add(p0, vec2(p0.y, 0)), a1 = m.add(p1, vec2(p1.y, depth));
        m.quadFacing(a0, a1, b1, b0, facing);  // the back plate hides the hollow spoke backs
    };
    for (int k = 0; k + 1 < R; k++)
        for (int c = 0; c < cols; c++) {
            if (!open(k, c)) continue;
            int c1 = (c + 1) % cols;
            int cp = (c + cols - 1) % cols;
            float thMid = (ang[k * cols + c] + ang[k * cols + c1]) * 0.5f;
            vec3 radial(0, cosf(thMid), sinf(thMid));
            vec3 tang(0, -sinf(thMid), cosf(thMid));
            // radial edges at columns c (left) and c1 (right) if the neighbour is solid
            if (!open(k, cp)) wall(id[k * cols + c], id[(k + 1) * cols + c], tang);
            if (!open(k, c1)) wall(id[k * cols + c1], id[(k + 1) * cols + c1], -tang);
            if (k == 0 || !open(k - 1, c)) wall(id[k * cols + c], id[k * cols + c1], radial);
            if (k + 2 >= R || !open(k + 1, c)) wall(id[(k + 1) * cols + c], id[(k + 1) * cols + c1], -radial);
        }
    m.uvMode = UV_BOX;
}

inline void buildWheel(const WheelDesign& d, MeshData& out) {
    PMesh m;
    m.newGroup(50.f);
    buildTire(m, d);
    float w = d.W * 0.5f, rr = d.rimR;
    // ---- rim lip + barrel (lathe, outside on the left)
    m.newGroup(45.f);
    m.use(MAT_RIM, colv(d.lipTint));
    std::vector<vec2> lip;
    float lipA = d.moto ? w * 0.78f : w * 0.90f;
    lip.push_back(vec2(lipA - 0.010f, rr + 0.016f));
    lip.push_back(vec2(lipA + 0.007f, rr + 0.007f));
    lip.push_back(vec2(lipA - 0.002f, rr - 0.016f));
    lip.push_back(vec2(lipA - 0.03f, rr - 0.020f));
    lip.push_back(vec2(-w * 0.82f, rr - 0.020f));
    lip.push_back(vec2(-w * 0.92f, rr + 0.012f));
    lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), lip, d.seg);
    float rOut = rr - 0.018f;
    float aLip = lipA - 0.004f;
    // ---- face
    m.newGroup(38.f);
    if (d.style == RIM_WIRE) {
        // wire-spoke wheel: hub barrel + crossing spokes
        m.use(MAT_CHROME, kCol1);
        std::vector<vec2> hub;
        hub.push_back(vec2(-w * 0.55f, 0.03f));
        hub.push_back(vec2(-w * 0.55f, 0.055f));
        hub.push_back(vec2(-w * 0.35f, 0.06f));
        hub.push_back(vec2(w * 0.35f, 0.06f));
        hub.push_back(vec2(w * 0.55f, 0.055f));
        hub.push_back(vec2(w * 0.55f, 0.03f));
        lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), hub, 16);
        disk(m, vec3(w * 0.55f, 0, 0), vec3(1, 0, 0), 0.03f, 12);
        int ns = 36;
        for (int i = 0; i < ns; i++) {
            float side = (i & 1) ? 1.f : -1.f;
            float th0 = kTwoPi * i / ns;
            float th1 = th0 + side * 0.35f;
            vec3 a(side * w * 0.45f, cosf(th0) * 0.058f, sinf(th0) * 0.058f);
            vec3 b(side * w * 0.2f, cosf(th1) * (rr - 0.02f), sinf(th1) * (rr - 0.02f));
            cyl(m, a, b, 0.0022f, 4, false);
        }
    } else if (d.style == RIM_STEEL || d.style == RIM_TRUCK) {
        // pressed steel disc with vent holes; truck: deep hub + hand holes
        WheelDesign s = d;
        s.spokes = d.style == RIM_TRUCK ? 10 : 8;
        s.split = false;
        s.winIn = d.style == RIM_TRUCK ? 0.62f : 0.58f;
        s.winOut = d.style == RIM_TRUCK ? 0.74f : 0.72f;
        s.spokeHub = s.spokeRim = rr * (d.style == RIM_TRUCK ? 0.20f : 0.17f);
        s.concave = d.style == RIM_TRUCK ? 0.03f : 0.01f;
        s.dish = d.style == RIM_TRUCK ? 0.05f : 0.022f;
        buildSpokeFace(m, s, rOut, aLip);
        if (d.hubcap) {
            m.newGroup(40.f);
            m.use(MAT_METAL_PAINTED, col(0.75f, 0.76f, 0.78f));
            std::vector<vec2> cap;
            float a0 = aLip - s.dish + 0.004f;
            cap.push_back(vec2(a0 - 0.004f, rr * 0.52f));
            cap.push_back(vec2(a0 + 0.002f, rr * 0.50f));
            cap.push_back(vec2(a0 + 0.010f, rr * 0.42f));
            cap.push_back(vec2(a0 + 0.022f, rr * 0.30f));
            cap.push_back(vec2(a0 + 0.030f, rr * 0.18f));
            cap.push_back(vec2(a0 + 0.030f, 0.0f));
            lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), cap, 20);
        }
    } else {
        WheelDesign s = d;
        if (d.style == RIM_MESH) { s.spokes = Max(d.spokes, 12); s.spokeHub = 0.010f; s.spokeRim = 0.008f; s.split = false; }
        if (d.style == RIM_TURBINE) { s.spokes = Max(d.spokes, 10); s.split = false; }
        if (d.style == RIM_DISH) { s.dish = Max(d.dish, 0.045f); }
        if (d.style == RIM_CLASSIC) { s.spokes = 5; s.spokeHub = 0.036f; s.spokeRim = 0.036f; s.winIn = 0.45f; s.winOut = 0.80f; }
        if (d.style == RIM_BIKE) { s.dish = 0.004f; s.concave = 0.0f; s.winIn = 0.30f; s.winOut = 0.92f; }
        buildSpokeFace(m, s, rOut, aLip);
        if (d.style == RIM_DISH || d.style == RIM_CLASSIC) {
            // polished step ring between the lip and the recessed face
            m.newGroup(30.f);
            m.use(MAT_CHROME, kCol1);
            std::vector<vec2> st;
            st.push_back(vec2(aLip, rOut + 0.004f));
            st.push_back(vec2(aLip - s.dish, rOut - 0.002f));
            lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), st, d.seg);
        }
    }
    // ---- hub: centre cap + lug nuts
    if (d.style != RIM_WIRE && !d.hubcap) {
        float aFace = aLip - (d.style == RIM_TRUCK ? 0.05f : d.dish) + (d.style == RIM_TRUCK ? 0.03f : 0.012f);
        m.newGroup(35.f);
        m.use(MAT_RIM, colv(d.capTint));
        std::vector<vec2> cap;
        float cr = rr * (d.style == RIM_TRUCK ? 0.22f : 0.16f);
        cap.push_back(vec2(aFace - 0.004f, cr * 1.05f));
        cap.push_back(vec2(aFace + 0.006f, cr));
        cap.push_back(vec2(aFace + 0.011f, cr * 0.6f));
        cap.push_back(vec2(aFace + 0.012f, 0.f));
        lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), cap, 16);
        m.use(MAT_CHROME, kCol1);
        float lr = rr * (d.style == RIM_TRUCK ? 0.36f : 0.25f);
        for (int i = 0; i < d.lugs; i++) {
            float th = kTwoPi * (i + 0.5f) / d.lugs;
            vec3 c(aFace - 0.004f, cosf(th) * lr, sinf(th) * lr);
            std::vector<vec2> nut;
            nut.push_back(vec2(0.0f, 0.0105f));
            nut.push_back(vec2(0.016f, 0.0105f));
            nut.push_back(vec2(0.021f, 0.0f));
            lathe(m, c, vec3(1, 0, 0), vec3(0, 1, 0), nut, 6);
        }
    }
    // ---- brake disc behind the face + back plate closing the wheel from the inside
    if (d.disc && !d.moto) {
        m.newGroup(30.f);
        m.use(MAT_METAL_BRUSHED, col(0.55f, 0.55f, 0.56f));
        float rd = rr * 0.80f;
        float a0 = -0.01f;
        std::vector<vec2> dp;
        dp.push_back(vec2(a0 - 0.014f, rd));
        dp.push_back(vec2(a0 + 0.012f, rd));
        dp.push_back(vec2(a0 + 0.012f, rr * 0.30f));
        lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), dp, 24);
        m.use(MAT_METAL_PAINTED, col(0.18f, 0.18f, 0.19f));
        std::vector<vec2> hat;
        hat.push_back(vec2(a0 + 0.012f, rr * 0.31f));
        hat.push_back(vec2(a0 + 0.035f, rr * 0.28f));
        hat.push_back(vec2(a0 + 0.035f, 0.0f));
        lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), hat, 16);
    } else if (d.moto) {
        m.newGroup(30.f);
        m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
        float rd = rr * 0.72f;
        std::vector<vec2> dp;
        dp.push_back(vec2(w * 0.45f, rd));
        dp.push_back(vec2(w * 0.55f, rd));
        dp.push_back(vec2(w * 0.55f, rr * 0.42f));
        lathe(m, vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), dp, 24);
    }
    m.newGroup(30.f);
    m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
    disk(m, vec3(-w * 0.80f, 0, 0), vec3(-1, 0, 0), rr - 0.01f, 16);
    m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.12f));
    disk(m, vec3(-w * 0.79f, 0, 0), vec3(1, 0, 0), rr - 0.01f, 16);
    finalizeMesh(m, out);
}

}  // namespace detail
}  // namespace Vehicles
