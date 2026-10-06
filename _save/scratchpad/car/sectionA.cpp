// ------------------------------------------------------------------------------------------------
// Getting in and out of a car through an opening door (AnimInput::car), authored at run time for the car at hand. It
// is authored for a door on the vehicle's left (a right one is mirrored), in the ped's model space at the reference
// body's size (the door's positions divided by the leg scale), with the ped held still by the game where carEntrySpot /
// carExitSpot put it.
//
// Getting in: the left hand reaches the outer handle, pops the latch and pulls the door open, stepping back as it
// swings; lets go (the door swings on), takes the roof rail, turns and steps the right foot over the sill into the
// footwell, ducks under the roof and sits down sideways onto the seat, lifts the left leg in over the sill, reaches out
// for the inner pull and pulls the door shut; then settles (hands to the wheel) - and buckles up (the belt layer).
// Getting out: unbuckles, pulls the inner release, pushes the door open as the left foot swings out onto the ground,
// turns out on the seat with the head down, stands up outside holding the top of the door, and pushes the door shut
// as it steps away.

// key times (s); getting out, after unbuckling (kOutBelt when belted)
static const float kInReach = 0.30f, kInUnlatch = 0.38f, kInPull = 0.78f, kInRelease = 0.95f, kInLift = 1.10f, kInStep = 1.26f,
                   kInSit = 1.60f, kInLegLift = 1.76f, kInLegIn = 1.92f, kInGrab = 2.10f, kInShut = 2.38f, kInEnd = 2.68f;
static const float kInOpenPull = 0.42f, kInOpenRest = 0.7f;     // the door's opening let go of / swung on to
static const float kOutBelt = 0.48f, kOutBeltOff = 0.3f;        // unbuckling first (the belt is off at kOutBeltOff)
static const float kOutHandle = 0.22f, kOutPop = 0.30f, kOutLift = 0.46f, kOutPush = 0.62f, kOutTurn = 0.98f, kOutStand = 1.36f,
                   kOutShutGrab = 1.54f, kOutShut = 1.84f, kOutEnd = 2.08f;
static const float kOutOpen = 0.78f;                            // pushed open to
static const float kBeltClick = 0.82f;                          // buckling up: the tongue clicks into the buckle

// Rigid move of a whole rig: turned by `yaw` about the model's vertical axis, then shifted by `off`.
static void transformRig(const AuthorCtx& A, Rig& r, float yaw, vec3 off) {
    quat q = qz(yaw);
    r.pelvis = rotate(q, A.pelvisBind + r.pelvis) + off - A.pelvisBind;
    r.pelvisYaw += yaw;
    for (int s = 0; s < 2; s++) {
        ArmCtl& a = r.arm[s];
        if (a.ik) {
            a.target = rotate(q, a.target) + off;
            a.ikPole = rotate(q, a.ikPole);
            if (a.orient) a.handRot = normalize(q * a.handRot);
        }
        LegCtl& l = r.leg[s];
        if (l.ik) {
            l.ankle = rotate(q, l.ankle) + off;
            l.knee = rotate(q, l.knee);
            l.yaw += yaw;
            if (l.footQ) l.footRot = normalize(q * l.footRot);
        }
    }
}

// The standing pose with its arms as IK (every key of the car clips drives the limbs the same way: no conversions)
static Rig standIK(const AuthorCtx& A) {
    Rig r;
    standPose(A, r);
    Pose p;
    rigToPose(A, r, p);
    for (int s = 0; s < 2; s++) armToIK(A, p, s, r.arm[s]);
    return r;
}

