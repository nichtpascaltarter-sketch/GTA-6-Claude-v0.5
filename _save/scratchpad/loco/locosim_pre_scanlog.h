// locosim.h: the game-like pedestrian driver shared by locometer (metrics) and lmviz (montages): terrain, a person
// with its animator, pedai's turnTo / ai.cpp's faceTowards, GameWorld::movePed, animatePed's inputs and foot probes.
#pragma once
using namespace Anim;
using Anim::detail::qz;

static float wrapA(float a) {
    if (a >= -kPi && a <= kPi) return a;
    return remainderf(a, kTwoPi);
}
static const float kDt = 1.f / 60.f;

// ------------------------------------------------------------------------------------------------ terrain
static long gScans = 0;          // ground scans the animator asked for (the game's extra ground queries / 12)
static double gAnimSec = 0.0;   // time spent in Animator::update, and how many updates
static long gAnimN = 0;
struct Terrain {
    int kind = 0;   // 0 flat, 1 hill along +y (up, 2 m flat, down) of grade tan(a), 2 cross slope (rising to +x), 3 stairs,
                    // 4 a 15 cm curb: up at y = 3, down at y = 8
    float a = 0.f;
    static constexpr float kY0 = 2.f;
    float kRise = 0.16f, kTread = 0.3f;   // (stairs: per terrain - the game's metro stairs are 17 x 29 cm, 17 a flight)
    int kN = 12;
    float h(float x, float y) const {
        switch (kind) {
            case 1: {
                const float g = tanf(a), y0 = 2.f, L = 8.f;
                return g * (Clamp(y - y0, 0.f, L) - Clamp(y - (y0 + L + 2.f), 0.f, L));
            }
            case 2: return tanf(a) * x;
            case 3: {
                const float top = kY0 + kN * kTread, land = top + 2.f;
                if (y < kY0) return 0.f;
                if (y < top) return (floorf((y - kY0) / kTread) + 1.f) * kRise;
                if (y < land) return kN * kRise;
                if (y < land + kN * kTread) return ((float)kN - 1.f - floorf((y - land) / kTread)) * kRise;
                return 0.f;
            }
            case 4: return y >= 3.f && y < 8.f ? 0.15f : 0.f;
            default: return 0.f;
        }
    }
    vec3 n(float x, float y) const {
        if (kind == 0 || kind == 3 || kind == 4) return vec3(0, 0, 1);
        const float e = 0.05f;
        float dx = (h(x + e, y) - h(x - e, y)) / (2.f * e), dy = (h(x, y + e) - h(x, y - e)) / (2.f * e);
        return normalize(vec3(-dx, -dy, 1.f));
    }
};

// ------------------------------------------------------------------------------------------------ a pedestrian
struct Person {
    CharacterDesc d;
    Skeleton sk;
    Animator an;
    AnimInput in;
    vec3 pos = vec3(0);
    float yaw = 0.f, turnRate = 0.f;
    vec2 vel = vec2(0);
    bool turnRateKnown = true;   // false: turned like ai.cpp's faceTowards (yaw changes, turnRate not updated)
    mat4 m[B_COUNT];
    vec3 animPos = vec3(0);      // the position at the last animation update (the game's Ped::animPos)
    bool animPosSet = false;
    bool rootMoveOff = false;    // (to compare: the animator not told the root's own move)
};
struct Who {
    u32 seed;
    int role;
    float age;
    int gender;
};
static const Who kPeople[] = {{12u, 0, 0.3f, 0}, {45u, 0, 0.25f, 1}, {77u, 3, 0.4f, 0}, {91u, 0, 0.92f, 1}, {140u, 2, 0.1f, 0}, {166u, 5, 0.5f, 0}};
static const int kNP = 6;

static std::unique_ptr<Person> makePerson(const Who& w, u32 animSeed) {
    std::unique_ptr<Person> p(new Person());
    p->d = randomCharacter(w.seed, w.role);
    p->d.age = w.age;
    p->d.gender = w.gender ? FEMALE : MALE;
    buildSkeleton(p->d, p->sk);
    p->an.init(&p->sk, animSeed);
    p->an.setCharacter(p->d);
    return p;
}

// pedai's turnTo (or ai.cpp's faceTowards when turnRateKnown is false) and GameWorld::movePed on the terrain
static void control(Person& P, vec2 desired, const float* faceYaw, float rate, const Terrain& T) {
    const float dt = kDt;
    float tgt = P.yaw;
    bool turn = false;
    if (faceYaw) tgt = *faceYaw, turn = true;
    else if (length2(desired) > 0.04f) tgt = atan2f(-desired.x, desired.y), turn = true;
    if (turn) {
        float d = wrapA(tgt - P.yaw), step = Clamp(d, -rate * dt, rate * dt);
        P.yaw = wrapA(P.yaw + step);
        if (P.turnRateKnown) P.turnRate = step / dt;
    }
    float accel = length(desired) > length(P.vel) ? 11.f : 16.f;
    vec2 dv = desired - P.vel;
    float dl = length(dv), mx = accel * dt;
    if (dl > mx) dv = dv * (mx / dl);
    P.vel = P.vel + dv;
    P.pos.x += P.vel.x * dt;
    P.pos.y += P.vel.y * dt;
    float gz = T.h(P.pos.x, P.pos.y);
    if (gz > P.pos.z - 0.6f && gz < P.pos.z + 0.55f + 0.01f) {
        P.pos.z = gz > P.pos.z ? Lerp(P.pos.z, gz, Saturate(dt * 18.f)) + (gz - P.pos.z) * 0.35f : gz;
        if (fabsf(P.pos.z - gz) < 0.02f) P.pos.z = gz;
    } else {
        P.pos.z = gz;
    }
}

