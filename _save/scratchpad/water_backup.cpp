// Buoyancy (on the CPU wave mirror), planing hulls, propulsion; land vehicles in water. Included by vehicle_sim.cpp.
namespace Vehicles {
namespace vsim {

// Buoyant columns along the 4 vertical box edges against a (flat) water level. Returns the submerged fraction.
float boxBuoyancy(StepCtx& x, float level, float weightFactor) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    float sub = 0.f;
    for (int k = 0; k < 4; k++) {
        vec3 lo = t.boxC + vec3(k & 1 ? t.boxH.x : -t.boxH.x, k & 2 ? t.boxH.y : -t.boxH.y, -t.boxH.z);
        vec3 hi = lo + vec3(0.f, 0.f, 2.f * t.boxH.z);
        vec3 rl = b.R * (lo - t.com), rh = b.R * (hi - t.com);
        vec3 pl = x.comW + rl, ph = x.comW + rh;
        float z0 = Min(pl.z, ph.z), z1 = Max(pl.z, ph.z);
        float imm = Saturate((level - z0) / Max(z1 - z0, 0.25f));
        if (imm <= 0.f) continue;
        sub += imm * 0.25f;
        vec3 bottom = pl.z < ph.z ? rl : rh, top = pl.z < ph.z ? rh : rl;
        vec3 r = lerp(bottom, top, imm * 0.5f);
        b.addForce(vec3(0.f, 0.f, b.mass * kGrav * weightFactor * 0.25f * imm), r);
    }
    return sub;
}

// Quadratic water drag on the box faces + angular damping, scaled by the submerged fraction.
void waterDrag(StepCtx& x, float sub, float cd) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    vec3 H = t.boxH;
    vec3 area(4.f * H.y * H.z, 4.f * H.x * H.z, 4.f * H.x * H.y);
    vec3 vl = b.local(b.vel);
    vec3 f = vec3(-vl.x * fabsf(vl.x) * area.x, -vl.y * fabsf(vl.y) * area.y, -vl.z * fabsf(vl.z) * area.z) * (0.5f * kRhoWater * cd * sub);
    f -= vl * (0.3f * b.mass * sub);
    // never reverse the velocity within a step
    for (int a = 0; a < 3; a++) {
        float lim = fabsf(vl[a]) * b.mass / x.dt * 0.5f;
        f[a] = Clamp(f[a], -lim, lim);
    }
    b.force += b.R * f;
    b.torque += b.torqueFor(b.angVel * (-2.5f * sub));
}

// Cars, bikes and aircraft in water: float briefly, flood and sink; engines drown.
void swampForces(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    float level = World::gMap->waterAt(x.comW.x, x.comW.y);
    float ext = fabsf(b.R.c[0].z) * t.boxH.x + fabsf(b.R.c[1].z) * t.boxH.y + fabsf(b.R.c[2].z) * t.boxH.z;
    float bcz = x.comW.z + (b.R * (t.boxC - t.com)).z;
    if (level <= World::kNoWater + 1.f || bcz - ext > level) {
        s.inWater = false;
        s.submerged = 0.f;
        s.waterDepth = 0.f;
        s.wasInWater = false;
        if (s.floodTimer > 0.f) s.floodTimer = Max(0.f, s.floodTimer - x.dt * 0.2f);
        return;
    }
    bool plane = s.cls == VC_PLANE;
    // sealed cabin floats first, then floods and sinks (planes float much longer)
    float floodTime = plane ? 40.f : 12.f;
    float buoy = (plane ? 1.8f : 1.6f) * (1.f - 0.72f * Saturate(s.floodTimer / floodTime));
    float sub = boxBuoyancy(x, level, buoy);
    s.submerged = sub;
    s.inWater = sub > 0.05f;
    s.waterDepth = Max(0.f, level - (float)(b.pos.z - (b.R * t.com).z));
    if (sub > 0.2f) s.floodTimer += x.dt;
    waterDrag(x, sub, 0.8f);
    // engine intake under water for more than ~1.5 s -> engine dead
    vec3 eng;
    if (isBikeClass(s.cls)) eng = t.com;
    else if (s.cls == VC_SUPER || s.cls == VC_BUS) eng = vec3(0.f, t.boxC.y - 0.6f * t.boxH.y, t.boxC.z + 0.4f * t.boxH.z);
    else eng = vec3(0.f, t.boxC.y + 0.6f * t.boxH.y, t.boxC.z + 0.4f * t.boxH.z);
    float ez = x.comW.z + (b.R * (eng - t.com)).z;
    if (ez < level) {
        s.intakeTimer += x.dt;
        if (s.intakeTimer > 1.5f) {
            s.engineFlooded = true;
            s.engineOn = false;
        }
    } else {
        s.intakeTimer = 0.f;
    }
    if (!s.wasInWater && s.inWater) s.splash = Max(s.splash, fabsf(b.vel.z) + 0.3f * sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y));
    s.wasInWater = s.inWater;
}