// The door (left one) in the authoring frame, with the measures the poses are placed by: s along the vehicle's
// forward axis, d out from its side (from the model origin), z up.
struct CarGeo {
    CarDoorInfo g;
    vec3 F, N, I, U;
    float carYaw = 0.f;        // yaw facing the vehicle's forward axis
    float sR = 0.f, sF = 0.f;  // the opening's rear and front end
    float dSkin = 0.f;         // the body side at the sill
    float sSeat = 0.f, dSeat = 0.f, zSeat = 0.f;
    float sP = 0.f;            // where the hips go in through the opening
    float dRail = 0.f, zRail = 0.f;
    float sH = 0.f;            // the outer handle
    vec3 at(float s, float d, float z) const { return F * s + N * d + U * z; }
    quat doorQ(float open) const { return quatAxisAngle(g.axis, open * g.maxOpen); }
    vec3 door(vec3 p, float open) const { return g.hinge + rotate(doorQ(open), p - g.hinge); }
    vec3 doorV(vec3 v, float open) const { return rotate(doorQ(open), v); }
};
static CarGeo carGeo(const CarDoorInfo& g) {
    CarGeo c;
    c.g = g;
    c.U = vec3(0, 0, 1);
    c.F = nrmOr(vec3(g.fwd.x, g.fwd.y, 0.f), vec3(-1, 0, 0));
    c.N = nrmOr(vec3(g.out.x, g.out.y, 0.f), vec3(0, -1, 0));
    c.I = -c.N;
    c.carYaw = atan2f(-c.F.x, c.F.y);
    c.sR = dot(g.rear, c.F);
    c.sF = dot(g.front, c.F);
    c.dSkin = 0.5f * (dot(g.rear, c.N) + dot(g.front, c.N));
    c.sSeat = dot(g.seat, c.F);
    c.dSeat = dot(g.seat, c.N);
    c.zSeat = g.seat.z;
    float lo = c.sR + 0.2f, hi = c.sF - 0.3f;
    c.sP = lo < hi ? Clamp(c.sSeat, lo, hi) : 0.5f * (c.sR + c.sF);
    c.dRail = dot(g.grip, c.N);
    c.zRail = g.grip.z;
    c.sH = dot(g.handle, c.F);
    return c;
}
// Mirror a door on the right into a left one (x -> -x; the hinge axis is a rotation axis: its opening sense flips)
static CarDoorInfo mirrorDoor(const CarDoorInfo& g) {
    CarDoorInfo m = g;
    auto mx = [](vec3 v) { return vec3(-v.x, v.y, v.z); };
    m.seat = mx(g.seat);
    m.fwd = mx(g.fwd);
    m.out = mx(g.out);
    m.hinge = mx(g.hinge);
    m.axis = vec3(g.axis.x, -g.axis.y, -g.axis.z);
    m.handle = mx(g.handle);
    m.handleIn = mx(g.handleIn);
    m.grip = mx(g.grip);
    m.front = mx(g.front);
    m.rear = mx(g.rear);
    return m;
}
static CarDoorInfo scaleDoor(const CarDoorInfo& g, float k) {
    CarDoorInfo m = g;
    m.seat = g.seat * k;
    m.hinge = g.hinge * k;
    m.handle = g.handle * k;
    m.handleIn = g.handleIn * k;
    m.grip = g.grip * k;
    m.front = g.front * k;
    m.rear = g.rear * k;
    m.sillZ = g.sillZ * k;
    m.roofZ = g.roofZ * k;
    return m;
}

// The door's opening (0 shut .. 1 = maxOpen) at clip time t.
static float carOpenAt(bool enter, bool belt, float t) {
    if (enter) {
        if (t <= kInReach) return 0.f;
        if (t <= kInUnlatch) return 0.05f * sstep(kInReach, kInUnlatch, t);
        if (t <= kInPull) return lerp(0.05f, kInOpenPull, sstep(kInUnlatch, kInPull, t));
        const float tSwing = kInRelease + 0.3f;
        if (t <= tSwing) {
            float u = lstep(kInPull, tSwing, t);
            return lerp(kInOpenPull, kInOpenRest, 1.f - (1.f - u) * (1.f - u));   // swings on after the pull, slowing
        }
        if (t <= kInGrab) return kInOpenRest;
        if (t <= kInShut) {
            float u = lstep(kInGrab, kInShut, t);
            return kInOpenRest * (1.f - u * u);   // pulled shut, speeding up into the slam
        }
        return 0.f;
    }
    t -= belt ? kOutBelt : 0.f;
    if (t <= kOutHandle) return 0.f;
    if (t <= kOutPop) return 0.05f * sstep(kOutHandle, kOutPop, t);
    if (t <= kOutPush) return lerp(0.05f, kOutOpen, sstep(kOutPop, kOutPush, t));
    if (t <= kOutShutGrab) return kOutOpen;
    if (t <= kOutShut) {
        float u = lstep(kOutShutGrab, kOutShut, t);
        return kOutOpen * (1.f - u * u);
    }
    return 0.f;
}
static float carClipLen(bool enter, bool belt) { return enter ? kInEnd : (belt ? kOutBelt : 0.f) + kOutEnd; }

