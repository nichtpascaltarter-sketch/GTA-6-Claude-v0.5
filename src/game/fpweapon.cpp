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
    switch (w) {
        case WPN_PISTOL:
            s.right = pistolGrip(vec3(0.f, 0.f, 0.03f), 0.3f);
            s.left = s.foregrip = cupGrip(s.right);
            s.sight = vec3(0.f, 0.16f, 0.078f), s.sightDist = 0.58f;
            s.hip = vec3(0.14f, 0.4f, -0.19f);
            s.muzzle = vec3(0.f, 0.175f, 0.055f);
            s.kickBack = 0.03f, s.kickPitch = 0.12f, s.longGun = false;
            break;
        case WPN_REVOLVER:
            s.right = pistolGrip(vec3(0.f, -0.01f, 0.02f), 0.45f);
            s.left = s.foregrip = cupGrip(s.right);
            s.sight = vec3(0.f, 0.212f, 0.0715f), s.sightDist = 0.6f;   // blade over the rear notch
            s.hip = vec3(0.14f, 0.4f, -0.19f);
            s.muzzle = vec3(0.f, 0.22f, 0.05f);
            s.kickBack = 0.04f, s.kickPitch = 0.2f, s.longGun = false;
            break;
        case WPN_SMG:
            s.right = pistolGrip(vec3(0.f, -0.02f, 0.02f), 0.25f);
            s.left = underGrip(vec3(0.f, 0.17f, 0.026f));
            s.foregrip = foreGrip(0.16f, 0.025f);
            s.sight = vec3(0.f, 0.12f, 0.094f), s.sightDist = 0.33f;   // rear sight ~18 cm from the eye
            s.muzzle = vec3(0.f, 0.27f, 0.055f);
            s.kickBack = 0.018f, s.kickPitch = 0.035f;
            break;
        case WPN_RIFLE:
            s.right = pistolGrip(vec3(0.f, -0.02f, 0.02f), 0.3f);
            s.left = underGrip(vec3(0.f, 0.245f, 0.037f));   // rear of the handguard, close to the magazine well
            s.foregrip = foreGrip(0.3f, 0.029f);
            s.sight = vec3(0.f, 0.12f, 0.11f), s.sightDist = 0.17f;   // through the built-in red-dot tube (rear end 13 cm out)
            s.muzzle = vec3(0.f, 0.6f, 0.055f);
            break;
        case WPN_SHOTGUN:
            s.right = {vec3(0.f, -0.085f, -0.01f), vec3(0.f, 0.6f, 0.8f), vec3(-1.f, 0.1f, 0.f)};   // wrist of the stock, low
            s.left = underGrip(vec3(0.f, 0.27f, 0.02f));   // rear half of the pump
            s.hip = vec3(0.13f, 0.16f, -0.2f);
            s.foregrip = foreGrip(0.3f, 0.017f);
            s.sight = vec3(0.f, 0.607f, 0.0752f), s.sightDist = 0.88f;   // brass bead at the muzzle
            s.muzzle = vec3(0.f, 0.62f, 0.06f);
            s.kickBack = 0.05f, s.kickPitch = 0.12f;
            break;
        case WPN_SNIPER:
            s.right = pistolGrip(vec3(0.f, -0.05f, 0.02f), 0.35f);
            s.left = s.foregrip = underGrip(vec3(0.f, 0.15f, 0.008f));
            s.sight = vec3(0.f, 0.1f, 0.115f), s.sightDist = 0.2f;
            s.hip = vec3(0.13f, 0.17f, -0.2f);
            s.muzzle = vec3(0.f, 0.78f, 0.06f);
            s.kickBack = 0.05f, s.kickPitch = 0.1f;
            break;
        case WPN_RPG:
            s.right = pistolGrip(vec3(0.f, 0.f, 0.04f), 0.2f);
            s.left = s.foregrip = {vec3(0.f, 0.22f, 0.005f), vec3(0.f, 0.f, 1.f), vec3(1.f, 0.f, 0.f)};
            s.sight = vec3(-0.05f, 0.1f, 0.14f), s.sightDist = 0.32f;   // the sight post on the left of the tube
            s.hip = vec3(0.16f, 0.2f, -0.13f);                           // on the shoulder
            s.muzzle = vec3(0.f, 0.7f, 0.08f);
            s.kickBack = 0.06f, s.kickPitch = 0.08f;
            break;
        default:
            s.right = pistolGrip(vec3(0.f), 0.3f);
            s.left = s.foregrip = cupGrip(s.right);
            s.sight = vec3(0.f, 0.1f, 0.08f), s.sightDist = 0.4f;
            s.muzzle = vec3(0.f, 0.2f, 0.05f);
            s.longGun = false;
            break;
    }
    return s;
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
    // reload: rolled over and tipped down in front of the chest (a quick dip at the start, back up at the end)
    if (fpw.reloadW > 0.001f) {
        float rw = fpw.reloadW;
        float dip = sinf(Saturate(reloadPh) * kPi);
        pos += vec3(-0.06f, -0.04f, -0.07f) * rw + vec3(0.f, 0.f, -0.02f) * dip * rw;
        q = normalize(quatAxisAngle(vec3(0, 1, 0), 0.65f * rw) * quatAxisAngle(vec3(1, 0, 0), (-0.25f - 0.1f * dip) * rw) * q);
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
    // the support hand lets go mid-reload (it fetches the magazine: the body's reload animation) and while running
    // with a handgun (it swings free)
    float leftW = fpw.w * (1.f - fpw.reloadW * sinf(Saturate((reloadPh - 0.12f) / 0.76f) * kPi)) * (s.longGun ? 1.f : 1.f - fpw.sprintW);
    vec3 shR = p.bones[Anim::B_UPPERARM_R].c[3].xyz(), shL = p.bones[Anim::B_UPPERARM_L].c[3].xyz();
    vec3 gp, ga, gpl;
    toModel(s.right, gp, ga, gpl);
    Anim::holdGrip(ce.skel, pose, true, gp, ga, gpl, (shR + gp) * 0.5f + vec3(0.3f, -0.05f, -0.3f), 0.9f, 0.55f, fpw.w);
    toModel(gl, gp, ga, gpl);
    Anim::holdGrip(ce.skel, pose, false, gp, ga, gpl, (shL + gp) * 0.5f + vec3(-0.2f, -0.05f, -0.35f), 0.85f, 0.5f, leftW);
    Anim::computeMatrices(ce.skel, pose, p.bones, p.skin);
}

}  // namespace Game
