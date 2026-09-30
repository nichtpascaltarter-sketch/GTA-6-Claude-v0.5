// Raycast wheels, suspension, tires, engine + gearbox, car steering/assists, bike balance. Included by vehicle_sim.cpp.
namespace Vehicles {
namespace vsim {

// Normalized tire curve over the combined slip s (s = 1 at peak grip): quadratic rise, smooth fall to 78% when sliding.
constexpr float kSlideDrop = 0.22f;
inline void tireCurve(float s, float& f, float& df) {
    if (s <= 1.f) {
        f = s * (2.f - s);
        df = 2.f - 2.f * s;
    } else {
        float u = s - 1.f, d = 1.f + u * u;
        f = 1.f - kSlideDrop * u * u / d;
        df = -kSlideDrop * 2.f * u / (d * d);
    }
}

// Longitudinal/lateral tire force for a wheel speed; `mono` evaluates the monotone envelope (peak held) used by the
// implicit wheel-spin solve. dfx = d(fx)/d(omega) >= 0 for the envelope.
struct TireEval {
    float Fmax, rad, vx, Vx, sy, kp;
    void eval(float omega, bool mono, float& fx, float& fy, float& dfx, float& sAbs) const {
        float sx = (omega * rad - vx) / (Vx * kp);
        float s2 = sx * sx + sy * sy;
        sAbs = sqrtf(s2);
        float dsx = rad / (Vx * kp);
        if (sAbs < 1e-6f) {
            fx = fy = 0.f;
            dfx = 2.f * Fmax * dsx;
            return;
        }
        float f, df;
        tireCurve(sAbs, f, df);
        if (mono && sAbs > 1.f) {
            f = 1.f;
            df = 0.f;
        }
        float inv = 1.f / sAbs, cx = sx * inv;
        fx = Fmax * f * cx;
        fy = -Fmax * f * sy * inv;
        float fOverS = sAbs < 1e-3f ? 2.f - sAbs : f * inv;
        dfx = Max(Fmax * (df * cx * cx + fOverS * (1.f - cx * cx)), 0.f) * dsx;
    }
};

// Implicit wheel spin: solve I*(w - w0)/dt = Td - Tb*sgn(w) - r*fx(w) (Tb = friction torque capacity, fx monotone)
// with a bracketed Newton iteration. Returns the new spin rate; `locked` when the brakes hold the wheel.
float solveWheelSpin(const TireEval& te, float w0, float I, float dt, float Td, float Tb, bool& locked) {
    locked = false;
    float fx, fy, dfx, sAbs;
    auto H = [&](float w, float& dH) {
        te.eval(w, true, fx, fy, dfx, sAbs);
        dH = I / dt + te.rad * dfx;
        return I * (w - w0) / dt - Td + te.rad * fx;
    };
    float dH;
    float h0 = H(0.f, dH);
    if (fabsf(h0) <= Tb) {
        locked = Tb > 0.f;
        return 0.f;
    }
    float sgn = h0 + Tb < 0.f ? 1.f : -1.f;   // root on the positive side when G(0) < 0
    float reach = dt * (fabsf(Td) + te.rad * te.Fmax) / I + 1e-3f;
    float lo, hi;
    if (sgn > 0.f) {
        lo = 0.f;
        hi = Max(0.f, w0) + reach;
    } else {
        lo = Min(0.f, w0) - reach;
        hi = 0.f;
    }
    float w = Clamp(w0, lo, hi);
    for (int it = 0; it < 10; it++) {
        float g = H(w, dH) + sgn * Tb;
        if (g < 0.f) lo = w;
        else hi = w;
        float wn = w - g / Max(dH, 1e-6f);
        if (!(wn > lo && wn < hi)) wn = 0.5f * (lo + hi);
        if (fabsf(wn - w) < 1e-4f * (1.f + fabsf(w))) {
            w = wn;
            break;
        }
        w = wn;
    }
    return w;
}

// Engine torque (N*m) at rpm: rises to the peak torque, then follows the power limit, falls past the redline.
float engineTorque(const VehicleTuning& t, float rpm) {
    float x = rpm / t.maxRpm;
    float tq = t.peakTorque * (0.55f + 0.45f * SmoothStep(0.08f, 0.5f, x));
    float w = Max(rpm, 100.f) * (kTwoPi / 60.f);
    tq = Min(tq, t.peakPowerW / w);
    tq *= 1.f - 0.35f * SmoothStep(0.93f, 1.03f, x);
    return tq;
}

struct DriveCmd {
    float wheelTorque = 0.f;   // total drive torque at the driven wheels (+ forward)
    float brake = 0.f;         // service brake 0..1
    bool handbrake = false;
    float reflectedI = 0.f;    // engine inertia seen at the wheels when the clutch is locked
    bool engaged = false;
    bool burnout = false;      // throttle + brake at a standstill: the drive axle spins, the other axle holds
};

// Engine, clutch, automatic gearbox, GTA-style reverse.
DriveCmd powertrain(StepCtx& x, float drivenOmega) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    float dt = x.dt;
    DriveCmd d;
    bool canRun = !s.engineFlooded && s.engineHealth > 0.f && !s.wrecked;
    if (c.engineOff || !canRun) s.engineOn = false;
    else if (!s.engineOn && c.hasDriver && c.throttle > 0.05f) s.engineOn = true;
    float thr = c.hasDriver ? Saturate(c.throttle) : 0.f, brk = Saturate(c.brake);
    float vF = x.vFwd;
    float driveIn, brakeIn;
    if (s.gear >= 0) {
        if (s.gear == 0) s.gear = 1;
        driveIn = thr;
        brakeIn = brk;
        if (brk > 0.1f && thr < 0.05f && vF < 0.8f && !c.handbrake && c.hasDriver && s.engineOn) {
            s.reverseTimer += dt;
            if (s.reverseTimer > 0.15f) {
                s.gear = -1;
                s.shiftTimer = 0.f;
                s.reverseTimer = 0.f;
                s.shifted = true;
            }
        } else {
            s.reverseTimer = 0.f;
        }
    } else {
        driveIn = c.handbrake || !c.hasDriver ? 0.f : brk;
        brakeIn = c.handbrake || !c.hasDriver ? brk : thr;
        if ((thr > 0.1f && brk < 0.05f && vF > -0.8f) || ((c.handbrake || !c.hasDriver) && fabsf(vF) < 0.8f)) {
            s.reverseTimer += dt;
            if (s.reverseTimer > 0.15f) {
                s.gear = 1;
                s.shiftTimer = 0.f;
                s.reverseTimer = 0.f;
                s.shifted = true;
            }
        } else {
            s.reverseTimer = 0.f;
        }
    }
    // automatic gearbox (decisions from road speed so wheelspin does not trigger upshifts)
    const float toRpm = 60.f / kTwoPi;
    if (s.shiftTimer > 0.f) {
        s.shiftTimer -= dt;
        if (s.shiftTimer <= 0.f) {
            s.gear = s.pendingGear;
            s.shifted = true;
        }
    } else if (s.gear >= 1 && t.gears > 1 && s.engineOn) {
        float rpmG = fabsf(vF) / t.driveRadius * t.ratio[s.gear] * toRpm;
        float upRpm = t.maxRpm * Lerp(0.5f, 0.93f, driveIn);
        if (s.gear < t.gears && rpmG > upRpm && (driveIn > 0.05f || rpmG > t.maxRpm * 0.9f)) {
            s.pendingGear = s.gear + 1;
            s.shiftTimer = t.shiftTime;
        } else if (s.gear > 1) {
            float downRpm = t.maxRpm * Lerp(0.25f, 0.55f, driveIn);
            float rpmLower = rpmG * t.ratio[s.gear - 1] / t.ratio[s.gear];
            if (rpmG < downRpm && rpmLower < t.maxRpm * 0.85f) {
                s.pendingGear = s.gear - 1;
                s.shiftTimer = t.shiftTime * 0.5f;
            }
        }
    }
    if (s.gear > 1 && fabsf(vF) < 1.5f) {
        s.gear = 1;
        s.shiftTimer = 0.f;
    }
    float ratio = s.gear > 0 ? t.ratio[s.gear] : (s.gear < 0 ? -t.ratio[0] : 0.f);
    bool engaged = s.shiftTimer <= 0.f && ratio != 0.f && s.engineOn;
    float wheelRpm = fabsf(drivenOmega * ratio) * toRpm;
    float launchRpm = t.idleRpm + driveIn * (0.45f * t.maxRpm - t.idleRpm);
    bool locked = engaged && wheelRpm >= launchRpm;
    if (!s.engineOn) s.engineRpm = Max(0.f, s.engineRpm - 2500.f * dt);
    else if (locked) s.engineRpm = wheelRpm;
    else {
        float target = engaged ? Max(wheelRpm, launchRpm) : t.idleRpm + driveIn * 0.8f * (t.maxRpm - t.idleRpm);
        s.engineRpm = approach(s.engineRpm, target, 9000.f * dt);
    }
    if (s.engineOn) s.engineRpm = Clamp(s.engineRpm, t.idleRpm * 0.9f, t.maxRpm * 1.02f);
    float thrEff = s.engineOn && engaged ? driveIn : 0.f;
    if (s.engineRpm >= t.maxRpm * 0.995f) thrEff = 0.f;  // rev limiter
    float top = s.gear < 0 ? Min(s.model->topSpeed * 0.35f, 14.f) : t.topSpeed;
    thrEff *= Saturate((top * 1.005f - fabsf(vF)) / (0.025f * top));  // governor
    s.throttleOut = approach(s.throttleOut, s.engineOn ? (engaged ? thrEff : driveIn * 0.6f) : 0.f, 8.f * dt);
    // turbo: boost builds with throttle above ~35 % rpm (spool lag ~0.7 s) and dumps quickly off throttle
    float boostT = t.turbo && s.engineOn ? Saturate(thrEff * 1.25f) * SmoothStep(0.3f, 0.55f, s.engineRpm / t.maxRpm) : 0.f;
    s.turboBoost = approach(s.turboBoost, boostT, (boostT > s.turboBoost ? 1.4f : 3.5f) * dt);
    // a battered engine (below 40 % health) loses up to 55 % of its power
    float enginePower = Clamp(0.45f + 0.55f * s.engineHealth / 400.f, 0.45f, 1.f);
    float Te = engineTorque(t, s.engineRpm) * thrEff * (1.f + 0.14f * s.turboBoost) * enginePower;
    if (thrEff < 0.02f && locked && wheelRpm > t.idleRpm * 1.1f) Te = -t.peakTorque * (0.06f + 0.1f * wheelRpm / t.maxRpm);
    s.engineLoad = s.engineOn ? Saturate(fabsf(Te) / t.peakTorque) : 0.f;
    d.wheelTorque = Te * ratio * 0.9f;
    d.brake = brakeIn;
    d.handbrake = c.handbrake;
    d.engaged = locked;
    // GTA-style burnout: accelerator and brake together at (near) a standstill in a forward gear
    d.burnout = s.gear >= 1 && thr > 0.5f && brk > 0.5f && fabsf(vF) < 3.f && !c.handbrake && c.hasDriver && s.engineOn && !isBikeClass(s.cls);
    d.reflectedI = locked && (d.burnout || (brakeIn < 0.1f && !c.handbrake)) ? t.engineI * ratio * ratio : 0.f;
    return d;
}

// Car steering: speed-sensitive lock, rate limit, Ackermann, countersteer assist.
float carSteer(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    Body& b = x.b;
    float v = fabsf(x.vFwd);
    // speed-sensitive lock: full lock in town, on the highway only what the front tires can use (a keyboard tap at
    // 100 km/h must not snap the car into a four-wheel slide)
    float hs = SmoothStep(8.f, 25.f, v);
    float aLat = Lerp(1.25f, 1.05f, hs) * Clamp(s.model->grip, 0.5f, 1.6f) * kGrav;
    float geo = atanf(t.wheelbase * aLat / Max(v * v, 1e-3f));
    float lim = Min(t.maxSteer, geo + t.alphaPeak * Lerp(0.5f, 0.25f, hs));
    float in = Clamp(c.steer, -1.f, 1.f);
    float target = in * lim;
    vec3 wl = b.local(b.angVel);
    if (v > 4.f && x.vFwd > 0.f && !c.handbrake) {
        float vxF = x.vLocal.x - wl.z * (t.frontY - t.com.y);
        float vxR = x.vLocal.x - wl.z * (t.rearY - t.com.y);
        float betaF = atan2f(vxF, x.vLocal.y), betaR = atan2f(vxR, x.vLocal.y);
        float w = SmoothStep(0.07f, 0.3f, fabsf(betaR));
        // only assist toward the slide (never against a player who already countersteers harder)
        float assist = Clamp(betaF, -t.maxSteer, t.maxSteer);
        target = Lerp(target, assist + in * lim * 0.35f, w * 0.65f);
        target = Clamp(target, -t.maxSteer, t.maxSteer);
    }
    float cur = s.steerOut * t.maxSteer;
    // steering rate: quick in town, calmer at speed; the wheel self-centers faster than it turns in
    float rate = Max(lim * Lerp(6.f, 3.5f, hs), 0.5f);
    if (fabsf(target) < fabsf(cur) || target * cur < 0.f) rate *= 1.6f;
    cur = approach(cur, target, rate * x.dt);
    s.steerOut = cur / t.maxSteer;
    // bent steering from crash damage pulls the car to one side once it rolls
    return cur + s.alignPull * SmoothStep(2.f, 10.f, v);
}

// Bike: lean-driven steering (countersteer implicit), balance torque with gravity/centripetal feed-forward,
// wheelies/stoppies, riderless fall onto the kickstand, crash detection.
// Steady-state lean limit the rider will use: the tires cannot hold more than atan(mu) (scooters lean less).
float bikeMaxLean(const VehicleState& s) {
    return Min(s.cls == VC_SCOOTER ? 0.68f : 0.8f, atanf(0.85f * Clamp(s.model->grip, 0.5f, 1.4f)));
}

float bikeSteer(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    bool rider = c.hasDriver && !s.riderOff;
    float v = fabsf(x.vFwd);
    float in = Clamp(c.steer, -1.f, 1.f);
    float delta;
    if (rider) {
        float spd = SmoothStep(1.5f, 8.f, v);
        // never ask the front tire for more turn than the grip-limited lean sustains (a lean overshoot must not tighten the line)
        float lmax = bikeMaxLean(s) + 0.05f;
        float lean = Clamp(s.lean, -lmax, lmax);
        float kin = atanf(t.wheelbase * kGrav * tanf(lean) / Max(v * v, 1.f));
        // yaw-rate feedback: the turn rate the lean can sustain is g*tan(lean)/v (+ = right = clockwise)
        float rTarget = kGrav * tanf(lean) / Max(v, 1.f);
        float rNow = -x.b.angVel.z;
        kin += 0.12f * (rTarget - rNow);
        float direct = in * t.maxSteer;
        delta = x.vFwd < -0.5f ? direct : Lerp(direct, kin, spd);
    } else {
        delta = s.steerOut * t.maxSteer * 0.95f;
    }
    delta = Clamp(delta, -t.maxSteer, t.maxSteer);
    s.steerOut = delta / t.maxSteer;
    return delta;
}

void bikeAssist(StepCtx& x, bool frontContact, bool rearContact, float driveIn, float brakeIn) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    Body& b = x.b;
    // rider mounted again after an ejection
    if (c.hasDriver && !s.prevHasDriver) s.riderOff = false;
    if (s.ejectTimer > 0.f) s.ejectTimer -= x.dt;
    bool rider = c.hasDriver && !s.riderOff;
    float v = fabsf(x.vFwd);
    vec3 wl = b.local(b.angVel);
    float lean = s.lean, leanRate = wl.y;
    float pitchAng = asinf(Clamp(x.fwd.z, -1.f, 1.f)), pitchRate = wl.x;
    bool grounded = frontContact || rearContact;
    if (rider) {
        float maxLean = bikeMaxLean(s);
        float spd = SmoothStep(1.5f, 8.f, v);
        float in = Clamp(c.steer, -1.f, 1.f);
        float targetLean = x.vFwd > -0.5f ? in * maxLean * spd : 0.f;
        // the rider leans in progressively (~0.35 s to full lean)
        targetLean = Clamp(targetLean, s.leanCmd - 2.5f * x.dt, s.leanCmd + 2.5f * x.dt);
        s.leanCmd = targetLean;
        if (grounded) {
            // the rider balances the bike: cancel the roll torque the tires produce this step (ground reaction vs
            // cornering force, including when the tires slide), then PD to the target lean
            float wn = 9.f, zeta = 0.9f;
            float acc = wn * wn * (targetLean - lean) - 2.f * zeta * wn * leanRate;
            // picking up a fallen bike / feet down at a standstill
            if (v < 1.f && fabsf(lean) > 0.5f) acc = Clamp(acc, -3.f, 3.f);
            b.torque += x.fwd * (-x.tireRollTorque) + b.torqueFor(x.fwd * acc);
            // yaw stability: pull the yaw rate toward what the lean sustains (arcade anti-lowside)
            if (v > 3.f && x.vFwd > 0.f) {
                // only suppress over-rotation (yaw beyond what the lean sustains); never add yaw
                float rTarget = -kGrav * tanf(Clamp(lean, -1.1f, 1.1f)) / v;  // world z (+ = left)
                float rMaxGrip = 1.1f * Clamp(s.model->grip, 0.5f, 1.4f) * kGrav / v;
                rTarget = Clamp(rTarget, -rMaxGrip, rMaxGrip);
                float err = b.angVel.z - rTarget;
                bool over = fabsf(b.angVel.z) > fabsf(rTarget) && err * b.angVel.z > 0.f;
                if (over) b.torque += b.torqueFor(vec3(0.f, 0.f, -4.f * err));
            }
        } else {
            // airborne: keep the bike upright-ish, rider pitch control
            b.torque += b.torqueFor(x.fwd * (-4.f * lean - 1.5f * leanRate));
        }
        // pitch: wheelies / stoppies / air control
        float pAcc = 0.f;
        bool pitchCtl = false;
        if (rearContact && !frontContact && pitchAng > 0.03f) {
            pitchCtl = true;
            if (c.pitch > 0.1f && driveIn > 0.1f) pAcc = 25.f * (Lerp(0.25f, 0.6f, c.pitch) - pitchAng) - 9.f * pitchRate;
            else pAcc = -2.5f - 2.f * Max(pitchRate, 0.f);
            if (pitchAng > 0.75f) pAcc = Min(pAcc, -25.f);
        } else if (frontContact && rearContact && c.pitch > 0.3f && driveIn > 0.5f && v < 28.f && v > 2.f) {
            pitchCtl = true;
            pAcc = 9.f * c.pitch;
        } else if (frontContact && !rearContact && pitchAng < -0.03f) {
            pitchCtl = true;
            if (c.pitch < -0.1f && brakeIn > 0.3f) pAcc = 25.f * (c.pitch * 0.4f - pitchAng) - 9.f * pitchRate;
            else pAcc = 3.f + 2.f * Max(-pitchRate, 0.f);
            if (pitchAng < -0.6f) pAcc = Max(pAcc, 25.f);
        } else if (frontContact && rearContact && c.pitch < -0.3f && brakeIn > 0.5f && v > 5.f) {
            pitchCtl = true;
            pAcc = 7.f * c.pitch;
        } else if (!grounded) {
            pitchCtl = true;
            pAcc = 4.f * c.pitch - 1.5f * pitchRate;
        }
        if (pitchCtl) b.torque += b.torqueFor(x.right * pAcc);
        // crash: lying down, looped out, upside down
        bool crash = (fabsf(lean) > 1.2f && v > 3.f) || x.up.z < 0.25f || pitchAng > 1.05f || pitchAng < -0.95f;
        if (crash && s.ejectTimer <= 0.f) {
            s.ejectRider = true;
            s.riderOff = true;
            s.ejectTimer = 1.f;
        }
    } else if (!rider) {
        s.leanCmd = lean;
    }
    if (!rider && grounded && v < 2.f && fabsf(lean) < 0.5f && x.up.z > 0.8f) {
        // riderless and nearly stopped: settle onto the kickstand (left, ~12 degrees)
        float aRight = -x.vFwd * b.angVel.z;
        float natural = b.mass * t.com.z * (kGrav * sinf(lean) - aRight * cosf(lean));
        const float standLean = -0.21f;
        float wn = 5.f;
        float acc = wn * wn * (standLean - lean) - 2.f * wn * leanRate;
        b.torque += x.fwd * (-natural) + b.torqueFor(x.fwd * acc);
    }
}

