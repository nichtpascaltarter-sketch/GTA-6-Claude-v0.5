// Vehicle tuning derived from VehicleModel metadata, placement, init/reset. Included by vehicle_sim.cpp.
namespace Vehicles {
namespace vsim {

// Handling character per class (suspension feel, assists, steering lock, tire, drivetrain).
struct ClassParams {
    float freq;       // suspension natural frequency (Hz) at suspensionStiffness 1
    float restFrac;   // rest compression as a fraction of travel
    float zetaB, zetaR;
    float arbF, arbR; // anti-roll bar rate relative to the wheel rate (front / rear axle)
    float maxSteer;   // steering lock (rad)
    float rollArm;    // lateral tire force height (0 contact .. 1 COM): lower roll moment, arcade stability
    float esc;        // stability assist
    float tcSlip;     // traction control slip limit
    float alphaPeak;  // slip angle at peak lateral force (rad)
    float inertia;    // box inertia scale
    float shift;      // gear shift time (s)
    float damage;     // damage multiplier
};

ClassParams classParams(VehicleClass c) {
    //                         freq  rest  zB    zR    arbF arbR  lock  rArm  esc   tc    aPk    inrt  shift dmg
    static const ClassParams kCompact  = {1.50f, 0.40f, 0.35f, 0.60f, 0.25f, 0.12f, 0.66f, 0.15f, 1.0f, 0.20f, 0.130f, 0.80f, 0.25f, 1.1f};
    static const ClassParams kSedan    = {1.40f, 0.40f, 0.35f, 0.60f, 0.20f, 0.10f, 0.62f, 0.15f, 1.0f, 0.20f, 0.130f, 0.80f, 0.25f, 1.0f};
    static const ClassParams kCoupe    = {1.55f, 0.40f, 0.36f, 0.62f, 0.40f, 0.25f, 0.60f, 0.25f, 0.8f, 0.25f, 0.125f, 0.80f, 0.20f, 1.0f};
    static const ClassParams kSuv      = {1.30f, 0.40f, 0.35f, 0.60f, 0.30f, 0.20f, 0.60f, 0.10f, 1.1f, 0.20f, 0.140f, 0.80f, 0.28f, 0.9f};
    static const ClassParams kPickup   = {1.30f, 0.40f, 0.35f, 0.60f, 0.30f, 0.15f, 0.60f, 0.12f, 0.9f, 0.25f, 0.140f, 0.80f, 0.28f, 0.9f};
    static const ClassParams kSports   = {1.75f, 0.40f, 0.40f, 0.65f, 0.40f, 0.30f, 0.58f, 0.28f, 0.6f, 0.30f, 0.120f, 0.80f, 0.15f, 1.0f};
    static const ClassParams kSuper    = {1.90f, 0.40f, 0.40f, 0.65f, 0.50f, 0.40f, 0.56f, 0.32f, 0.6f, 0.30f, 0.115f, 0.80f, 0.12f, 1.0f};
    static const ClassParams kMuscle   = {1.45f, 0.40f, 0.33f, 0.58f, 0.20f, 0.05f, 0.60f, 0.12f, 0.35f, 0.45f, 0.130f, 0.80f, 0.22f, 0.9f};
    static const ClassParams kVan      = {1.35f, 0.40f, 0.35f, 0.60f, 0.35f, 0.25f, 0.62f, 0.15f, 1.1f, 0.20f, 0.140f, 0.80f, 0.30f, 0.9f};
    static const ClassParams kBus      = {1.20f, 0.40f, 0.35f, 0.60f, 0.70f, 0.50f, 0.62f, 0.25f, 1.3f, 0.15f, 0.150f, 0.85f, 0.40f, 0.6f};
    static const ClassParams kTruck    = {1.20f, 0.40f, 0.35f, 0.60f, 0.70f, 0.50f, 0.58f, 0.25f, 1.3f, 0.15f, 0.150f, 0.85f, 0.45f, 0.55f};
    static const ClassParams kService  = {1.30f, 0.40f, 0.35f, 0.60f, 0.40f, 0.30f, 0.60f, 0.18f, 1.1f, 0.20f, 0.140f, 0.80f, 0.30f, 0.8f};
    static const ClassParams kPolice   = {1.50f, 0.40f, 0.36f, 0.62f, 0.40f, 0.25f, 0.62f, 0.25f, 0.9f, 0.25f, 0.125f, 0.80f, 0.20f, 0.85f};
    static const ClassParams kBike     = {1.70f, 0.35f, 0.40f, 0.60f, 0.00f, 0.00f, 0.50f, 0.00f, 0.0f, 0.30f, 0.160f, 1.00f, 0.12f, 1.2f};
    static const ClassParams kScooter  = {1.60f, 0.35f, 0.40f, 0.60f, 0.00f, 0.00f, 0.60f, 0.00f, 0.0f, 0.30f, 0.160f, 1.00f, 0.10f, 1.2f};
    static const ClassParams kBoat     = {1.50f, 0.40f, 0.40f, 0.60f, 0.00f, 0.00f, 0.50f, 0.40f, 0.0f, 0.30f, 0.130f, 0.90f, 0.20f, 0.8f};
    static const ClassParams kPlane    = {1.60f, 0.40f, 0.40f, 0.70f, 0.50f, 0.50f, 0.90f, 0.50f, 0.0f, 1.00f, 0.130f, 0.75f, 0.20f, 1.0f};
    static const ClassParams kHeli     = {2.00f, 0.40f, 0.50f, 0.80f, 0.50f, 0.50f, 0.00f, 0.50f, 0.0f, 1.00f, 0.130f, 0.70f, 0.20f, 1.0f};
    switch (c) {
        case VC_COMPACT: return kCompact;
        case VC_SEDAN: case VC_TAXI: return kSedan;
        case VC_COUPE: return kCoupe;
        case VC_SUV: return kSuv;
        case VC_PICKUP: return kPickup;
        case VC_SPORTS: return kSports;
        case VC_SUPER: return kSuper;
        case VC_MUSCLE: return kMuscle;
        case VC_VAN: case VC_AMBULANCE: return kVan;
        case VC_BUS: return kBus;
        case VC_TRUCK: case VC_FIRETRUCK: return kTruck;
        case VC_SERVICE: return kService;
        case VC_POLICE: return kPolice;
        case VC_MOTORBIKE: return kBike;
        case VC_SCOOTER: return kScooter;
        case VC_BOAT: case VC_JETSKI: case VC_AIRBOAT: return kBoat;
        case VC_PLANE: return kPlane;
        case VC_HELI: return kHeli;
        default: return kSedan;
    }
}

float wheelTravel(const VehicleState& s, int i) {
    (void)i;
    float tr = s.model->suspensionTravel;
    if (isAirClass(s.cls)) tr = Max(tr, 0.12f);
    return Clamp(tr, 0.04f, 0.6f);
}

// Rider mass added to single-track vehicles (their models describe the machine only).
inline float riderMass(VehicleClass c) { return (isBikeClass(c) || c == VC_JETSKI) ? 75.f : 0.f; }

// Minimum-norm static wheel loads satisfying force and moment balance (handles bikes / multi-axle trucks).
void staticLoads(const VehicleModel& m, int nw, vec3 com, float weight, float* out) {
    // A = [1; x; y] (3 x n); L = A^T (A A^T + eps)^-1 b
    double M[3][3] = {}, bvec[3] = {weight, weight * com.x, weight * com.y};
    for (int i = 0; i < nw; i++) {
        double a[3] = {1.0, m.wheels[i].pos.x, m.wheels[i].pos.y};
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++) M[r][k] += a[r] * a[k];
    }
    for (int r = 0; r < 3; r++) M[r][r] += 1e-4;
    // solve M z = b (Gaussian elimination with partial pivoting)
    double A[3][4];
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++) A[r][k] = M[r][k];
        A[r][3] = bvec[r];
    }
    for (int col = 0; col < 3; col++) {
        int piv = col;
        for (int r = col + 1; r < 3; r++)
            if (fabs(A[r][col]) > fabs(A[piv][col])) piv = r;
        for (int k = 0; k < 4; k++) std::swap(A[col][k], A[piv][k]);
        double d = A[col][col];
        if (fabs(d) < 1e-12) continue;
        for (int r = 0; r < 3; r++) {
            if (r == col) continue;
            double f = A[r][col] / d;
            for (int k = col; k < 4; k++) A[r][k] -= f * A[col][k];
        }
    }
    double z[3];
    for (int r = 0; r < 3; r++) z[r] = fabs(A[r][r]) > 1e-12 ? A[r][3] / A[r][r] : 0.0;
    float sum = 0.f;
    for (int i = 0; i < nw; i++) {
        float L = (float)(z[0] + z[1] * m.wheels[i].pos.x + z[2] * m.wheels[i].pos.y);
        out[i] = Max(L, 0.08f * weight / nw);
        sum += out[i];
    }
    for (int i = 0; i < nw; i++) out[i] *= weight / Max(sum, 1e-3f);
}

