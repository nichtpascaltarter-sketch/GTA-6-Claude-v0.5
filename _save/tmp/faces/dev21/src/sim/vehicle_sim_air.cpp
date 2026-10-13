// Fixed-wing aircraft and helicopters. Included by vehicle_sim.cpp.
namespace Vehicles {
namespace vsim {

// Pilot input shaping for keyboard and pad: keys are digital, so the stick position ramps toward the command and
// back to center faster (linear response, so AI pilots can command attitudes directly).
inline float pilotInput(float& state, float in, float rateIn, float rateOut, float dt) {
    in = Clamp(in, -1.f, 1.f);
    float rate = fabsf(in) > fabsf(state) && in * state >= 0.f ? rateIn : rateOut;
    state += Clamp(in - state, -rate * dt, rate * dt);
    return state;
}

// Height above the ground/water below the COM, refreshed at 30 Hz (cheap), plus the surface normal.
void updateAgl(StepCtx& x) {
    VehicleState& s = *x.s;
    s.aglTimer -= x.dt;
    if (s.aglTimer > 0.f) return;
    s.aglTimer = 1.f / 30.f;
    Phys::GroundHit g = Phys::gCollision->ground(x.comW.x, x.comW.y, x.comW.z, 0.f);
    float top = g.water ? Max(g.z, g.waterZ) : g.z;
    float originZ = x.comW.z - (x.b.R * s.tune.com).z;
    s.agl = Max(originZ - top, 0.f);
}

void planeForces(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleModel& m = *x.m;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    Body& b = x.b;
    float dt = x.dt;
    bool canRun = !s.engineFlooded && s.engineHealth > 0.f && !s.wrecked;
    if (c.engineOff || !canRun) s.engineOn = false;
    else if (!s.engineOn && c.hasDriver && c.throttle > 0.05f) s.engineOn = true;
    float thrCmd = s.engineOn && c.hasDriver ? Saturate(c.throttle) : 0.f;
    s.throttleOut = approach(s.throttleOut, thrCmd, 0.7f * dt);
    s.rotorSpeed = approach(s.rotorSpeed, s.engineOn ? 0.3f + 0.7f * s.throttleOut : 0.f, 0.8f * dt);
    s.rotorAngle = fmodf(s.rotorAngle + s.rotorSpeed * 110.f * dt, kTwoPi);
    s.engineRpm = s.engineOn ? t.maxRpm * (0.25f + 0.75f * s.rotorSpeed) : Max(0.f, s.engineRpm - 1500.f * dt);
    s.engineLoad = s.throttleOut;
    updateAgl(x);
    // landing gear: retract above 40 m AGL, extend below 30 m (hysteresis)
    if (s.agl > 40.f) s.gearDown = approach(s.gearDown, 0.f, dt / 3.f);
    else if (s.agl < 30.f) s.gearDown = approach(s.gearDown, 1.f, dt / 3.f);
    vec3 vl = x.vLocal;
    float V = x.speed;
    s.airspeed = V;
    float S = t.wingArea;
    float top = Max(m.topSpeed, 30.f);
    vec3 wl = b.local(b.angVel);
    float q = 0.5f * kRhoAir * V * V;
    float alpha = 0.f, beta = 0.f;
    const float alpha0 = 0.05f;   // wing incidence
    const float aStall = 0.26f;   // ~15 degrees
    if (V > 1.f) {
        alpha = atan2f(-vl.z, Max(vl.y, 0.1f));
        beta = asinf(Clamp(vl.x / V, -1.f, 1.f));
        float a = alpha + alpha0;
        float stallB = SmoothStep(aStall, aStall + 0.12f, fabsf(a));
        float CL = Lerp(t.liftSlope * a, 1.05f * sinf(2.f * a), stallB);
        s.stall = stallB;
        // ground effect within half a wingspan
        float span = sqrtf(S * 7.5f);
        float ge = 1.f + 0.25f * Sq(Saturate(1.f - s.agl / (0.5f * span)));
        vec3 liftDir = cross(x.right, b.vel);
        float ll = length(liftDir);
        liftDir = ll > 1e-4f ? liftDir / ll : x.up;
        float L = q * S * CL * ge;
        float airBrake = s.agl > 3.f ? Saturate(c.brake) : 0.f;
        float Dp = q * (t.dragArea + s.gearDown * 0.012f * S + airBrake * 0.06f * S);
        float Di = q * S * (t.inducedK * CL * CL / ge + stallB * 0.3f);
        vec3 dragDir = -b.vel / V;
        float Y = -q * 0.25f * S * sinf(beta);
        b.addForce(liftDir * L + dragDir * (Dp + Di) + x.right * Y, vec3(0.f));
    } else {
        s.stall = 0.f;
    }
    // propeller / jet thrust
    bool jet = top > 115.f;
    float vf = Max(vl.y, 1.f);
    // propeller: static thrust falling off with airspeed, capped by shaft power; jets: roughly constant thrust
    float T = s.throttleOut * (jet ? t.thrustStatic : Min(t.thrustStatic * (1.f - 0.3f * Min(vf / top, 1.2f)), 0.8f * t.peakPowerW / vf));
    b.addForce(x.fwd * T, vec3(0.f));
    // control surfaces as angular accelerations: authority grows with dynamic pressure
    float qRef = 0.5f * kRhoAir * Sq(0.6f * top);
    float qf = Clamp(q / qRef, 0.f, 1.6f), qs = sqrtf(qf);
    bool pilot = c.hasDriver;
    float pitchIn = pilotInput(s.ctlPitch, pilot ? c.pitch : 0.f, 2.5f, 4.f, x.dt);
    float rollIn = pilotInput(s.ctlRoll, pilot ? c.roll : 0.f, 2.5f, 4.f, x.dt);
    float yawIn = pilotInput(s.ctlYaw, pilot ? c.yaw : 0.f, 2.f, 4.f, x.dt);
    // elevator commands an angle of attack, limited to 6 g
    float aCmd = pitchIn > 0.f ? pitchIn * 0.3f : pitchIn * 0.14f;
    float aLim = 6.f * b.mass * kGrav / (Max(q, 1.f) * S * t.liftSlope) - alpha0;
    aCmd = Min(aCmd, aLim);
    float pAcc = qf * 8.f * (aCmd - alpha) - 3.f * qs * wl.x - s.stall * qf * 5.f;
    float rAcc = qf * 6.f * rollIn - 3.f * qs * wl.y - qf * 1.5f * beta;
    if (s.stall > 0.5f) rAcc += s.stall * qf * 3.f * (wl.z > 0.f ? -1.f : 1.f);  // wing drop
    // weathervane + rudder + automatic coordination (turns work without rudder)
    float coord = -kGrav * sinf(s.lean) / Max(V, 12.f);
    float yAcc = -qf * 4.f * beta - qf * 2.5f * yawIn - 2.f * qs * wl.z + qf * 1.5f * (coord - wl.z);
    // on the ground: no aero moments below rotation speed except damping
    b.torque += b.torqueFor(b.R * vec3(pAcc, rAcc, yAcc));
}

void rotorStrikeCheck(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    const VehicleModel& m = *x.m;
    Body& b = x.b;
    if (s.rotorSpeed < 0.2f) return;
    vec3 hubL = length(m.rotorPos) > 0.01f ? m.rotorPos : vec3(0.f, t.boxC.y, t.boxC.z + t.boxH.z);
    vec3 rHub = b.R * (hubL - t.com);
    vec3 hub = x.comW + rHub;
    bool lowTilt = x.up.z > 0.97f && s.agl > t.rotorR * 0.5f + 1.f;
    thread_local std::vector<int> ids;
    Phys::gCollision->collidersNear(hub.xy(), t.rotorR + 0.5f, ids);
    if (ids.empty() && lowTilt) return;
    int hits = 0;
    vec3 push(0.f);
    for (int k = 0; k < 8; k++) {
        float a = k * (kTwoPi / 8.f);
        vec3 p = hub + b.R * vec3(cosf(a) * t.rotorR, sinf(a) * t.rotorR, 0.f);
        bool hit = false;
        for (int id : ids) {
            const Phys::Collider& c = Phys::gCollision->collider(id);
            if (c.owner < 0) continue;
            if (c.kind == Phys::COL_BOX) {
                vec3 d = p - c.c;
                vec3 ax(c.ax.x, c.ax.y, 0.f), ay(-c.ax.y, c.ax.x, 0.f);
                if (fabsf(dot(d, ax)) < c.he.x && fabsf(dot(d, ay)) < c.he.y && fabsf(d.z) < c.he.z) hit = true;
            } else {
                float dx = p.x - c.c.x, dy = p.y - c.c.y;
                if (dx * dx + dy * dy < c.he.x * c.he.x && p.z > c.c.z && p.z < c.c.z + c.he.z) hit = true;
            }
            if (hit) break;
        }
        if (!hit && !lowTilt) {
            Phys::GroundHit g = Phys::gCollision->ground(p.x, p.y, p.z + 0.5f, 0.f);
            if (g.z > p.z) hit = true;
        }
        if (hit) {
            hits++;
            push += hub - p;
        }
    }
    if (!hits) return;
    s.rotorStrike = true;
    s.rotorSpeed *= 0.985f;
    zoneDamage(s, 25.f * hits * x.dt * 30.f, 4);
    vec3 n = normalize(vec3(push.x, push.y, 0.f));
    b.impulse(n * (b.mass * 0.4f), rHub);
    b.angVel += x.up * (2.f * x.dt * hits);
}

void heliForces(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    Body& b = x.b;
    float dt = x.dt;
    bool canRun = !s.engineFlooded && s.engineHealth > 0.f && !s.wrecked;
    if (c.engineOff || !canRun) s.engineOn = false;
    else if (!s.engineOn && c.hasDriver && (c.lift > 0.05f || c.throttle > 0.05f)) s.engineOn = true;
    float spool = s.engineOn ? 1.f : 0.f;
    s.rotorSpeed = approach(s.rotorSpeed, spool, (spool > s.rotorSpeed ? 0.22f : 0.07f) * dt);
    s.rotorAngle = fmodf(s.rotorAngle + s.rotorSpeed * 42.f * dt, kTwoPi);
    s.tailRotorAngle = fmodf(s.tailRotorAngle + s.rotorSpeed * 140.f * dt, kTwoPi);
    s.engineRpm = s.engineOn ? t.maxRpm * (0.3f + 0.7f * s.rotorSpeed) : Max(0.f, s.engineRpm - 800.f * dt);
    updateAgl(x);
    float rs2 = s.rotorSpeed * s.rotorSpeed;
    bool grounded = s.airborneTime <= 0.f && !s.inWater;
    bool pilot = c.hasDriver;
    float lift = pilot ? Clamp(c.lift, -1.f, 1.f) : -1.f;
    // ground effect within one rotor diameter
    float ge = 1.f + 0.18f * Sq(Saturate(1.f - s.agl / (2.f * t.rotorR)));
    float Tmax = 1.4f * b.mass * kGrav * rs2 * ge;
    float T;
    if (grounded && lift <= 0.05f) T = 0.35f * b.mass * kGrav * rs2;
    else {
        float vzT = lift > 0.f ? lift * 8.f : lift * 6.f;
        float az = kGrav + 2.5f * (vzT - b.vel.z);
        T = b.mass * az / Max(x.up.z, 0.5f);
    }
    T = Clamp(T, 0.f, Tmax);
    b.addForce(x.up * T, vec3(0.f));
    s.throttleOut = Saturate(T / Max(b.mass * kGrav * 1.4f, 1.f));
    s.engineLoad = s.throttleOut;
    // cyclic: attitude targets (max 30 deg); with no input the stabilizer tilts against drift (hover hold)
    vec3 fh = vec3(x.fwd.x, x.fwd.y, 0.f);
    float fl = length(fh);
    fh = fl > 1e-3f ? fh / fl : vec3(0, 1, 0);
    vec3 rh(fh.y, -fh.x, 0.f);
    float vF = dot(b.vel, fh), vR = dot(b.vel, rh);
    const float maxTilt = 0.52f;
    float pitchIn = pilotInput(s.ctlPitch, pilot ? c.pitch : 0.f, 3.f, 5.f, x.dt);
    float rollIn = pilotInput(s.ctlRoll, pilot ? c.roll : 0.f, 3.f, 5.f, x.dt);
    float tp = fabsf(pitchIn) > 0.04f ? pitchIn * maxTilt : Clamp(atanf(0.9f * vF / kGrav), -0.35f, 0.35f);
    float tr = fabsf(rollIn) > 0.04f ? rollIn * maxTilt : Clamp(-atanf(0.9f * vR / kGrav), -0.35f, 0.35f);
    if (!pilot) tp = tr = 0.f;
    float pitchAng = asinf(Clamp(x.fwd.z, -1.f, 1.f));
    vec3 wl = b.local(b.angVel);
    float pAcc = 12.f * (tp - pitchAng) - 5.f * wl.x;
    float rAcc = 12.f * (tr - s.lean) - 5.f * wl.y;
    float yawIn = pilotInput(s.ctlYaw, pilot ? c.yaw : 0.f, 3.f, 5.f, x.dt);
    float yAcc = 4.f * (-yawIn * 1.5f - wl.z);
    float ctrl = rs2 * (grounded && lift <= 0.05f ? 0.15f : 1.f);
    b.torque += b.torqueFor(b.R * vec3(pAcc, rAcc, yAcc) * ctrl);
    // fuselage + disc drag (sized so that 30 degrees of tilt reaches the model's top speed)
    b.force -= b.vel * (0.5f * kRhoAir * t.heliDragArea * x.speed);
    b.force.z -= b.vel.z * fabsf(b.vel.z) * 0.5f * kRhoAir * t.heliDragArea * 2.f;
    rotorStrikeCheck(x);
}

}  // namespace vsim
}  // namespace Vehicles
