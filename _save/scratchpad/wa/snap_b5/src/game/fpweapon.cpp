// First-person weapon handling. In first person a gun sits in front of the eyes: carried low on the right, raised so
// its sights (or optic) sit on the line of sight while aiming, tipped down while sprinting or reloading and pulled in
// against walls. It lags quick turns a little, bobs with the stride and kicks back on every shot. After the camera
// update the player's hands are IK'd onto its grips (the rest of the body keeps its own animation). Magnifying scopes
// switch to a scope sight picture on the HUD with the weapon hidden; reflex and red-dot optics add a red dot.

namespace Game {

namespace fpw_detail {

struct Grip {
    vec3 pos, axis, palm;   // fist centre, handle axis (thumb side), palm normal: weapon space (x right, y barrel, z up)
};
struct Spec {
    Grip right, left, foregrip;   // foregrip: the support hand with a WC_GRIP fitted
    vec3 sight;                   // on the line of sight when aiming: iron sights / optic centre (weapon space)
    float sightDist;              // eye to `sight` when aiming (m)
    vec3 hip;                     // weapon origin carried at the hip (camera space: x right, y forward, z up)
    vec3 muzzle;                  // bore exit (weapon space), before a suppressor
    float kickBack, kickPitch;    // recoil per shot: pushed back (m), muzzle up (rad)
    bool longGun;                 // stock in the shoulder, both hands on it while running
    vec3 feed;                    // where the support hand works during a reload (weapon space): magazine base, shell
                                  // loading port, open cylinder or muzzle
    int reloadKind;               // 0 magazine, 1 shells one at a time, 2 revolver cylinder, 3 rocket into the muzzle
};

// A pistol-style grip tilted back by `ang` whose top is at `top`: the fist sits 3.5 cm down the handle, thumb side up
// the handle, palm facing it from the right
Grip pistolGrip(vec3 top, float ang) {
    vec3 down(0.f, -sinf(ang), -cosf(ang));
    return {top + down * 0.035f, -down, vec3(-1.f, 0.12f, 0.f)};
}
// the support hand wrapped round the firing hand's fist from the left and below
Grip cupGrip(const Grip& r) { return {r.pos + vec3(-0.03f, 0.012f, -0.018f), r.axis, vec3(1.f, 0.1f, 0.3f)}; }
// palm up under a handguard / pump, thumb along the barrel
Grip underGrip(vec3 p) { return {p, vec3(0.f, 1.f, 0.f), vec3(0.25f, 0.f, 1.f)}; }
// a vertical foregrip (WC_GRIP) whose rail clamp is at (y, top)
Grip foreGrip(float y, float top) { return {vec3(0.f, y - 0.003f, top - 0.04f), vec3(0.f, 0.09f, 1.f), vec3(1.f, 0.f, 0.f)}; }

Spec specFor(WeaponType w) {
    Spec s;
    s.longGun = true;
    s.kickBack = 0.025f;
    s.kickPitch = 0.05f;
    s.hip = vec3(0.13f, 0.21f, -0.21f);
    s.reloadKind = 0;
    switch (w) {
        case WPN_PISTOL:
            s.right = pistolGrip(vec3(0.f, 0.f, 0.03f), 0.3f);
            s.left = s.foregrip = cupGrip(s.right);
            s.sight = vec3(0.f, 0.16f, 0.078f), s.sightDist = 0.58f;
            s.hip = vec3(0.14f, 0.4f, -0.19f);
            s.muzzle = vec3(0.f, 0.175f, 0.055f);
            s.kickBack = 0.03f, s.kickPitch = 0.12f, s.longGun = false;
            s.feed = vec3(0.f, -0.035f, -0.078f);   // magazine base under the grip
            break;
        case WPN_REVOLVER:
            s.right = pistolGrip(vec3(0.f, -0.01f, 0.02f), 0.45f);
            s.left = s.foregrip = cupGrip(s.right);
            s.sight = vec3(0.f, 0.212f, 0.0715f), s.sightDist = 0.6f;   // blade over the rear notch
            s.hip = vec3(0.14f, 0.4f, -0.19f);
            s.muzzle = vec3(0.f, 0.22f, 0.05f);
            s.kickBack = 0.04f, s.kickPitch = 0.2f, s.longGun = false;
            s.feed = vec3(-0.022f, 0.045f, 0.035f), s.reloadKind = 2;   // the cylinder, swung out to the left
            break;
        case WPN_SMG:
            s.right = pistolGrip(vec3(0.f, -0.02f, 0.02f), 0.25f);
            s.left = underGrip(vec3(0.f, 0.17f, 0.026f));
            s.foregrip = foreGrip(0.16f, 0.025f);
            s.sight = vec3(0.f, 0.12f, 0.094f), s.sightDist = 0.33f;   // rear sight ~18 cm from the eye
            s.muzzle = vec3(0.f, 0.27f, 0.055f);
            s.kickBack = 0.018f, s.kickPitch = 0.035f;
            s.feed = vec3(0.f, 0.076f, -0.105f);
            break;
        case WPN_RIFLE:
            s.right = pistolGrip(vec3(0.f, -0.02f, 0.02f), 0.3f);
            s.left = underGrip(vec3(0.f, 0.245f, 0.037f));   // rear of the handguard, close to the magazine well
            s.foregrip = foreGrip(0.3f, 0.029f);
            s.sight = vec3(0.f, 0.12f, 0.11f), s.sightDist = 0.17f;   // through the built-in red-dot tube (rear end 13 cm out)
            s.muzzle = vec3(0.f, 0.6f, 0.055f);
            s.feed = vec3(0.f, 0.118f, -0.1f);   // base of the curved magazine
            break;
        case WPN_SHOTGUN:
            s.right = {vec3(0.f, -0.085f, -0.01f), vec3(0.f, 0.6f, 0.8f), vec3(-1.f, 0.1f, 0.f)};   // wrist of the stock, low
            s.left = underGrip(vec3(0.f, 0.27f, 0.02f));   // rear half of the pump
            s.hip = vec3(0.13f, 0.16f, -0.2f);
            s.foregrip = foreGrip(0.3f, 0.017f);
            s.sight = vec3(0.f, 0.607f, 0.0752f), s.sightDist = 0.88f;   // brass bead at the muzzle
            s.muzzle = vec3(0.f, 0.62f, 0.06f);
            s.kickBack = 0.05f, s.kickPitch = 0.12f;
            s.feed = vec3(0.f, 0.075f, 0.002f), s.reloadKind = 1;   // loading port under the receiver
            break;
        case WPN_SNIPER:
            s.right = pistolGrip(vec3(0.f, -0.05f, 0.02f), 0.35f);
            s.left = s.foregrip = underGrip(vec3(0.f, 0.15f, 0.008f));
            s.sight = vec3(0.f, 0.1f, 0.115f), s.sightDist = 0.2f;
            s.hip = vec3(0.13f, 0.17f, -0.2f);
            s.muzzle = vec3(0.f, 0.78f, 0.06f);
            s.kickBack = 0.05f, s.kickPitch = 0.1f;
            s.feed = vec3(0.f, 0.035f, -0.05f);
            break;
        case WPN_RPG:
            s.right = pistolGrip(vec3(0.f, 0.f, 0.04f), 0.2f);
            s.left = s.foregrip = {vec3(0.f, 0.22f, 0.005f), vec3(0.f, 0.f, 1.f), vec3(1.f, 0.f, 0.f)};
            s.sight = vec3(-0.05f, 0.1f, 0.14f), s.sightDist = 0.32f;   // the sight post on the left of the tube
            s.hip = vec3(0.16f, 0.2f, -0.13f);                           // on the shoulder
            s.muzzle = vec3(0.f, 0.7f, 0.08f);
            s.kickBack = 0.06f, s.kickPitch = 0.08f;
            s.feed = vec3(0.f, 0.68f, 0.08f), s.reloadKind = 3;
            break;
        default:
            s.right = pistolGrip(vec3(0.f), 0.3f);
            s.left = s.foregrip = cupGrip(s.right);
            s.sight = vec3(0.f, 0.1f, 0.08f), s.sightDist = 0.4f;
            s.muzzle = vec3(0.f, 0.2f, 0.05f);
            s.longGun = false;
            s.feed = s.right.pos + vec3(0.f, 0.f, -0.06f);
            break;
    }
    return s;
}

// Support-hand choreography of a reload: waypoints on the reload's progress (0..1). Targets: 0 the hand's grip, 1 the
// feed point (magazine well, loading port, cylinder, muzzle), 2 the belt below the view (a spare magazine, shells, a
// speed loader or a rocket).
struct ReloadKey { float t; int target; };
const ReloadKey kReloadMag[] = {{0.f, 0}, {0.16f, 1}, {0.4f, 2}, {0.58f, 1}, {0.68f, 1}, {0.86f, 0}, {1.f, 0}};
const ReloadKey kReloadShells[] = {{0.f, 0}, {0.12f, 1}, {0.24f, 2}, {0.36f, 1}, {0.48f, 2}, {0.6f, 1}, {0.72f, 2}, {0.84f, 1}, {0.95f, 0}, {1.f, 0}};
const ReloadKey kReloadCylinder[] = {{0.f, 0}, {0.12f, 1}, {0.34f, 1}, {0.5f, 2}, {0.7f, 1}, {0.8f, 1}, {0.92f, 0}, {1.f, 0}};
const ReloadKey kReloadRocket[] = {{0.f, 0}, {0.18f, 2}, {0.48f, 1}, {0.62f, 1}, {0.85f, 0}, {1.f, 0}};

// Where along the choreography the hand is: the two targets and the eased blend between them
void reloadLeg(int kind, float ph, int& a, int& b, float& t) {
    const ReloadKey* k = kReloadMag;
    int n = (int)(sizeof(kReloadMag) / sizeof(kReloadMag[0]));
    if (kind == 1) k = kReloadShells, n = (int)(sizeof(kReloadShells) / sizeof(kReloadShells[0]));
    else if (kind == 2) k = kReloadCylinder, n = (int)(sizeof(kReloadCylinder) / sizeof(kReloadCylinder[0]));
    else if (kind == 3) k = kReloadRocket, n = (int)(sizeof(kReloadRocket) / sizeof(kReloadRocket[0]));
    a = b = k[n - 1].target;
    t = 0.f;
    for (int i = 0; i + 1 < n; i++)
        if (ph <= k[i + 1].t) {
            a = k[i].target;
            b = k[i + 1].target;
            float u = Saturate((ph - k[i].t) / Max(k[i + 1].t - k[i].t, 1e-3f));
            t = u * u * (3.f - 2.f * u);
            return;
        }
}

// camera-space axes -> rotation (columns = the weapon's x / y / z in camera space) from small yaw / pitch / roll
mat3 camRot(float yaw, float pitch, float roll) {
    quat q = quatAxisAngle(vec3(0, 0, 1), yaw) * quatAxisAngle(vec3(1, 0, 0), pitch) * quatAxisAngle(vec3(0, 1, 0), roll);
    return mat3FromQuat(q);
}

}  // namespace fpw_detail

bool GameWorld::fpWeaponUsable(const Ped& p) const {
    if (!rig.fpActive || p.state != PS_ONFOOT || p.ragdoll || p.weapon < WPN_PISTOL || p.weapon > WPN_RPG) return false;
    if (rig.scriptBlend > 0.f) return false;   // easing back from a cutscene shot: the view is not at the eyes yet
    if (p.phoneCall || p.phoneBrowse || p.meleeMove >= 0 || p.takedownT >= 0.f || p.moveMode != 0) return false;
    // climbing, vaulting and throws need the hands
    int a = p.anim.actionDone() ? -1 : p.anim.action;
    if (a == Anim::CLIP_CLIMB || a == Anim::CLIP_VAULT || a == Anim::CLIP_THROW || a == Anim::CLIP_GET_UP_FRONT || a == Anim::CLIP_GET_UP_BACK)
        return false;
    return true;
}

// Muzzle of the first-person weapon (world), for shots and tracers; false when the view is not holding one.
bool GameWorld::fpWeaponMuzzle(dvec3& out) const {
    if (!fpw.active || fpw.w < 0.5f || player < 0) return false;
    const Ped& p = peds[player];
    fpw_detail::Spec s = fpw_detail::specFor(p.weapon);
    vec3 m = s.muzzle;
    u8 comps = weaponComps(p, p.weapon);
    if (comps & WC_SUPPRESSOR) m.y += 0.15f;
    out = fpw.pos + dvec3(fpw.rot * m);
    return true;
}

void GameWorld::updateFirstPersonWeapon(float dt) {
    using namespace fpw_detail;
    if (player < 0 || player >= (int)peds.size() || !peds[player].used) return;
    Ped& p = peds[player];
    dt = Clamp(dt, 0.f, 0.1f);
    bool usable = fpWeaponUsable(p);
    // a change of weapon drops the hold for a moment (the new gun comes up from below)
    if (usable && fpw.weapon != p.weapon) {
        fpw.w = 0.f;
        fpw.weapon = p.weapon;
    }
    float k = 1.f - expf(-dt * 9.f);
    fpw.w += ((usable ? 1.f : 0.f) - fpw.w) * k;
    if (!rig.fpActive) fpw.w = 0.f;
    fpw.active = fpw.w > 0.01f && rig.fpActive && p.charIndex >= 0;
    fpw.scope = fpw.redDot = 0.f;
    fpw.hideWeapon = false;
    fpw.magInHand = false;
    if (!fpw.active) {
        fpw.ads = 0.f;
        fpw.lastYaw = rig.cam.yaw;
        fpw.lastPitch = rig.cam.pitch;
        return;
    }
    const WeaponType w = p.weapon;
    const Spec s = specFor(w);
    const WeaponInfo& wi = weaponInfo(w);
    u8 comps = weaponComps(p, w);
    bool magnified = w == WPN_SNIPER || ((comps & WC_SCOPE) && (w == WPN_RIFLE || w == WPN_REVOLVER));
    bool reflex = (w == WPN_SMG && (comps & WC_SCOPE)) || (w == WPN_RIFLE && !magnified);

    // ---- motion state
    float adsT = p.aiming ? 1.f : 0.f;
    fpw.ads += (adsT - fpw.ads) * (1.f - expf(-dt * 13.f));
    float ads = fpw.ads * fpw.ads * (3.f - 2.f * fpw.ads);
    float spd = length(vec2(p.vel.x, p.vel.y));
    bool sprinting = spd > 5.2f && !p.aiming && !p.firing;
    fpw.sprintW += ((sprinting ? 1.f : 0.f) - fpw.sprintW) * (1.f - expf(-dt * 8.f));
    bool reloading = p.reloadTimer > 0.f && wi.reloadTime > 0.f;
    fpw.reloadW += ((reloading ? 1.f : 0.f) - fpw.reloadW) * (1.f - expf(-dt * 10.f));
    float reloadPh = reloading ? Saturate(1.f - p.reloadTimer / wi.reloadTime) : 1.f;
    fpw.kick = Max(0.f, fpw.kick * expf(-dt * 14.f));
    // lag behind quick turns (a spring on the view's angular velocity)
    float yawVel = wrapAngle(rig.cam.yaw - fpw.lastYaw) / Max(dt, 1e-3f), pitchVel = (rig.cam.pitch - fpw.lastPitch) / Max(dt, 1e-3f);
    fpw.lastYaw = rig.cam.yaw;
    fpw.lastPitch = rig.cam.pitch;
    float swayK = 1.f - 0.75f * ads;
    vec2 swayT(Clamp(-yawVel * 0.018f, -0.07f, 0.07f) * swayK, Clamp(-pitchVel * 0.018f, -0.06f, 0.06f) * swayK);
    vec2 acc = (swayT - fpw.sway) * 160.f - fpw.swayVel * 2.f * sqrtf(160.f);
    fpw.swayVel += acc * dt;
    fpw.sway += fpw.swayVel * dt;
    // stride bob (the eye height itself is steadied by the camera)
    float ph = p.anim.phase * kTwoPi;
    float bobA = Saturate(spd / 4.f) * (1.f - 0.75f * ads) * (1.f + fpw.sprintW);
    vec3 bob(sinf(ph) * 0.007f * bobA, 0.f, -fabsf(sinf(ph)) * 0.009f * bobA + 0.0045f * bobA);
    // breathing drift while standing still
    float tb = (float)time;
    vec3 breathe(sinf(tb * 0.9f) * 0.0012f, 0.f, sinf(tb * 1.7f) * 0.0015f);

    // ---- weapon placement in camera space
    // hip carry: barrel converging on the crosshair ~12 m out, canted a touch
    vec3 hipPos = s.hip;
    mat3 hipRot = camRot(atan2f(s.hip.x, 12.f), atan2f(-s.hip.z, 12.f), s.longGun ? -0.08f : -0.05f);
    // aiming down the sights: `sight` on the view axis at sightDist
    vec3 adsPos = vec3(0.f, s.sightDist, 0.f) - s.sight;
    if (reflex && w == WPN_SMG) adsPos = vec3(0.f, 0.31f, 0.f) - vec3(0.f, 0.112f, 0.108f);   // through the reflex window
    if (magnified) adsPos = vec3(0.f, 0.26f, 0.f) - vec3(s.sight.x, 0.1f, w == WPN_SNIPER ? 0.115f : 0.13f);
    vec3 pos = lerp(hipPos, adsPos, ads);
    quat q = normalize(nlerp(quatFromMat3(hipRot), quat(), ads));
    // sprint: muzzle down and across the body
    if (fpw.sprintW > 0.001f) {
        float sw = fpw.sprintW;
        pos += vec3(-0.04f, -0.07f, -0.025f) * sw;
        q = normalize(quatAxisAngle(vec3(0, 0, 1), 0.45f * sw) * quatAxisAngle(vec3(1, 0, 0), (s.longGun ? -0.45f : -0.6f) * sw) *
                      quatAxisAngle(vec3(0, 1, 0), (s.longGun ? 0.35f : 0.1f) * sw) * q);
    }
    // reload: brought in front of the chest where the eyes can follow it, rolled so the feed faces the support hand
    // (a revolver the other way, its cylinder out to the left), muzzle a little up; seating a magazine jolts it up.
    // The RPG comes off the shoulder with the muzzle dipped towards the hand bringing the rocket.
    if (fpw.reloadW > 0.001f) {
        float rw = fpw.reloadW * fpw.reloadW * (3.f - 2.f * fpw.reloadW);
        vec3 rpos = s.longGun ? vec3(0.07f, 0.3f, -0.16f) : vec3(0.06f, 0.3f, -0.12f);
        float roll = 0.55f, pitch = 0.12f, yaw = s.longGun ? 0.25f : 0.1f;
        if (s.reloadKind == 2) roll = -0.75f;
        if (s.reloadKind == 3) rpos = vec3(0.16f, 0.16f, -0.18f), roll = 0.f, pitch = -0.45f, yaw = 0.35f;
        float sd = (reloadPh - 0.6f) / 0.035f;
        float seat = s.reloadKind == 0 ? expf(-sd * sd) : 0.f;
        pos = lerp(pos, rpos + vec3(0.f, 0.f, 0.014f * seat), rw);
        quat qr = quatAxisAngle(vec3(0, 0, 1), yaw) * quatAxisAngle(vec3(1, 0, 0), pitch + 0.08f * seat) * quatAxisAngle(vec3(0, 1, 0), roll);
        q = normalize(nlerp(q, qr, rw));
    }
    // against a wall: pull the gun in and tip the muzzle up
    {
        vec3 f = rig.cam.forward();
        WorldHit h;
        float reach = Max(s.sightDist, 0.45f) + s.muzzle.y * 0.6f;
        float block = 0.f;
        if (raycast(rig.cam.pos, f, reach + 0.1f, h, player, -1, false, true)) block = Saturate((reach - h.t) / 0.45f);
        fpw.block += (block - fpw.block) * (1.f - expf(-dt * 10.f));
        if (fpw.block > 0.001f) {
            pos += vec3(0.02f, -0.12f, -0.03f) * fpw.block;
            q = normalize(quatAxisAngle(vec3(1, 0, 0), 0.7f * fpw.block) * quatAxisAngle(vec3(0, 0, 1), 0.25f * fpw.block) * q);
        }
    }
    // recoil: straight back and muzzle up, settling quickly
    pos += vec3(0.f, -s.kickBack, s.kickBack * 0.25f) * fpw.kick;
    q = normalize(quatAxisAngle(vec3(1, 0, 0), s.kickPitch * fpw.kick) * q);
    // turn lag, bob, breathing
    pos += bob + breathe * (0.4f + 0.6f * ads);
    quat qs = quatAxisAngle(vec3(0, 0, 1), fpw.sway.x) * quatAxisAngle(vec3(1, 0, 0), fpw.sway.y);
    pos = rotate(qs, pos);
    q = normalize(qs * q);
    // coming up from below when drawn / switched to
    float up = 1.f - fpw.w;
    pos += vec3(0.02f, -0.05f, -0.22f) * up;
    q = normalize(quatAxisAngle(vec3(1, 0, 0), -0.8f * up) * q);

    // ---- to world: camera basis (x right, y forward, z up)
    vec3 F = rig.cam.forward(), R = rig.cam.right(), U = cross(R, F);   // (the first-person camera never rolls)
    mat3 camB(R, F, U);
    mat3 qm = mat3FromQuat(q);
    fpw.pos = rig.cam.pos + dvec3(camB * pos);
    fpw.rot = camB * qm;

    // sight pictures on the HUD: scopes take over near the end of the raise (the weapon is then hidden)
    if (magnified) {
        fpw.scope = Saturate((fpw.ads - 0.8f) / 0.2f) * fpw.w * (1.f - fpw.reloadW);
        fpw.scopeKind = w == WPN_SNIPER ? 1 : 0;
        fpw.hideWeapon = fpw.scope > 0.9f;
    } else if (reflex) {
        fpw.redDot = Saturate((fpw.ads - 0.85f) / 0.15f) * fpw.w * (1.f - fpw.reloadW) * (1.f - fpw.block);
    }

    // ---- hands onto the grips (model space of the ped)
    const CharEntry& ce = chars[p.charIndex];
    quat qyi = conj(quatAxisAngle(vec3(0, 0, 1), p.yaw));
    vec3 wpos = rotate(qyi, rel(fpw.pos, p.pos));
    mat3 wrot = mat3FromQuat(qyi) * fpw.rot;
    const Grip& gl = (comps & WC_GRIP) ? s.foregrip : s.left;
    auto toModel = [&](const Grip& g, vec3& pos, vec3& axis, vec3& palm) {
        pos = wpos + wrot * g.pos;
        axis = normalize(wrot * g.axis);
        palm = normalize(wrot * g.palm);
    };
    Anim::Pose pose = p.anim.pose;
    // the support hand swings free while running with a handgun
    float leftW = fpw.w * (s.longGun ? 1.f : 1.f - fpw.sprintW * (1.f - fpw.reloadW));
    vec3 shR = p.bones[Anim::B_UPPERARM_R].c[3].xyz(), shL = p.bones[Anim::B_UPPERARM_L].c[3].xyz();
    vec3 gp, ga, gpl;
    toModel(s.right, gp, ga, gpl);
    Anim::holdGrip(ce.skel, pose, true, gp, ga, gpl, (shR + gp) * 0.5f + vec3(0.3f, -0.05f, -0.3f), 0.9f, 0.55f, fpw.w);
    toModel(gl, gp, ga, gpl);
    float leftCurl = 0.85f;
    if (fpw.reloadW > 0.001f) {
        // reload choreography: grip -> feed -> the belt below the view -> feed -> ... -> grip (targets blended in the
        // ped's model space; the belt is fixed below the eyes, the others move with the gun)
        Grip feed = {s.feed, vec3(0.f, 1.f, 0.f), vec3(0.2f, 0.f, 1.f)};   // palm up under the magazine / port
        if (s.reloadKind == 2) feed = {s.feed, vec3(0.f, 1.f, 0.f), vec3(1.f, 0.f, 0.3f)};   // palm on the cylinder
        if (s.reloadKind == 3) feed = {s.feed, vec3(0.f, 0.f, 1.f), vec3(0.f, -1.f, 0.f)};   // the rocket pushed in
        vec3 fp, fa, fpl;
        toModel(feed, fp, fa, fpl);
        vec3 beltCam(-0.12f, 0.1f, -0.52f);
        vec3 bp = rotate(qyi, rel(rig.cam.pos + dvec3(camB * beltCam), p.pos));
        vec3 ba = normalize(rotate(qyi, camB * vec3(0.f, 0.3f, 1.f))), bpl = normalize(rotate(qyi, camB * vec3(1.f, 0.f, 0.f)));
        vec3 tp[3] = {gp, fp, bp}, ta[3] = {ga, fa, ba}, tpl[3] = {gpl, fpl, bpl};
        int a, b;
        float t;
        reloadLeg(s.reloadKind, reloadPh, a, b, t);
        float rw = fpw.reloadW;
        gp = lerp(gp, lerp(tp[a], tp[b], t), rw);
        ga = normalize(lerp(ga, normalize(lerp(ta[a], ta[b], t)), rw));
        gpl = normalize(lerp(gpl, normalize(lerp(tpl[a], tpl[b], t)), rw));
        leftCurl = Lerp(0.85f, 0.7f, rw);   // holding a magazine / shells rather than wrapped round a handguard
        // the magazine leaves with the hand (pulled at the first reach, a fresh one brought back and seated)
        if (s.reloadKind == 0 && rw > 0.5f && reloadPh > 0.16f && reloadPh < 0.58f) {
            fpw.magInHand = true;
            vec3 handW = rotate(conj(qyi), gp);                        // the fist, world (relative to the ped)
            vec3 feedW = rel(fpw.pos, p.pos) + fpw.rot * s.feed;       // the magazine base in the gun
            fpw.magOffset = handW - feedW;
        }
    }
    Anim::holdGrip(ce.skel, pose, false, gp, ga, gpl, (shL + gp) * 0.5f + vec3(-0.2f, -0.05f, -0.35f), leftCurl, 0.5f, leftW);
    Anim::computeMatrices(ce.skel, pose, p.bones, p.skin);
}

}  // namespace Game