// Hand grips used on the door (left hand, authoring frame): the outer pull handle from below (thumb forwards, palm up,
// fingers hooked behind the bar), the inner pull bar from above (fingers over it towards the door)
static vec3 outerFingers(const CarGeo& c) { return normalize(c.I + c.U * 0.35f); }
static vec3 innerFingers(const CarGeo& c) { return normalize(c.N + c.U * 0.3f - c.U * 0.0f); }

// The seated pose at the end of getting in / the start of getting out
static Rig carSeated(const AuthorCtx& A, const CarGeo& c) {
    Rig e;
    if (c.g.driver) drivePose(A, e, 0.f);
    else passengerPose(A, e, 0.f, 4.f);
    transformRig(A, e, c.carYaw, c.g.seat - vec3(0.f, 0.f, kSeatHipZ));
    return e;
}
static float limbReach(const AuthorCtx& A) {
    return length(A.sk.bindLocalPos[B_FOREARM_L]) + length(A.sk.bindLocalPos[B_HAND_L]) + A.D.palmLen * 0.8f;
}
// Hand target on a point with the palm down (resting on a rail / the top of a door)
static void palmDown(const AuthorCtx& A, ArmCtl& a, int sd, vec3 p, vec3 fingers, vec3 pole) {
    vec3 f = nrmOr(fingers - vec3(0, 0, 1) * dot(fingers, vec3(0, 0, 1)), vec3(0, 1, 0));
    armIK(a, p + vec3(0, 0, 0.035f) - f * (A.D.palmLen * 0.5f), pole, 0.55f);
    a.orient = true;
    a.handRot = handFrame(A, sd, f, vec3(0, 0, -1));
}
// Plain hand target (wrist) with a hand frame
static void handAt(const AuthorCtx& A, ArmCtl& a, int sd, vec3 wrist, vec3 fingers, vec3 palm, vec3 pole, float curl) {
    armIK(a, wrist, pole, curl);
    a.orient = true;
    vec3 f = normalize(fingers);
    a.handRot = handFrame(A, sd, f, nrmOr(palm - f * dot(palm, f), vec3(0, 0, -1)));
}
static vec3 hipMid(const AuthorCtx& A) { return (A.hip[0] + A.hip[1]) * 0.5f; }
// Yaw of a horizontal direction (0 = +y)
static float yawOf(vec3 v) { return atan2f(-v.x, v.y); }