// GameWorld::animatePed's locomotion inputs and foot probes, then the update and the model-space bones
static void animate(Person& P, const Terrain& T) {
    AnimInput& in = P.in;
    float spd = length(P.vel);
    in.speed = spd;
    in.turnRate = P.turnRate;
    vec2 fwd(-sinf(P.yaw), cosf(P.yaw)), rightV(cosf(P.yaw), sinf(P.yaw));
    in.localMoveDir = spd > 0.1f ? normalize(vec2(dot(P.vel, rightV), dot(P.vel, fwd))) : vec2(0, 1);
    // the root's own move since the last update, pushes and all (GameWorld::animatePed)
    {
        vec3 D = P.pos - P.animPos;
        in.rootMove = vec3(dot(vec2(D.x, D.y), rightV), dot(vec2(D.x, D.y), fwd), D.z);
        in.rootMoveValid = P.animPosSet && !P.rootMoveOff && length(vec2(D.x, D.y)) < 1.f;
        P.animPos = P.pos;
        P.animPosSet = true;
    }
    quat q = qz(P.yaw);
    vec3 base = P.pos;
    vec3 pl = P.an.footProbe(0), pr = P.an.footProbe(1);
    vec3 fl = base + rotate(q, vec3(pl.x, pl.y, 0.f)), fr = base + rotate(q, vec3(pr.x, pr.y, 0.f));
    // (the game's clamp: 0.45 m since the stair gait - a foot two risers up from the root; LM_PROBE_CLAMP=0.3 as before)
    static const float kClamp = getenv("LM_PROBE_CLAMP") ? (float)atof(getenv("LM_PROBE_CLAMP")) : 0.45f;
    in.groundOffsetL = Clamp(T.h(fl.x, fl.y) - base.z, -kClamp, kClamp);
    in.groundOffsetR = Clamp(T.h(fr.x, fr.y) - base.z, -kClamp, kClamp);
    vec3 n = normalize(T.n(fl.x, fl.y) + T.n(fr.x, fr.y) + vec3(0, 0, 1e-3f));
    in.groundNormal = vec3(dot(vec2(n.x, n.y), rightV), dot(vec2(n.x, n.y), fwd), n.z);
    in.footProbes = true;
    // the ground scan the animator asked for (GameWorld::animatePed)
    in.groundScanValid = false;
    if (P.an.wantsGroundScan()) {
        vec3 s0 = P.an.groundScanFrom(), sd = P.an.groundScanDir();
        for (int k = 0; k < kGroundScan; k++) {
            vec3 mm = s0 + sd * (kGroundScanStep * k), ww = base + rotate(q, vec3(mm.x, mm.y, 0.f));
            in.groundScan[k] = Clamp(T.h(ww.x, ww.y) - base.z, -1.f, 1.f);
        }
        in.groundScanValid = true;
        gScans++;
    }
    {
        double t0 = TimeSeconds();
        P.an.update(in, kDt);
        gAnimSec += TimeSeconds() - t0;
        gAnimN++;
    }
    computeMatrices(P.sk, P.an.pose, P.m, nullptr);
}

static vec3 toWorld(const Person& P, vec3 mp) { return P.pos + rotate(qz(P.yaw), mp); }

// heel, ball and toe tip of a foot (anim_test's footPoints), world space
struct FootW {
    vec3 p[3];
    float gz[3];
    float yaw;
};
static void footWorld(const Person& P, const Terrain& T, int s, FootW& f) {
    const Skeleton& sk = P.sk;
    const mat4* m = P.m;
    int fb = s ? B_FOOT_R : B_FOOT_L;
    float ball = sk.bindLocalPos[s ? B_TOE_R : B_TOE_L].y, heel = ball * (0.21f / 0.52f), toe = ball * (0.79f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z +
                 sk.bindLocalPos[B_FOOT_L].z;
    vec3 hm = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
    vec3 bm = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
    int tb = s ? B_TOE_R : B_TOE_L;
    float toeZ = ankH + sk.bindLocalPos[B_TOE_L].z;
    vec3 tm = m[tb].c[3].xyz() + transformDir(m[tb], vec3(0.f, toe - ball, -toeZ));
    f.p[0] = toWorld(P, hm);
    f.p[1] = toWorld(P, bm);
    f.p[2] = toWorld(P, tm);
    for (int k = 0; k < 3; k++) f.gz[k] = T.h(f.p[k].x, f.p[k].y);
    vec3 dd = f.p[1] - f.p[0];
    f.yaw = atan2f(-dd.x, dd.y);
}