void deriveTuning(VehicleState& s, const VehicleModel& m) {
    VehicleTuning& t = s.tune;
    t = VehicleTuning();
    const ClassParams cp = classParams(m.cls);
    float massM = Max(m.mass, 20.f);
    float mass = massM + riderMass(m.cls);
    // collision box (fallback from the wheels when the model has none)
    t.boxC = m.boxCenter;
    t.boxH = m.boxHalf;
    if (t.boxH.x < 0.05f || t.boxH.y < 0.05f || t.boxH.z < 0.05f) {
        vec3 mn(1e9f), mx(-1e9f);
        for (auto& w : m.wheels) {
            mn = vmin(mn, w.pos - vec3(w.width * 0.5f, w.radius, w.radius));
            mx = vmax(mx, w.pos + vec3(w.width * 0.5f, w.radius, w.radius));
        }
        if (m.wheels.empty()) { mn = vec3(-0.9f, -2.2f, 0.2f); mx = vec3(0.9f, 2.2f, 1.5f); }
        t.boxC = (mn + mx) * 0.5f + vec3(0, 0, 0.4f);
        t.boxH = vmax((mx - mn) * 0.5f, vec3(0.3f, 0.5f, 0.5f));
    }
    t.boxH = vmax(t.boxH, vec3(0.1f));
    // center of mass (+ rider for single-track vehicles)
    t.com = m.centerOfMass;
    if (riderMass(m.cls) > 0.f) {
        vec3 seat = vec3(0.f, -0.1f, 0.9f);
        for (auto& st : m.seats)
            if (st.driver) seat = st.pos;
        t.com = (m.centerOfMass * massM + (seat + vec3(0, 0, 0.25f)) * riderMass(m.cls)) / mass;
    }
    t.boundR = length(t.boxH) + length(t.boxC - t.com) + 0.25f;
    t.inertiaScale = cp.inertia;
    // ---- wheels ----
    int nw = Min((int)m.wheels.size(), kMaxWheels);
    s.wheelCount = nw;
    float loads[kMaxWheels] = {};
    if (nw > 0) staticLoads(m, nw, t.com, mass * kGrav, loads);
    float freq = cp.freq * sqrtf(Clamp(m.suspensionStiffness, 0.3f, 4.f));
    float travel = nw ? wheelTravel(s, 0) : 0.2f;
    float fy = -1e9f, ry = 1e9f, trackSum = 0.f;
    int trackN = 0;
    float driveR = 0.f;
    int driveN = 0;
    int nFrontDriven = 0, nRearDriven = 0;
    for (int i = 0; i < nw; i++) {
        const WheelSpec& ws = m.wheels[i];
        bool front = ws.pos.y > t.com.y;
        if (ws.drive) (front ? nFrontDriven : nRearDriven)++;
    }
    float frontShare = nFrontDriven && nRearDriven ? Clamp(m.driveFront, 0.f, 1.f) : (nFrontDriven ? 1.f : 0.f);
    if (nFrontDriven && nRearDriven && m.driveFront <= 0.f) frontShare = 0.f;
    float mWheel = Clamp(massM * 0.012f, 4.f, 90.f);
    for (int i = 0; i < nw; i++) {
        const WheelSpec& ws = m.wheels[i];
        float mi = loads[i] / kGrav;
        float w = kTwoPi * freq;
        t.staticLoad[i] = loads[i];
        t.springK[i] = mi * w * w;
        t.restComp[i] = cp.restFrac * travel;
        float cc = 2.f * sqrtf(t.springK[i] * mi);
        t.dampBump[i] = cp.zetaB * cc;
        t.dampRebound[i] = cp.zetaR * cc;
        t.wheelI[i] = 0.6f * mWheel * ws.radius * ws.radius;
        bool front = ws.pos.y > t.com.y;
        t.rear[i] = !front;
        if (ws.drive) {
            t.driveShare[i] = front ? frontShare / nFrontDriven : (1.f - frontShare) / Max(nRearDriven, 1);
            driveR += ws.radius;
            driveN++;
        }
        float bias = isBikeClass(m.cls) ? (front ? 1.3f : 0.7f) : (front ? 1.15f : 0.85f);
        t.brakeT[i] = Max(m.brakeForce, 500.f) * ws.radius * bias;
        t.arbPair[i] = -1;
        if (ws.steer) fy = Max(fy, ws.pos.y);
        else ry = Min(ry, ws.pos.y);
    }
    // normalize brake bias so the total equals brakeForce * n
    {
        float sum = 0.f, want = 0.f;
        for (int i = 0; i < nw; i++) {
            sum += t.brakeT[i];
            want += Max(m.brakeForce, 500.f) * m.wheels[i].radius;
        }
        for (int i = 0; i < nw; i++) t.brakeT[i] *= want / Max(sum, 1e-3f);
    }
    // anti-roll bar pairs (left/right wheels on the same axle)
    for (int i = 0; i < nw; i++) {
        if (t.arbPair[i] >= 0 || !m.wheels[i].left) continue;
        for (int j = 0; j < nw; j++) {
            if (j == i || m.wheels[j].left || t.arbPair[j] >= 0) continue;
            if (fabsf(m.wheels[i].pos.y - m.wheels[j].pos.y) > 0.25f) continue;
            t.arbPair[i] = (signed char)j;
            t.arbPair[j] = (signed char)i;
            float f = m.wheels[i].pos.y > t.com.y ? cp.arbF : cp.arbR;
            t.arbK[i] = t.arbK[j] = f * 0.5f * (t.springK[i] + t.springK[j]);
            trackSum += fabsf(m.wheels[i].pos.x - m.wheels[j].pos.x);
            trackN++;
            break;
        }
    }
    if (fy < -1e8f) fy = nw ? m.wheels[0].pos.y : 1.3f;
    if (ry > 1e8f) {
        ry = fy;
        for (int i = 0; i < nw; i++) ry = Min(ry, m.wheels[i].pos.y);
    }
    t.frontY = fy;
    t.rearY = ry;
    t.wheelbase = Max(fy - ry, 0.8f);
    t.track = trackN ? trackSum / trackN : 1.5f;
    t.maxSteer = cp.maxSteer;
    t.rollArm = cp.rollArm;
    t.esc = cp.esc;
    t.tcSlip = cp.tcSlip;
    t.alphaPeak = cp.alphaPeak;
    t.kappaPeak = isBikeClass(m.cls) ? 0.12f : 0.10f;
    // ---- powertrain ----
    t.driveRadius = driveN ? driveR / driveN : (nw ? m.wheels[0].radius : 0.33f);
    t.maxRpm = Clamp(m.maxRpm, 1500.f, 20000.f);
    t.idleRpm = Clamp(t.maxRpm * 0.13f, 550.f, 1300.f);
    t.peakPowerW = Max(m.power, 1.f) * 1000.f;
    float wMax = t.maxRpm * kTwoPi / 60.f;
    t.peakTorque = Max(m.torque, t.peakPowerW / (0.9f * wMax));
    t.engineI = 0.08f + 0.0006f * t.peakTorque;
    t.gears = Clamp(m.gears, 1, 10);
    t.shiftTime = cp.shift;
    float top = Max(m.topSpeed, 5.f);
    float topRatio = 0.9f * wMax / (top / t.driveRadius);
    float spread = t.gears > 1 ? Clamp(0.9f + 0.55f * t.gears, 2.2f, 6.2f) : 1.f;
    t.ratio[1] = topRatio * spread;
    for (int g = 2; g <= t.gears; g++) t.ratio[g] = t.ratio[1] * powf(topRatio / t.ratio[1], (float)(g - 1) / (float)(t.gears - 1));
    t.ratio[0] = t.ratio[1] * 0.95f;
    // ---- aero ----
    t.dragArea = Max(m.dragCoef * m.frontalArea, 0.05f);
    t.liftArea = Max(m.downforce, 0.f) * Max(m.frontalArea, 0.5f);
    // ---- water ----
    float zBottom = t.boxC.z - t.boxH.z, zTop = t.boxC.z + t.boxH.z;
    int nf = Min((int)m.floatPoints.size(), kMaxFloats);
    if (isBoatClass(m.cls)) {
        if (nf == 0) {
            const float ys[3] = {-0.75f, 0.f, 0.75f};
            for (int k = 0; k < 3; k++)
                for (int sx = -1; sx <= 1; sx += 2) t.floatPt[nf++] = vec3(sx * 0.65f * t.boxH.x, t.boxC.y + ys[k] * t.boxH.y, zBottom);
        } else {
            for (int k = 0; k < nf; k++) t.floatPt[k] = m.floatPoints[k];
        }
        t.floatCount = nf;
        float zb = 1e9f;
        for (int k = 0; k < nf; k++) zb = Min(zb, t.floatPt[k].z);
        // design waterline: z = 0 when the model origin sits at the waterline (float points below it),
        // otherwise 30% up the hull from the lowest point.
        float wl0 = zb < -0.05f ? 0.f : zb + 0.3f * Max(zTop - zb, 0.3f);
        float sumDraft = 0.f;
        for (int k = 0; k < nf; k++) {
            t.floatDraft[k] = Max(wl0 - t.floatPt[k].z, 0.04f);
            t.floatMax[k] = Max(zTop - t.floatPt[k].z, t.floatDraft[k] * 1.6f);
            sumDraft += t.floatDraft[k];
        }
        t.floatK = mass * kGrav / Max(sumDraft, 1e-3f);
        t.floatDamp = 0.55f * 2.f * sqrtf(t.floatK * mass / Max(nf, 1));
        // propulsion: boat prop / jet intake at the stern bottom, airboat fan above the deck
        float sternY = t.boxC.y - t.boxH.y * 0.92f;
        if (m.cls == VC_AIRBOAT) t.propPos = length(m.rotorPos) > 0.01f ? m.rotorPos : vec3(0.f, sternY + 0.3f, zTop + 0.6f);
        else t.propPos = length(m.rotorPos) > 0.01f && m.rotorPos.z < zb + 0.5f ? m.rotorPos : vec3(0.f, sternY, zb + 0.02f);
        float top = Max(m.topSpeed, 4.f);
        float eta = m.cls == VC_AIRBOAT ? 0.45f : 0.5f;
        float vref = Max(0.5f * top, 5.f);
        t.thrustStatic = eta * t.peakPowerW / vref;
        float Ttop = eta * t.peakPowerW / top;
        float lin = 0.02f * mass;
        t.planeSpeed = Clamp((m.cls == VC_JETSKI ? 0.33f : (m.cls == VC_AIRBOAT ? 0.3f : 0.4f)) * top, 4.f, 14.f);
        t.hullDragX = Max((Ttop - lin * top) / (0.55f * top * top), 0.5f);
        t.hullDragY = t.hullDragX * 25.f + 0.4f * mass / 10.f;
    }
    // ---- aircraft ----
    if (m.cls == VC_PLANE) {
        t.wingArea = m.wingArea > 0.5f ? m.wingArea : mass / 75.f;
        t.liftSlope = m.liftSlope > 0.5f ? m.liftSlope : 5.0f;
        t.inducedK = 1.f / (kPi * 0.8f * 7.5f);
        float top = Max(m.topSpeed, 30.f);
        bool jet = top > 115.f;
        t.thrustStatic = jet ? Max(0.35f * mass * kGrav, t.peakPowerW / top) : 19.f * m.power;
        // parasitic drag area chosen so that full throttle level flight tops out at topSpeed
        float q = 0.5f * kRhoAir * top * top;
        float Ttop = jet ? t.thrustStatic * 0.85f : Min(t.thrustStatic * 0.7f, 0.8f * t.peakPowerW / top);
        float CL = mass * kGrav / (q * t.wingArea);
        float induced = q * t.wingArea * t.inducedK * CL * CL;
        t.dragArea = Max((Ttop - induced) / q, 0.012f * t.wingArea);
    }
    if (m.cls == VC_HELI) {
        t.rotorR = m.rotorRadius > 0.5f ? m.rotorRadius : Max(t.boxH.y * 1.1f, 4.f);
        float top = Max(m.topSpeed, 20.f);
        // 30 degrees of disc tilt holds altitude and balances drag at top speed
        t.heliDragArea = 0.577f * mass * kGrav / (0.5f * kRhoAir * top * top);
    }
    // ---- rigid body ----
    s.body.setBoxInertia(mass, t.boxH);
    s.body.invInertiaLocal = s.body.invInertiaLocal / t.inertiaScale;
    if (isBikeClass(m.cls)) {
        // single-track: the rider adds roll inertia high up; keep yaw agile
        s.body.invInertiaLocal.y *= 0.8f;
    }
}