// Boats, jetskis and airboats.
void boatForces(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleModel& m = *x.m;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    Body& b = x.b;
    float dt = x.dt;
    bool jetski = s.cls == VC_JETSKI, airboat = s.cls == VC_AIRBOAT;
    bool canRun = !s.engineFlooded && s.engineHealth > 0.f && !s.wrecked;
    if (c.engineOff || !canRun) s.engineOn = false;
    else if (!s.engineOn && c.hasDriver && (c.throttle > 0.05f || c.brake > 0.05f)) s.engineOn = true;
    // ---- buoyancy at the float points on the wave surface ----
    float sumImm = 0.f, sumMax = 0.f, sumDraftImm = 0.f, sumDraft = 0.f;
    int wet = 0;
    for (int k = 0; k < t.floatCount; k++) {
        vec3 r = b.R * (t.floatPt[k] - t.com);
        vec3 p = x.comW + r;
        sumMax += t.floatMax[k];
        sumDraft += t.floatDraft[k];
        float wz;
        vec3 wn;
        if (!Phys::waterSurface(p.x, p.y, wz, &wn)) {
            s.waterZPrev[k] = -1e6f;
            continue;
        }
        float vWater = s.waterZPrev[k] > -1e5f ? Clamp((wz - s.waterZPrev[k]) / dt, -4.f, 4.f) : 0.f;
        s.waterZPrev[k] = wz;
        float imm = wz - p.z;
        if (imm <= 0.f) continue;
        wet++;
        float immC = Min(imm, t.floatMax[k]);
        sumImm += immC;
        sumDraftImm += Min(imm, t.floatDraft[k]);
        float F = t.floatK * immC;
        float vz = b.velAt(r).z - vWater;
        float Fd = -t.floatDamp * vz * Saturate(imm / t.floatDraft[k]);
        b.addForce(vec3(wn.x * F * 0.5f, wn.y * F * 0.5f, F + Fd), r);
    }
    s.submerged = sumMax > 0.f ? sumImm / sumMax : 0.f;
    float wetFrac = sumDraft > 0.f ? sumDraftImm / sumDraft : 0.f;
    bool wasIn = s.inWater;
    s.inWater = wet > 0;
    if (!wasIn && s.inWater && x.speed > 2.f) s.splash = Max(s.splash, fabsf(b.vel.z) + 0.2f * x.speed);
    s.wasInWater = s.inWater;
    // capsized hull: the deck/box still floats and the hull rights itself (arcade)
    if (x.up.z < 0.35f) {
        float level = World::gMap->waterAt(x.comW.x, x.comW.y);
        if (level > World::kNoWater + 1.f) {
            float sub = boxBuoyancy(x, level, 1.2f);
            if (sub > 0.05f) {
                s.inWater = true;
                vec3 ax = cross(x.up, vec3(0, 0, 1));
                float dir = dot(ax, x.fwd);
                float sgn = fabsf(dir) > 0.1f ? (dir > 0.f ? 1.f : -1.f) : 1.f;
                b.torque += b.torqueFor(x.fwd * (sgn * 2.5f)) - b.torqueFor(x.fwd * (dot(b.angVel, x.fwd) * 1.5f));
                wetFrac = Max(wetFrac, 0.5f);
            }
        }
    }
    vec3 vl = x.vLocal;
    float vf = vl.y;
    float L = 2.f * t.boxH.y;
    float planing = SmoothStep(0.55f * t.planeSpeed, t.planeSpeed, fabsf(vf));
    vec3 wl = b.local(b.angVel);
    if (s.inWater && wetFrac > 0.f) {
        float wf = Saturate(wetFrac * 1.25f);
        // longitudinal resistance (drops when planing) + hump before planing
        float fLong = -(t.hullDragX * (1.f - 0.45f * planing) * vf * fabsf(vf) + 0.02f * b.mass * vf) * wf;
        float hump = expf(-Sq((fabsf(vf) / t.planeSpeed - 0.65f) / 0.3f));
        fLong -= (vf > 0.f ? 1.f : -1.f) * b.mass * kGrav * 0.2f * hump * wf * Saturate(fabsf(vf));
        b.addForce(x.fwd * fLong, vec3(0.f));
        // lateral (keel) resistance at the bow and stern quarters: carving + yaw damping + directional stability.
        // Applied at the COM height: planing hulls bank into turns rather than heeling outward.
        float zKeel = 0.f;
        for (int e = 0; e < 2; e++) {
            float yy = t.boxC.y + (e ? 0.3f : -0.38f) * L - t.com.y;
            vec3 rl(0.f, yy, zKeel);
            float vlat = vl.x - wl.z * yy;
            float kq = t.hullDragY * (e ? 0.45f : 0.55f), kl = 0.5f * b.mass * (e ? 0.45f : 0.55f);
            float f = -(kq * vlat * fabsf(vlat) + kl * vlat) * wf;
            float lim = fabsf(vlat) * b.mass * 0.5f / dt;
            b.addForce(x.right * Clamp(f, -lim, lim), b.R * rl);
        }
        // planing lift raises the hull; the bow climbs over the hump, then settles to a small running trim
        b.addForce(x.up * (b.mass * kGrav * 0.25f * planing * wf), vec3(0.f));
        b.torque += x.right * (b.mass * kGrav * L * (0.2f * hump + 0.02f * planing) * wf);
        // trim stability: a hull that noses up beyond its running trim meets the flow with its bottom, the lift moves
        // aft and pushes the bow back down (keeps light, powerful craft from back-flipping under full throttle)
        {
            float trim = asinf(Clamp(x.fwd.z, -1.f, 1.f));
            float over = trim - (0.05f + 0.12f * hump);
            if (over > 0.f) {
                float flow = Saturate(fabsf(vf) / 3.f);
                float pAcc = -(40.f * over + 6.f * Max(wl.x, 0.f)) * flow * Max(wf, 0.35f);
                b.torque += b.torqueFor(x.right * pAcc);
            }
        }
        // angular damping in water
        vec3 acc(-wl.x * 1.2f, -wl.y * 1.2f, -wl.z * 0.3f);
        b.torque += b.torqueFor(b.R * acc * wf);
        // bank into turns (hull shape / rider lean)
        float bankF = jetski ? 0.8f : (airboat ? 0.15f : 0.4f);
        float target = -bankF * atanf(vf * b.angVel.z / kGrav);
        target = Clamp(target, -0.6f, 0.6f);
        // (the jetski rider balances the narrow hull as long as it touches the water)
        float kb = jetski ? 60.f : 12.f;
        float rAcc = (kb * (target - s.lean) - 2.f * sqrtf(kb) * wl.y) * (jetski ? Max(wf, 0.6f) : wf);
        b.torque += b.torqueFor(x.fwd * rAcc);
    }
    // ---- propulsion ----
    float thr = c.hasDriver ? Saturate(c.throttle) : 0.f, brk = c.hasDriver ? Saturate(c.brake) : 0.f;
    float cmd = s.engineOn ? thr - brk * 0.6f : 0.f;
    s.throttleOut = approach(s.throttleOut, fabsf(cmd), 3.f * dt);
    s.rotorSpeed = approach(s.rotorSpeed, s.engineOn ? 0.2f + 0.8f * fabsf(cmd) : 0.f, 1.5f * dt);
    s.rotorAngle = fmodf(s.rotorAngle + s.rotorSpeed * 70.f * dt, kTwoPi);
    s.engineRpm = s.engineOn ? t.idleRpm + (t.maxRpm - t.idleRpm) * s.rotorSpeed : Max(0.f, s.engineRpm - 3000.f * dt);
    s.engineLoad = fabsf(cmd);
    vec3 rProp = b.R * (t.propPos - t.com);
    vec3 pProp = x.comW + rProp;
    bool propWet = airboat;
    if (!airboat) {
        float wz;
        propWet = Phys::waterSurface(pProp.x, pProp.y, wz) && wz > pProp.z - 0.08f;
    }
    float steer = Clamp(c.steer, -1.f, 1.f);
    s.steerOut = approach(s.steerOut, steer, 4.f * dt);
    steer = s.steerOut;
    float top = Max(m.topSpeed, 4.f);
    if (propWet && fabsf(cmd) > 0.001f) {
        float v = Max(vf, 0.f);
        float T = cmd > 0.f ? Min(t.thrustStatic, 0.5f * t.peakPowerW / Max(v, 0.1f)) * cmd : t.thrustStatic * 0.45f * cmd;
        T *= Saturate((top * 1.02f - vf) / (0.04f * top));
        // a hull's thrust line is set to pass near the center of mass (shaft/nozzle angle, drive trim): push at COM
        // height so full throttle squats the stern only through the hull's hump trim; the airboat fan pushes from
        // high above the deck and does pitch the bow down
        vec3 rThrust = airboat ? rProp : b.R * vec3(t.propPos.x - t.com.x, t.propPos.y - t.com.y, 0.f);
        b.addForce(x.fwd * T, rThrust);
    }
    // steering: rudder / jet nozzle / air rudders command a yaw rate the hull can carve
    // (max lateral acceleration per hull type, tighter radius at low speed; thrust gives authority at a standstill)
    {
        float aMax = (jetski ? 1.1f : (airboat ? 0.7f : 1.0f)) * kGrav;
        float rMin = (jetski ? 2.0f : (airboat ? 1.8f : 1.5f)) * L;
        float v = fabsf(vf);
        float rT = -steer * Min(Max(v, 1.5f) / rMin, aMax / Max(v, 1.f)) * (vf < -0.5f ? -1.f : 1.f);
        float wet = airboat ? 1.f : (s.inWater ? Max(Saturate(wetFrac * 1.5f), 0.7f) : 0.f);
        float authority = Saturate(Max(v / 3.f, (propWet ? fabsf(cmd) : 0.f) * 0.8f)) * wet;
        if (airboat) authority = Saturate(Max(v / 3.f, fabsf(cmd)));   // air rudders work in the fan wash, also on land
        float acc = 12.f * (rT - b.angVel.z) * authority;
        b.torque += b.torqueFor(vec3(0.f, 0.f, acc));   // about world vertical (hulls lean in turns)
    }
    if (airboat) b.force -= b.vel * (0.5f * kRhoAir * t.dragArea * x.speed);
    // jetski rider falls off when flipped
    if (jetski && c.hasDriver && !s.riderOff && x.up.z < 0.2f) {
        s.ejectRider = true;
        s.riderOff = true;
    }
    if (jetski && c.hasDriver && !s.prevHasDriver) s.riderOff = false;
    s.lean = atan2f(-x.right.z, x.up.z);
    float lvl = World::gMap->waterAt(x.comW.x, x.comW.y);
    s.waterDepth = lvl > World::kNoWater + 1.f ? Max(0.f, lvl - (float)(b.pos.z - (b.R * t.com).z)) : 0.f;
}

}  // namespace vsim
}  // namespace Vehicles