// Suspension, tires, drive and brakes for every wheeled vehicle (cars, bikes, aircraft gear).
void wheelForces(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleModel& m = *x.m;
    const VehicleTuning& t = s.tune;
    const VehicleControls& c = *x.c;
    Body& b = x.b;
    float dt = x.dt;
    bool bike = isBikeClass(s.cls), plane = s.cls == VC_PLANE, heli = s.cls == VC_HELI, boat = isBoatClass(s.cls);
    bool road = !bike && isRoadClass(s.cls);
    s.hbTimer = c.handbrake ? 0.f : s.hbTimer + dt;
    if (s.wheelCount == 0) {
        x.wheelsInContact = 0;
        return;
    }
    bool gearRetracted = plane && s.gearDown < 0.9f;
    // ---- steering ----
    float steer = 0.f;
    if (bike) steer = bikeSteer(x);
    else if (plane) {
        float v = fabsf(x.vFwd);
        float lim = Lerp(t.maxSteer, 0.06f, SmoothStep(3.f, 30.f, v));
        float cur = s.steerOut * t.maxSteer;
        cur = approach(cur, Clamp(c.steer + c.yaw, -1.f, 1.f) * lim, 2.5f * dt);
        s.steerOut = cur / t.maxSteer;
        steer = cur;
    } else if (road) steer = carSteer(x);
    // ---- drive ----
    float drivenOmega = 0.f, driveW = 0.f;
    for (int i = 0; i < s.wheelCount; i++)
        if (t.driveShare[i] > 0.f) {
            drivenOmega += s.wheels[i].spinVel * t.driveShare[i];
            driveW += t.driveShare[i];
        }
    if (driveW > 0.f) drivenOmega /= driveW;
    DriveCmd dc;
    if (road || bike) {
        dc = powertrain(x, drivenOmega);
        if (bike && s.wheelCount >= 2) {
            // power-wheelie limiter: the rider feathers the throttle past the balance point
            bool frontUp = true, rearDown = false;
            for (int i = 0; i < s.wheelCount; i++) {
                if (m.wheels[i].pos.y > t.com.y && s.wheels[i].contact) frontUp = false;
                if (m.wheels[i].pos.y <= t.com.y && s.wheels[i].contact) rearDown = true;
            }
            float pitchAng = asinf(Clamp(x.fwd.z, -1.f, 1.f));
            float target = c.pitch > 0.1f ? Lerp(0.25f, 0.6f, c.pitch) : 0.1f;
            if (frontUp && rearDown && pitchAng > target) dc.wheelTorque *= Saturate(1.f - (pitchAng - target) * 6.f);
        }
    } else {
        dc.brake = heli ? 1.f : Saturate(c.brake);
        if (!c.hasDriver) dc.brake = 1.f;
    }
    // ---- suspension ----
    struct WW {
        bool contact;
        vec3 r, n;
        float comp, load;
        u8 surface;
    } ww[kMaxWheels];
    int nContact = 0;
    float totalLoad = 0.f;
    vec3 accP(0.f), accN(0.f);
    vec3 up = x.up;
    for (int i = 0; i < s.wheelCount; i++) {
        const WheelSpec& ws = m.wheels[i];
        WheelState& w = s.wheels[i];
        WW& o = ww[i];
        o.contact = false;
        o.load = 0.f;
        float travel = wheelTravel(s, i);
        float rad = ws.radius * (w.burst ? 0.8f : 1.f);
        // steer angle (Ackermann on steered wheels, half-rate opposite steer for steered rear axles)
        float d = 0.f;
        if (ws.steer && steer != 0.f) {
            float ts = tanf(steer);
            float sx = ws.pos.x * (bike ? 0.f : 0.6f);
            d = atanf(ts / Max(1.f - sx * ts / t.wheelbase, 0.2f));
            if (ws.pos.y < t.com.y) d = -d * 0.5f;
        }
        w.steerAngle = d;
        if (gearRetracted) {
            w.contact = false;
            w.compression = approach(w.compression, 0.f, dt);
            continue;
        }
        vec3 rMount = b.R * (ws.pos + vec3(0.f, 0.f, travel - t.restComp[i] + t.rideDrop) - t.com);
        vec3 mount = x.comW + rMount;
        vec3 hubPrev = mount - up * (travel - w.compression);
        float fallSpec = Max(0.f, -b.velAt(rMount).z) * dt;
        float zRef = Max(mount.z, hubPrev.z + 0.6f * rad) + fallSpec + 0.05f;
        Phys::GroundHit g = Phys::gCollision->ground(hubPrev.x, hubPrev.y, zRef, 0.f);
        vec3 P(hubPrev.x, hubPrev.y, g.z), n = g.normal;
        u8 surf = g.surface;
        bool water = g.water && g.waterZ - g.z > 0.3f;
        // leading-edge sample: the tire meets curbs before its center does
        vec3 vHub = b.velAt(hubPrev - x.comW);
        float vh = sqrtf(vHub.x * vHub.x + vHub.y * vHub.y);
        if (vh > 0.5f && !plane) {
            float lx = vHub.x / vh * 0.75f * rad, ly = vHub.y / vh * 0.75f * rad;
            Phys::GroundHit g2 = Phys::gCollision->ground(hubPrev.x + lx, hubPrev.y + ly, zRef, 0.f);
            float h2 = g2.z - 0.339f * rad;
            if (h2 > P.z + 0.005f) {
                P.z = h2;
                n = g2.normal;
                surf = g2.surface;
            }
        }
        float upN = dot(up, n);
        if (upN < 0.25f || g.z < -1e8f) {
            w.contact = false;
            w.compression = approach(w.compression, 0.f, travel * 8.f * dt);
            w.compressionVel = 0.f;
            continue;
        }
        // tire profile: cars/aircraft roll on a flat tread (contact below the hub along n); bike tires are a thin
        // disk with a round crown (radius 0.3 r), so a leaned bike's contact stays near the wheel plane
        float radEff = rad, crown = rad;
        vec3 dPlane = -n;
        if (bike) {
            vec3 axle = x.right;
            vec3 d = -n + axle * dot(n, axle);
            float dl = length(d);
            dPlane = dl > 1e-3f ? d / dl : -n;
            crown = 0.3f * rad;
            radEff = (rad - crown) * dl + crown;
        }
        float dist = dot(mount - P, n);
        float comp = travel - (dist - radEff) / upN;
        if (comp <= 0.f) {
            w.contact = false;
            w.compressionVel = 0.f;
            w.compression = approach(w.compression, 0.f, travel * 8.f * dt);
            continue;
        }
        float compC = Min(comp, travel);
        vec3 hub = mount - up * (travel - compC);
        vec3 cp = bike ? hub + dPlane * (rad - crown) - n * crown : hub - n * rad;
        vec3 r = cp - x.comW;
        float compVel = -dot(b.velAt(r), n) / upN;
        float fs = t.staticLoad[i] + t.springK[i] * (compC - t.restComp[i]);
        float fd = compVel > 0.f ? t.dampBump[i] * compVel : t.dampRebound[i] * compVel;
        o.contact = true;
        o.r = r;
        o.n = n;
        o.comp = compC;
        o.load = fs + fd;
        o.surface = water ? (u8)Phys::SURF_WATER : surf;
        w.compressionVel = compVel;
        w.compression = compC;
        if (comp > travel) x.cs.add(r, n, (comp - travel) * upN, 0.f, 0.f, -1, CK_BUMP, surf);
        nContact++;
        accP += cp;
        accN += n;
    }
    // anti-roll bars
    for (int i = 0; i < s.wheelCount; i++) {
        int j = t.arbPair[i];
        if (j <= i || !ww[i].contact || !ww[j].contact) continue;
        float f = t.arbK[i] * (ww[i].comp - ww[j].comp);
        ww[i].load += f;
        ww[j].load -= f;
    }
    for (int i = 0; i < s.wheelCount; i++)
        if (ww[i].contact) {
            ww[i].load = Max(ww[i].load, 0.f);
            totalLoad += ww[i].load;
        }
    x.wheelsInContact = nContact;
    if (nContact > 0) {
        x.groundP = accP / (float)nContact;
        x.groundN = normalize(accN);
        x.groundValid = true;
    }
    // ---- tires ----
    float gripBase = Clamp(m.grip, 0.3f, 2.f);
    float staticSum = 0.f, rearDrive = 0.f;
    for (int i = 0; i < s.wheelCount; i++) {
        staticSum += t.staticLoad[i];
        if (t.rear[i]) rearDrive += t.driveShare[i];
    }
    // drift: a rear-driven car sliding under power keeps its wheelspin and the stability aid backs off while the driver
    // countersteers (or just after a handbrake flick); steering into the slide or letting go lets the car recover
    float betaBody = atan2f(x.vLocal.x, Max(fabsf(x.vLocal.y), 1.f));
    bool counter = c.steer * b.angVel.z > 0.f && fabsf(c.steer) > 0.1f && fabsf(b.angVel.z) > 0.15f;  // + steer = right, + yaw = left
    bool sliding = road && rearDrive > 0.3f && c.throttle > 0.4f && fabsf(betaBody) > 0.15f && x.speed > 5.f;
    // a drift starts with a countersteer or a handbrake flick and lasts while the driver keeps the slide going with
    // throttle and steering (either way); lifting off, centering the wheel or regaining grip ends it
    if (sliding && (counter || s.hbTimer < 1.5f || (s.driftTimer > 0.f && fabsf(c.steer) > 0.1f))) s.driftTimer = 0.3f;
    else s.driftTimer = Max(s.driftTimer - dt, 0.f);
    bool drift = sliding && s.driftTimer > 0.f;
    bool frontC = false, rearC = false;
    for (int i = 0; i < s.wheelCount; i++) {
        const WheelSpec& ws = m.wheels[i];
        WheelState& w = s.wheels[i];
        const WW& o = ww[i];
        float rad = ws.radius * (w.burst ? 0.8f : 1.f);
        float share = t.driveShare[i];
        float Tb = t.brakeT[i] * dc.brake;
        if (dc.burnout) {
            // the burnout axle (rear when driven, else the driven front) spins free of the service brake,
            // the other axle is braked hard and holds the car
            share = t.burnShare[i];
            Tb = share > 0.f ? 0.f : t.brakeT[i];
        }
        float Iw = t.wheelI[i] + dc.reflectedI * share;
        float Td = dc.wheelTorque * share;
        bool hb = dc.handbrake && t.rear[i];
        if (hb) Tb = Max(Tb, t.brakeT[i] * 2.5f);
        if ((plane || heli) && ws.steer) Tb = heli ? Tb : 0.f;
        float Tbear = 0.4f + 0.004f * Iw * fabsf(w.spinVel);
        if (!o.contact) {
            // free wheel: drive, brakes, bearing drag
            float wn = w.spinVel + Td / Iw * dt;
            float bs = (Tb + Tbear) / Iw * dt;
            wn = fabsf(wn) <= bs ? 0.f : wn - (wn > 0.f ? bs : -bs);
            w.spinVel = wn;
            w.contact = false;
            w.load = 0.f;
            w.slip = 0.f;
            w.lateralSlip = 0.f;
            w.spinAngle = fmodf(w.spinAngle + w.spinVel * dt, kTwoPi);
            continue;
        }
        if (ws.pos.y > t.com.y) frontC = true;
        else rearC = true;
        vec3 fwdG, latG;
        if (bike) {
            // bikes steer in the ground plane (the lean does not eat the steering angle)
            vec3 f0 = x.fwd - o.n * dot(x.fwd, o.n);
            float fl = length(f0);
            f0 = fl > 1e-4f ? f0 / fl : x.fwd;
            vec3 l0 = cross(f0, o.n);
            fwdG = f0 * cosf(w.steerAngle) + l0 * sinf(w.steerAngle);
            latG = cross(fwdG, o.n);
        } else {
            vec3 hl(sinf(w.steerAngle), cosf(w.steerAngle), 0.f);
            vec3 hdg = b.R * hl;
            fwdG = hdg - o.n * dot(hdg, o.n);
            float fl = length(fwdG);
            fwdG = fl > 1e-4f ? fwdG / fl : x.fwd;
            latG = cross(fwdG, o.n);
        }
        vec3 vC = b.velAt(o.r);
        float vx = dot(vC, fwdG), vy = dot(vC, latG);
        float N = o.load;
        float loadRatio = N / Max(t.staticLoad[i], 1.f);
        float mu = gripBase * surfaceGrip(o.surface) * wetGrip(o.surface, x.speed) * Clamp(1.f - 0.12f * (loadRatio - 1.f), 0.75f, 1.12f);
        if (w.burst) mu *= 0.4f;
        if (dc.brake > 0.05f && !dc.burnout && fabsf(vx) > 1.f) mu *= t.brakeGrip;   // brake upgrades: pads / ABS tuning
        float Fmax = mu * N;
        float mShare = b.mass * Max(N / Max(totalLoad, 1.f), 0.5f * t.staticLoad[i] / Max(staticSum, 1.f));
        // rear tires a little stiffer in cornering than the fronts (wider rears / toe-in): a mild understeer bias keeps
        // cars straight and stable at high speed under power while the peak grip stays the same
        const float kp = t.kappaPeak, ap = t.alphaPeak * (t.rear[i] && !bike ? 0.88f : 1.f);
        float Vx = Max(fabsf(vx), 0.5f);
        float sy = atan2f(vy, Max(fabsf(vx), 1.5f)) / ap;
        // implicit wheel-spin update (bracketed Newton on the monotone tire envelope)
        TireEval te{Fmax, rad, vx, Vx, sy, kp};
        bool lockedW = false;
        float wn = solveWheelSpin(te, w.spinVel, Iw, dt, Td, Tb + Tbear, lockedW);
        // ABS on the service brakes (not the handbrake): hold the tire at peak slip
        if (!hb && dc.brake > 0.05f && fabsf(vx) > 1.5f) {
            float lim = (vx - (vx > 0.f ? 1.f : -1.f) * kp * 1.05f * Vx) / rad;
            if (vx > 0.f && wn < lim) wn = lim;
            if (vx < 0.f && wn > lim) wn = lim;
        }
        // traction control (friction-circle aware): limit drive slip so that lateral grip survives
        if (!hb && !dc.burnout && fabsf(Td) > 1.f) {
            // generous at launch (burnouts, fishtails), just past peak at speed (no power spin-outs on a straight)
            float sLow = t.tcSlip / kp, sHigh = Min(sLow, 0.85f);
            float sLim = bike ? 1.25f : Lerp(sLow, sHigh, SmoothStep(8.f, 25.f, fabsf(vx))) * (s.hbTimer < 1.f || drift ? 3.f : 1.f);
            float sxMax = sqrtf(Max(sLim * sLim - sy * sy, 0.25f));
            float dvMax = sxMax * kp * Vx;
            if (Td > 0.f && vx > -1.f) wn = Min(wn, Max((vx + dvMax) / rad, w.spinVel - 400.f * dt));
            if (Td < 0.f && vx < 1.f) wn = Max(wn, Min((vx - dvMax) / rad, w.spinVel + 400.f * dt));
        }
        float fx, fy, dfx, sAbs;
        te.eval(wn, false, fx, fy, dfx, sAbs);
        if (dc.burnout && share > 0.f) fx *= 0.7f;   // smoking rubber: the braked axle holds the car in place
        (void)lockedW;
        // caps: never push harder than what cancels the slip velocity within this step
        float capX = fabsf(wn * rad - vx) * mShare / dt;
        fx = Clamp(fx, -capX, capX);
        float capY = fabsf(vy) * mShare / dt;
        fy = Clamp(fy, -capY, capY);
        // rolling resistance
        float rr = surfaceRolling(o.surface) * N * (w.burst ? 4.f : 1.f);
        rr = Min(rr, fabsf(vx) * mShare / dt);
        fx -= vx > 0.f ? rr : (vx < 0.f ? -rr : 0.f);
        w.spinVel = wn;
        w.spinAngle = fmodf(w.spinAngle + wn * dt, kTwoPi);
        // apply
        float h = -dot(o.r, x.up);
        vec3 rLat = o.r + x.up * (h * t.rollArm);
        b.addForce(o.n * N, o.r);
        b.addForce(fwdG * fx, o.r);
        b.addForce(latG * fy, rLat);
        if (bike) x.tireRollTorque += dot(cross(o.r, o.n * N + fwdG * fx) + cross(rLat, latG * fy), x.fwd);
        w.contact = true;
        w.contactPos = o.r + b.R * t.com;
        w.contactNormal = o.n;
        w.surface = o.surface;
        w.load = N;
        w.slip = Clamp((sAbs - 0.8f) / 1.2f, 0.f, 4.f);   // 0 grip .. 1 sliding .. 4 burnout / locked at speed
        w.lateralSlip = vy;
    }
    // limited-slip coupling between driven wheels of an axle
    for (int i = 0; i < s.wheelCount; i++) {
        int j = t.arbPair[i];
        if (j <= i || t.driveShare[i] <= 0.f || t.driveShare[j] <= 0.f) continue;
        float avg = 0.5f * (s.wheels[i].spinVel + s.wheels[j].spinVel);
        s.wheels[i].spinVel = Lerp(s.wheels[i].spinVel, avg, 0.2f);
        s.wheels[j].spinVel = Lerp(s.wheels[j].spinVel, avg, 0.2f);
    }
    // ---- assists ----
    vec3 wl = b.local(b.angVel);
    if (road) {
        float v = x.speed;
        // stability: damp yaw beyond what the steering asks for (off while using the handbrake)
        if (t.esc > 0.f && nContact >= 3 && v > 5.f && s.hbTimer > 0.7f && x.vFwd > 0.f) {
            float steerA = 0.f;
            for (int i = 0; i < s.wheelCount; i++)
                if (m.wheels[i].steer) steerA = s.wheels[i].steerAngle;
            float rKin = x.vFwd * tanf(-steerA) / t.wheelbase;  // + = CCW (left)
            float rMax = 1.2f * gripBase * kGrav / v;
            rKin = Clamp(rKin, -rMax, rMax);
            float err = wl.z - rKin;
            float beta = atan2f(x.vLocal.x, x.vLocal.y);
            bool over = err * wl.z > 0.f;
            float gain = t.esc * (over ? 2.5f : 0.6f) * SmoothStep(0.03f, 0.12f, fabsf(beta) + fabsf(err) * 0.15f);
            // at motorway speeds the car tracks the steering: a neutral-steering chassis putting its power down would
            // otherwise wander off into a slow spin
            gain += 2.f * SmoothStep(20.f, 45.f, v);
            if (drift) gain *= 0.2f;
            b.torque += b.torqueFor(x.up * (-err * gain));
        }
        // air control (GTA-style) and flip-back
        bool airborne = nContact == 0 && s.airborneTime > 0.08f;
        float rollIn = fabsf(c.roll) > 0.01f ? c.roll : c.steer;
        if (airborne && c.hasDriver) {
            vec3 acc(c.pitch * 3.f - wl.x * 1.2f, rollIn * 3.f - wl.y * 1.2f, -wl.z * 0.4f);
            b.torque += b.torqueFor(b.R * acc);
        }
        if (x.up.z < 0.45f && x.speed < 3.f && nContact < 2 && fabsf(rollIn) > 0.1f && c.hasDriver) {
            vec3 ax = cross(x.up, vec3(0, 0, 1));
            float dir = dot(ax, x.fwd);
            float sgn = fabsf(dir) > 0.2f ? (dir > 0.f ? 1.f : -1.f) : (rollIn > 0.f ? 1.f : -1.f);
            float need = b.mass * kGrav * Max(t.boxH.x, t.boxH.z) * 1.5f;
            float rate = dot(b.angVel, x.fwd) * sgn;
            if (rate < 2.5f) b.torque += x.fwd * (sgn * need * fabsf(rollIn));
        }
    }
    if (bike) {
        float driveIn = s.gear < 0 ? 0.f : Saturate(c.throttle);
        bikeAssist(x, frontC, rearC, driveIn, dc.brake + (c.handbrake ? 0.5f : 0.f));
    }
    // aero drag + downforce (road vehicles and bikes; aircraft handle their own)
    if (road || bike) {
        float v2 = x.speed * x.speed;
        if (v2 > 0.01f) {
            float dragA = t.dragArea * (bike && c.pitch < -0.5f ? 0.85f : 1.f);
            b.force -= b.vel * (0.5f * kRhoAir * dragA * x.speed);
            float vf = Max(x.vFwd, 0.f);
            float down = 0.5f * kRhoAir * t.liftArea * vf * vf;
            if (down > 0.f && nContact > 0) {
                vec3 rF = b.R * vec3(0.f, t.frontY - t.com.y, -t.com.z * 0.5f), rR = b.R * vec3(0.f, t.rearY - t.com.y, -t.com.z * 0.5f);
                b.addForce(-x.up * (down * 0.45f), rF);
                b.addForce(-x.up * (down * 0.55f), rR);
            }
        }
    }
    (void)boat;
}

}  // namespace vsim
}  // namespace Vehicles