// Height of the model origin above the supporting surface when resting on it.
float restOffset(const VehicleState& s) {
    const VehicleModel& m = *s.model;
    if (s.wheelCount > 0 && !isBoatClass(s.cls)) {
        float lo = 1e9f;
        for (int i = 0; i < s.wheelCount; i++) lo = Min(lo, m.wheels[i].pos.z - m.wheels[i].radius);
        return -lo;
    }
    float lo = s.tune.boxC.z - s.tune.boxH.z;
    for (int k = 0; k < s.tune.floatCount; k++) lo = Min(lo, s.tune.floatPt[k].z);
    return -lo + 0.01f;
}

void placeVehicle(VehicleState& s, dvec3 pos, float yaw) {
    const VehicleTuning& t = s.tune;
    quat qYaw = quatAxisAngle(vec3(0, 0, 1), yaw);
    float x = (float)pos.x, y = (float)pos.y;
    double z = pos.z;
    quat q = qYaw;
    bool floating = false;
    if (isBoatClass(s.cls) && World::gMap->isWater(x, y)) {
        float wz = World::gMap->waterAt(x, y);
        Phys::waterSurface(x, y, wz);
        float zb = 1e9f;
        for (int k = 0; k < t.floatCount; k++) zb = Min(zb, t.floatPt[k].z);
        float wl0 = zb < -0.05f ? 0.f : zb + 0.3f * Max(t.boxC.z + t.boxH.z - zb, 0.3f);
        z = wz - wl0;
        floating = true;
    }
    if (!floating) {
        Phys::GroundHit g = Phys::gCollision->ground(x, y, (float)pos.z + 1.5f, 0.f);
        if (g.z < -1e8f) g.z = (float)pos.z;
        z = g.z + restOffset(s);
        if (g.normal.z > 0.85f) q = normalize(quatFromTo(vec3(0, 0, 1), g.normal) * qYaw);
    }
    s.body.pos = dvec3(pos.x, pos.y, z);
    s.body.rot = q;
    s.body.vel = vec3(0.f);
    s.body.angVel = vec3(0.f);
    s.body.force = vec3(0.f);
    s.body.torque = vec3(0.f);
    for (int i = 0; i < s.wheelCount; i++) {
        WheelState& w = s.wheels[i];
        w = WheelState();
        w.compression = t.restComp[i];
        w.contact = !floating;
    }
    mat3 R = mat3FromQuat(q);
    for (int k = 0; k < t.floatCount; k++) {
        vec3 p = s.body.pos.toVec3() + R * t.floatPt[k];
        float wz = p.z;
        if (Phys::waterSurface(p.x, p.y, wz)) s.waterZPrev[k] = wz;
        else s.waterZPrev[k] = p.z;
    }
}

}  // namespace vsim