static void carEntryKeys(const AuthorCtx& A, const CarGeo& c, std::vector<Key>& K) {
    const Rig st = standIK(A);
    const float hipZ = hipMid(A).z, fh = A.footH, reachL = limbReach(A);
    const vec3 U = c.U, F = c.F, N = c.N, I = c.I;
    K.clear();
    K.push_back({0.f, st});
    // reach for the outer handle (a step towards it when it is far)
    const vec3 G = c.g.handle - U * 0.006f;
    const vec3 fing1 = outerFingers(c);
    const vec3 pole1 = normalize(-U + F * 0.3f + N * 0.4f);
    vec3 toG = G - A.gh[0];
    float yawG = yawOf(toG);
    float over = Max(0.f, length(toG) - 0.92f * reachL);
    vec3 stepV = over > 0.f ? normalize(vec3(toG.x, toG.y, 0.f)) * Min(over * 1.15f, 0.32f) : vec3(0.f);
    Rig r1 = st;
    r1.pelvisYaw = 0.12f * yawG;
    setFootFlat(A, r1.leg[0], A.ankle[0] + stepV, st.leg[0].yaw + 0.25f * yawG);
    placeHips(A, r1, hipMid(A) + stepV * 0.5f - U * 0.012f);
    r1.spineYaw = 0.35f * yawG;
    r1.spinePitch = 0.1f + Min(over, 0.3f) * 0.6f;
    r1.headYaw = 0.4f * yawG;
    r1.headPitch = 0.28f;
    gripArm(A, r1.arm[0], 0, G, F, fing1, pole1);
    r1.arm[0].fingers = 0.75f;
    K.push_back({kInReach, r1});
    // pop the latch
    Rig r2 = r1;
    {
        float o = carOpenAt(true, false, kInUnlatch);
        gripArm(A, r2.arm[0], 0, c.door(G, o), c.doorV(F, o), c.doorV(fing1, o), pole1);
        r2.arm[0].fingers = 0.9f;
        r2.spinePitch -= 0.03f;
    }
    K.push_back({kInUnlatch, r2});
    // pull it open, stepping back as it swings out
    Rig r3 = r2;
    {
        float o = carOpenAt(true, false, kInPull);
        vec3 G3 = c.door(G, o);
        r3.pelvisYaw = 0.22f * c.carYaw;
        setFootFlat(A, r3.leg[1], A.ankle[1] + N * 0.14f - F * 0.05f, st.leg[1].yaw + 0.12f * c.carYaw);
        r3.leg[0].yaw = st.leg[0].yaw + 0.3f * c.carYaw;
        placeHips(A, r3, hipMid(A) + stepV * 0.35f + N * 0.07f - U * 0.02f);
        float yawG3 = yawOf(G3 - A.gh[0]);
        r3.spineYaw = 0.6f * (yawG3 - r3.pelvisYaw);
        r3.spinePitch = 0.02f;
        r3.headYaw = 0.5f * (yawG3 - r3.pelvisYaw);
        r3.headPitch = 0.2f;
        gripArm(A, r3.arm[0], 0, G3, c.doorV(F, o), c.doorV(fing1, o), normalize(-U + F * 0.5f + N * 0.2f));
        r3.arm[0].fingers = 0.9f;
    }
    K.push_back({kInPull, r3});
    // let go (the door swings on), turn towards the doorway, the left foot steps up to it; the left hand goes to the
    // roof rail over the opening
    const vec3 R = c.at(c.sP + 0.1f, c.dRail - 0.025f, c.zRail + 0.06f);
    const vec3 railPole = normalize(-U + N * 0.6f);
    Rig r4 = r3;
    {
        r4.pelvisYaw = 0.48f * c.carYaw;
        setFootFlat(A, r4.leg[0], c.at(c.sP + 0.1f, c.dSkin + 0.44f, 0.f), 0.55f * c.carYaw);
        placeHips(A, r4, c.at(c.sP - 0.08f, c.dSkin + 0.52f, hipZ - 0.04f));
        r4.spineYaw = 0.2f * (c.carYaw - r4.pelvisYaw);
        r4.spinePitch = 0.12f;
        r4.headYaw = 0.3f * (c.carYaw - r4.pelvisYaw);
        r4.headPitch = 0.15f;
        palmDown(A, r4.arm[0], 0, lerp(c.door(G, kInOpenPull), R, 0.55f) + U * 0.06f, F, railPole);
    }
    K.push_back({kInRelease, r4});
    // the right foot comes up over the sill, the body leaning in, the head going down; a hand on the rail
    Rig r5 = r4;
    {
        r5.pelvisYaw = 0.72f * c.carYaw;
        placeHips(A, r5, c.at(c.sP - 0.05f, c.dSkin + 0.36f, hipZ - 0.11f));
        LegCtl& l = r5.leg[1];
        l.ik = true;
        l.footQ = false;
        l.ankle = c.at(c.sP + 0.06f, c.dSkin + 0.06f, c.g.sillZ + 0.14f + fh);
        l.pitch = -0.35f;
        l.roll = 0.f;
        l.yaw = 0.8f * c.carYaw;
        l.knee = normalize(F * 0.6f + U * 0.7f + I * 0.1f);
        r5.spineYaw = 0.1f * (c.carYaw - r5.pelvisYaw);
        r5.spinePitch = 0.3f;
        r5.spineRoll = 0.12f;
        r5.headYaw = 0.f;
        r5.headPitch = 0.24f;
        palmDown(A, r5.arm[0], 0, R, F, railPole);
        handAt(A, r5.arm[1], 1, c.at(c.sSeat + 0.12f, c.dSeat - 0.02f, c.zSeat + 0.22f), F * 0.6f + I * 0.4f - U * 0.3f, -U, normalize(N - U), 0.4f);
    }
    K.push_back({kInLift, r5});
    // the right foot into the footwell, the hips over the sill's edge, ducked under the roof
    Rig r6 = r5;
    {
        setFootFlat(A, r6.leg[1], c.at(c.sSeat + 0.42f, c.dSeat + 0.02f, c.g.sillZ), c.carYaw);
        r6.leg[1].knee = normalize(F + U * 0.5f);
        r6.pelvisYaw = 0.86f * c.carYaw;
        r6.pelvisPitch = -0.08f;
        placeHips(A, r6, c.at(c.sP - 0.02f, c.dSkin + 0.2f, Max(c.zSeat + 0.16f, hipZ - 0.24f)));
        r6.spinePitch = 0.42f;
        r6.spineRoll = 0.16f;
        r6.headPitch = 0.28f;
        palmDown(A, r6.arm[0], 0, R, F, railPole);
        handAt(A, r6.arm[1], 1, c.at(c.sSeat + 0.06f, c.dSeat - 0.24f, c.zSeat + 0.06f), F * 0.7f + I * 0.3f - U * 0.4f, -U, normalize(N - U), 0.3f);
    }
    K.push_back({kInStep, r6});
    // sit: the hips onto the seat, facing ahead, still ducked; the left foot outside on the ground
    const vec3 hipSit = c.at(Clamp(c.sSeat, c.sR + 0.12f, c.sF - 0.2f), c.dSeat, c.zSeat + 0.02f);
    Rig r7 = r6;
    {
        r7.pelvisYaw = c.carYaw;
        r7.pelvisPitch = -0.3f;
        placeHips(A, r7, hipSit);
        setFootFlat(A, r7.leg[0], c.at(c.sSeat + 0.24f, c.dSkin + 0.27f, 0.f), c.carYaw + 0.35f);
        r7.leg[0].knee = normalize(F + U * 0.5f + N * 0.3f);
        r7.spineYaw = 0.f;
        r7.spinePitch = 0.36f;
        r7.spineRoll = 0.08f;
        r7.headPitch = 0.15f;
        handAt(A, r7.arm[0], 0, c.at(c.sSeat + 0.28f, c.dSkin - 0.06f, c.zSeat + 0.36f), F + N * 0.3f, -I, normalize(N - U), 0.4f);
        handAt(A, r7.arm[1], 1, c.at(c.sSeat + 0.08f, c.dSeat - 0.26f, c.zSeat + 0.05f), F * 0.7f + I * 0.3f - U * 0.4f, -U, normalize(N - U), 0.3f);
    }
    K.push_back({kInSit, r7});
    // the left leg lifts in over the sill
    const Rig seated = carSeated(A, c);
    Rig r8 = r7;
    {
        placeHips(A, r8, c.g.seat);
        LegCtl& l = r8.leg[0];
        l.ik = true;
        l.footQ = false;
        l.ankle = c.at(c.sSeat + 0.38f, c.dSkin - 0.05f, c.g.sillZ + 0.16f + fh);
        l.pitch = -0.45f;
        l.roll = 0.f;
        l.yaw = c.carYaw + 0.1f;
        l.knee = normalize(F + U);
        r8.spinePitch = 0.28f;
        r8.spineRoll = 0.04f;
        r8.headPitch = 0.1f;
        handAt(A, r8.arm[0], 0, c.at(c.sSeat + 0.32f, c.dSkin + 0.02f, c.zSeat + 0.4f), F + N * 0.4f, -I, normalize(N - U), 0.4f);
    }
    K.push_back({kInLegLift, r8});
    // in: both feet in the footwell, reaching out for the door's inner pull
    const vec3 Gin = c.g.handleIn + U * 0.004f;
    const vec3 finIn = innerFingers(c);
    const vec3 poleIn = normalize(-U * 0.8f + N * 0.4f - F * 0.2f);
    auto reachDoor = [&](Rig& r, float o, float lean) {
        vec3 Gd = c.door(Gin, o);
        gripArm(A, r.arm[0], 0, Gd, c.doorV(F, o), c.doorV(finIn, o), poleIn);
        r.arm[0].fingers = 0.85f;
        // the left shoulder of the seated body (approx.), leaning out towards a door out of reach
        vec3 sh = c.g.seat + N * 0.19f + U * 0.48f;
        float far = Max(0.f, length(Gd - sh) - 0.88f * reachL);
        r.spineRoll = -Min(far * 1.6f, 0.42f) * lean;
        r.spineYaw = Min(far * 1.2f, 0.35f) * lean;
        r.headYaw = 0.5f * lean;
        r.headPitch = 0.05f;
    };
    Rig r9 = r8;
    {
        r9.leg[0] = seated.leg[0];
        r9.leg[1] = seated.leg[1];
        r9.spinePitch = 0.2f;
        reachDoor(r9, kInOpenRest, 0.7f);
        r9.arm[0].fingers = 0.4f;
        r9.arm[1] = seated.arm[1];
    }
    K.push_back({kInLegIn, r9});
    Rig r10 = r9;
    reachDoor(r10, kInOpenRest, 1.f);
    K.push_back({kInGrab, r10});
    // pulled shut
    Rig r11 = r10;
    {
        reachDoor(r11, 0.f, 0.f);
        r11.spinePitch = 0.12f;
        r11.headYaw = 0.25f;
    }
    K.push_back({kInShut, r11});
    K.push_back({kInEnd, seated});
}