void resetVehicle(VehicleState& s, dvec3 pos, float yaw) {
    if (!s.model) return;
    vsim::placeVehicle(s, pos, yaw);
    s.gear = 1;
    s.pendingGear = 1;
    s.shiftTimer = 0.f;
    s.throttleOut = 0.f;
    s.engineLoad = 0.f;
    s.engineRpm = s.engineOn ? s.tune.idleRpm : 0.f;
    s.engineFlooded = false;
    s.riderOff = false;
    s.inWater = false;
    s.wasInWater = false;
    s.submerged = 0.f;
    s.waterDepth = 0.f;
    s.floodTimer = 0.f;
    s.airborneTime = 0.f;
    s.upsideDownTime = 0.f;
    s.sleeping = false;
    s.sleepTimer = 0.f;
    s.reverseTimer = 0.f;
    s.intakeTimer = 0.f;
    s.ejectTimer = 0.f;
    s.hbTimer = 10.f;
    s.gearDown = 1.f;
    s.lean = 0.f;
    s.leanCmd = 0.f;
    s.steerOut = 0.f;
    s.stall = 0.f;
    s.heliYawTarget = 0.f;
    vsim::clearEvents(s);
}

void initVehicle(VehicleState& s, const VehicleModel& m, int modelIndex, dvec3 pos, float yaw) {
    s = VehicleState();
    s.model = &m;
    s.modelIndex = modelIndex;
    s.cls = m.cls;
    vsim::deriveTuning(s, m);
    s.engineOn = true;
    s.health = 1000.f;
    s.engineHealth = 1000.f;
    for (float& d : s.damageZones) d = 0.f;
    s.wrecked = false;
    s.rotorSpeed = 0.f;
    resetVehicle(s, pos, yaw);
}

}  // namespace Vehicles