static void carExitKeys(const AuthorCtx& A, const CarGeo& c, std::vector<Key>& K) {
    const Rig st = standIK(A);
    const float hipZ = hipMid(A).z, fh = A.footH;
    const vec3 U = c.U, F = c.F, N = c.N, I = c.I;
    const Rig seated = carSeated(A, c);
    K.clear();
    K.push_back({0.f, seated});
    float t0 = 0.f;
    if (c.g.belt) {
        // unbuckle: the left hand presses the buckle by the right hip and guides the belt back up past the shoulder
        Rig b1 = seated;
        handAt(A, b1.arm[0], 0, c.g.seat + I * 0.19f - F * 0.03f - U * 0.04f, -U + F * 0.5f + I * 0.2f, -I * 0.3f - U * 0.5f + F * 0.2f,
               normalize(N - U * 0.5f), 0.5f);
        b1.spineYaw = -0.3f;
        b1.headYaw = -0.35f;
        b1.headPitch = 0.32f;
        K.push_back({0.18f, b1});
        Rig b2 = seated;
        handAt(A, b2.arm[0], 0, c.g.seat + N * 0.12f + F * 0.12f + U * 0.44f, U + F * 0.3f, I * 0.5f - F * 0.5f, normalize(N - U), 0.6f);
        b2.headYaw = 0.25f;
        b2.headPitch = 0.1f;
        K.push_back({0.38f, b2});
        t0 = kOutBelt;
    }
    const vec3 Gin = c.g.handleIn + U * 0.004f;
    const vec3 finIn = innerFingers(c);
    const vec3 poleIn = normalize(-U * 0.8f + N * 0.4f - F * 0.2f);
    // the inner release, the latch pops
    Rig x3 = seated;
    gripArm(A, x3.arm[0], 0, Gin, F, finIn, poleIn);
    x3.arm[0].fingers = 0.85f;
    x3.headYaw = 0.3f;
    x3.headPitch = 0.08f;
    K.push_back({t0 + kOutHandle, x3});
    Rig x4 = x3;
    {
        float o = carOpenAt(false, false, kOutPop);
        gripArm(A, x4.arm[0], 0, c.door(Gin, o), c.doorV(F, o), c.doorV(finIn, o), poleIn);
        x4.arm[0].fingers = 0.9f;
    }
    K.push_back({t0 + kOutPop, x4});
    // pushing the door open, the left foot swinging out over the sill
    Rig x5 = x4;
    {
        float o = carOpenAt(false, false, kOutLift);
        gripArm(A, x5.arm[0], 0, c.door(Gin, o), c.doorV(F, o), c.doorV(finIn, o), poleIn);
        x5.arm[0].fingers = 0.7f;
        LegCtl& l = x5.leg[0];
        l.ik = true;
        l.footQ = false;
        l.ankle = c.at(c.sSeat + 0.36f, c.dSkin - 0.02f, c.g.sillZ + 0.16f + fh);
        l.pitch = -0.3f;
        l.roll = 0.f;
        l.yaw = c.carYaw + 0.25f;
        l.knee = normalize(F + U * 0.7f + N * 0.3f);
        x5.spinePitch = 0.25f;
        x5.spineRoll = -0.1f;
        x5.headYaw = 0.35f;
    }
    K.push_back({t0 + kOutLift, x5});
    const vec3 Gtop = c.g.grip + U * 0.035f;   // on top of the door near its rear edge
    const vec3 topPole = normalize(-U + N * 0.3f - F * 0.3f);
    Rig x6 = x5;
    {
        setFootFlat(A, x6.leg[0], c.at(c.sSeat + 0.26f, c.dSkin + 0.3f, 0.f), c.carYaw + 0.45f);
        x6.leg[0].knee = normalize(F + U * 0.4f + N * 0.4f);
        palmDown(A, x6.arm[0], 0, c.door(Gtop, kOutOpen), c.doorV(-F, kOutOpen), topPole);
        x6.spinePitch = 0.3f;
        x6.spineRoll = -0.12f;
        x6.headYaw = 0.3f;
    }
    K.push_back({t0 + kOutPush, x6});
    // turned out on the seat, the hips at the sill, head down; the right foot comes out to the sill
    Rig x7 = x6;
    {
        x7.pelvisYaw = c.carYaw + 0.72f;
        x7.pelvisPitch = -0.22f;
        placeHips(A, x7, c.at(lerp(c.sSeat, c.sP, 0.5f), c.dSeat + 0.55f * (c.dSkin - c.dSeat), c.zSeat + 0.04f));
        setFootFlat(A, x7.leg[1], c.at(c.sSeat + 0.22f, c.dSkin - 0.12f, c.g.sillZ + 0.02f), c.carYaw + 0.5f);
        x7.leg[1].knee = normalize(F + U * 0.5f + N * 0.3f);
        x7.spineYaw = 0.f;
        x7.spinePitch = 0.46f;
        x7.spineRoll = -0.05f;
        x7.headYaw = 0.f;
        x7.headPitch = 0.22f;
        handAt(A, x7.arm[1], 1, c.at(c.sSeat - 0.02f, c.dSeat + 0.08f, c.zSeat + 0.04f), F * 0.5f - U * 0.6f, -U, normalize(I - U), 0.3f);
    }
    K.push_back({t0 + kOutTurn, x7});
    // standing outside, holding the top of the door
    Rig x8 = x7;
    {
        x8.pelvisYaw = c.carYaw + 0.5f;
        x8.pelvisPitch = 0.f;
        placeHips(A, x8, c.at(c.sP - 0.04f, c.dSkin + 0.42f, hipZ - 0.05f));
        setFootFlat(A, x8.leg[1], c.at(c.sP - 0.16f, c.dSkin + 0.28f, 0.f), c.carYaw + 0.3f);
        setFootFlat(A, x8.leg[0], c.at(c.sP + 0.12f, c.dSkin + 0.52f, 0.f), c.carYaw + 0.5f);
        x8.leg[0].knee = normalize(F * 0.3f + U * 0.2f + N * 0.5f);
        x8.leg[1].knee = normalize(F * 0.3f + U * 0.2f + N * 0.5f);
        x8.spinePitch = 0.12f;
        x8.spineRoll = 0.f;
        x8.headPitch = 0.f;
        handAt(A, x8.arm[1], 1, c.at(c.sP - 0.1f, c.dSkin + 0.3f, hipZ - 0.05f), -U + F * 0.2f, I, normalize(I - U), 0.3f);
    }
    K.push_back({t0 + kOutStand, x8});
    // facing away (the clip's root facing), a hand on the door's outside near its rear edge, pushing it shut
    const vec3 stHip = hipMid(A);
    Rig x9 = x8;
    {
        x9.pelvisYaw = 0.f;
        placeHips(A, x9, lerp(c.at(c.sP - 0.04f, c.dSkin + 0.42f, hipZ - 0.05f), stHip, 0.5f));
        x9.leg[0] = st.leg[0];
        setFootFlat(A, x9.leg[1], lerp(c.at(c.sP - 0.16f, c.dSkin + 0.28f, 0.f), A.ankle[1] - vec3(0, 0, A.footH), 0.5f), 0.5f * st.leg[1].yaw);
        vec3 nD = c.doorV(N, kOutOpen);
        vec3 p = c.door(c.g.grip - U * 0.06f, kOutOpen) + nD * 0.03f;
        vec3 fing = normalize(U * 0.5f + c.doorV(F, kOutOpen) * 0.5f);
        gripArm(A, x9.arm[0], 0, p, cross(-nD, fing), fing, normalize(-U + nD * 0.3f));
        x9.arm[0].fingers = 0.15f;
        x9.spineYaw = 0.25f;
        x9.spineRoll = -0.06f;
        x9.headYaw = 0.35f;
        x9.arm[1] = st.arm[1];
    }
    K.push_back({t0 + kOutShutGrab, x9});
    Rig x10 = x9;
    {
        placeHips(A, x10, stHip + F * 0.02f);
        x10.leg[1] = st.leg[1];
        vec3 p = c.g.grip - U * 0.06f + N * 0.07f;
        vec3 fing = normalize(U * 0.5f + F * 0.5f);
        gripArm(A, x10.arm[0], 0, p, cross(-N, fing), fing, normalize(-U + N * 0.3f));
        x10.arm[0].fingers = 0.15f;
        x10.spineYaw = 0.1f;
        x10.headYaw = 0.15f;
    }
    K.push_back({t0 + kOutShut, x10});
    K.push_back({t0 + kOutEnd, st});
}

// The hand on the door at clip time t (left hand, authoring frame): its grip (fist centre), handle axis, palm, the
// elbow's direction and how firmly it holds (0 not touching)
struct CarHold {
    vec3 pos, axis, palm, poleDir;
    float w = 0.f, fingers = 0.85f;
};
static CarHold carHoldAt(const CarGeo& c, bool enter, float t) {
    CarHold h;
    const vec3 U = c.U, F = c.F, N = c.N;
    float o = carOpenAt(enter, c.g.belt, t);
    auto set = [&](vec3 p, vec3 fing, vec3 axis, vec3 pole, float w, float curl) {
        h.pos = c.door(p, o);
        h.axis = normalize(c.doorV(axis, o));
        vec3 f = c.doorV(fing, o);
        h.palm = normalize(cross(f, h.axis));   // left hand: palm = fingers x thumb axis
        h.poleDir = pole;
        h.w = w;
        h.fingers = curl;
    };
    if (enter) {
        float wo = sstep(kInReach - 0.08f, kInReach, t) * (1.f - sstep(kInRelease - 0.05f, kInRelease + 0.02f, t));
        float wi = sstep(kInGrab - 0.1f, kInGrab, t) * (1.f - sstep(kInShut + 0.04f, kInShut + 0.16f, t));
        if (wo > 0.f) set(c.g.handle - U * 0.006f, outerFingers(c), F, normalize(-U + F * 0.3f + N * 0.4f), wo, 0.9f);
        else if (wi > 0.f) set(c.g.handleIn + U * 0.004f, innerFingers(c), F, normalize(-U * 0.8f + N * 0.4f - F * 0.2f), wi, 0.85f);
        return h;
    }
    float t0 = c.g.belt ? kOutBelt : 0.f;
    float wi = sstep(t0 + kOutHandle - 0.1f, t0 + kOutHandle, t) * (1.f - sstep(t0 + kOutLift, t0 + kOutPush - 0.04f, t));
    float ws = sstep(t0 + kOutShutGrab - 0.08f, t0 + kOutShutGrab, t) * (1.f - sstep(t0 + kOutShut - 0.02f, t0 + kOutShut + 0.08f, t));
    if (wi > 0.f) set(c.g.handleIn + U * 0.004f, innerFingers(c), F, normalize(-U * 0.8f + N * 0.4f - F * 0.2f), wi, 0.85f);
    else if (ws > 0.f) {
        // flat hand on the door's outside near its rear top corner, pushing it shut
        vec3 fing = normalize(U * 0.5f + F * 0.5f);
        set(c.g.grip - U * 0.06f + N * 0.03f, fing, cross(-N, fing), normalize(-U + N * 0.3f), ws, 0.15f);
    }
    return h;
}

// Buckling up (an arm layer over the driving pose; the right-hand version is mirrored): the left hand reaches back
// past the left shoulder for the belt's tongue, draws the belt across the chest and down to the buckle by the right
// hip, clicks it in at kBeltClick and goes back to the wheel.
static void clipBeltOn(const AuthorCtx& A, float t, Rig& r) {
    const float s = A.D.s;
    Rig base;
    drivePose(A, base, 0.f);
    const vec3 hip(0.f, 0.f, kSeatHipZ);
    Rig back = base, across = base, click = base;
    handAt(A, back.arm[0], 0, hip + vec3(-0.17f, -0.06f, 0.5f) * s, vec3(0.1f, -0.6f, 0.6f), vec3(0.6f, -0.4f, -0.2f), vec3(-1.f, -0.3f, -0.6f), 0.8f);
    back.spineYaw = 0.22f;
    back.headYaw = 0.55f;
    back.arm[0].clavUp = 0.06f;
    handAt(A, across.arm[0], 0, hip + vec3(0.02f, 0.2f, 0.3f) * s, vec3(0.7f, 0.2f, -0.5f), vec3(0.f, -0.6f, -0.8f), vec3(-1.f, -0.6f, -0.3f), 0.8f);
    across.spineYaw = -0.05f;
    across.headYaw = -0.1f;
    across.headPitch = 0.2f;
    handAt(A, click.arm[0], 0, hip + vec3(0.17f, 0.06f, 0.02f) * s, vec3(0.3f, 0.5f, -0.8f), vec3(0.f, -0.3f, -0.95f), vec3(-0.6f, -0.8f, -0.3f), 0.7f);
    click.spineYaw = -0.18f;
    click.headYaw = -0.35f;
    click.headPitch = 0.4f;
    std::vector<Key> k = {{0.f, base}, {0.3f, back}, {0.58f, across}, {kBeltClick, click}, {0.95f, click}, {1.25f, base}};
    sampleKeys(A, k, t, false, 1.25f, r);
}

