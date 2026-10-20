// locometer: locomotion measurements through the real Animator, driven the way the game drives a pedestrian
// (GameWorld::movePed's 11 / 16 m/s^2, pedai's turnTo at 6 rad/s, animatePed's inputs and foot probes, pednav's
// social-force avoidance): planted-foot sliding per step (cm), foot pivoting per step (deg), starts, stops, turns on
// the spot and at walking speed, slopes, stairs, two people passing / overtaking, a standing crowd, look-at.
// Build: g++ -O2 -std=c++17 -I <tree> locometer.cpp -o lm      Run: lm [filter] > out.txt
// Output lines "M <key> <value>" (cmp.py tabulates two runs).
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
#include <cstring>
#include <functional>

#include "locosim.h"

static const char* gFilter = nullptr;
static bool want(const char* name) { return !gFilter || strstr(name, gFilter); }
// LM_TRACE=1: per-frame trace of the first run of each selected scenario (stderr)
static bool gTraceOn = false, gTraceNow = false, gSteps = false;
static int gTraceRun = 0, gRun = 0;
static void M(const std::string& k, float v) { printf("M %-44s %10.3f\n", k.c_str(), v); }

// ------------------------------------------------------------------------------------------------ contact tracking
struct Step {
    int foot, label;
    float t0, t1, slide, pivot, maxFrame;
};
struct Tracker {
    static constexpr float kDown = 0.008f;   // a sole point within 8 mm of the ground under it is on it
    static constexpr int kLabels = 32;
    bool in[2] = {false, false};
    FootW prev[2];
    float slide[2] = {0, 0}, pivot[2] = {0, 0}, t0[2] = {0, 0}, maxF[2] = {0, 0};
    int label0[2] = {0, 0}, frames[2] = {0, 0};
    std::vector<Step> steps;
    float winSlide[kLabels] = {}, winPivot[kLabels] = {}, winTime[kLabels] = {};
    float lowest[kLabels], floatMax[kLabels];
    int floatFrames[kLabels] = {}, scuffs[kLabels] = {};
    Tracker() {
        for (int i = 0; i < kLabels; i++) lowest[i] = 1e9f, floatMax[i] = 0.f;
    }
    void add(const Person& P, const Terrain& T, float t, int label) {
        if (label <= 0) {
            // warm-up: forget open contacts so that they do not count
            for (int s = 0; s < 2; s++) {
                footWorld(P, T, s, prev[s]);
                in[s] = false;
            }
            return;
        }
        winTime[label] += kDt;
        for (int s = 0; s < 2; s++) {
            FootW f;
            footWorld(P, T, s, f);
            bool down[3];
            for (int k = 0; k < 3; k++) down[k] = f.p[k].z - f.gz[k] < kDown;
            const float low = soleClear(f, T);
            lowest[label] = Min(lowest[label], low);
            // a foot the animator holds planted but shown off the ground
            if (P.an.planted[s] && low > 0.015f) {
                floatFrames[label]++;
                floatMax[label] = Max(floatMax[label], low);
#ifdef LM_LAND
                static const float kFloatMin = getenv("LM_FLOATMIN") ? (float)atof(getenv("LM_FLOATMIN")) : 0.2f;
                if (low > kFloatMin && getenv("LM_FLOATLOG"))
                    fprintf(stderr, "FLOAT run %d t%.3f foot %d label %d low %.3f h %.3f b %.3f t %.3f g %.2f %.2f %.2f kind %d age %.3f fz %.3f ga %.3f st %.2f | pc %.3f cy %.2f piv %.2f sink %.3f tr %.2f\n", gRun - 1, t, s, label, low,
                            f.p[0].z - f.gz[0], f.p[1].z - f.gz[1], f.p[2].z - f.gz[2], f.gz[0] - P.pos.z, f.gz[1] - P.pos.z, f.gz[2] - P.pos.z,
                            P.an.dbgPlantKind[s], P.an.plantAge[s], s ? P.an.footR : P.an.footL, P.an.groundAhead[s], P.an.stepT[s],
                            length(P.an.plantCorr[s]), P.an.corrYaw[s], P.an.pivotW[s], P.an.legSink, P.turnRate);
#endif
            }
            bool contact = down[0] || down[1] || down[2];
            if (gTraceNow) {
                float best = -1.f;
                if (contact && in[s]) {
                    best = 1e9f;
                    for (int k = 0; k < 3; k++)
                        if (down[k]) best = Min(best, length(vec2(f.p[k].x - prev[s].p[k].x, f.p[k].y - prev[s].p[k].y)));
                }
#ifndef LM_HEAD
                fprintf(stderr, "%s%d %d%d%d pl%d st%.2f h%5.1f b%5.1f t%5.1f sl%5.2f fy%6.1f SL%6.2f fz%6.2f ga%6.2f g%6.2f,%6.2f sh%5.2f sd%d bp%d hw%.3f,%.3f dr%6.3f pr%6.3f | ", s ? "" : "", s,
                        down[0], down[1], down[2], (int)P.an.planted[s], P.an.stepT[s], (f.p[0].z - f.gz[0]) * 100.f, (f.p[1].z - f.gz[1]) * 100.f,
                        (f.p[2].z - f.gz[2]) * 100.f, best * 100.f, f.yaw * 57.3f, P.an.stairLand[s], s ? P.an.footR : P.an.footL, P.an.groundAhead[s],
                        f.gz[0] - P.pos.z, f.gz[2] - P.pos.z, P.an.stairShift[s], (int)P.an.scanDone[s], (int)P.an.ballProbe[s], f.p[0].x, f.p[0].y, P.an.dbgDrift[s], P.an.dbgPred[s]);
#else
                fprintf(stderr, "%s%d %d%d%d pl%d st%.2f h%5.1f b%5.1f t%5.1f sl%5.2f fy%6.1f SL%6.2f fz%6.2f ga%6.2f g%6.2f,%6.2f sh%5.2f sd%d bp%d hw%.3f,%.3f dr%6.3f pr%6.3f | ", s ? "" : "", s,
                        down[0], down[1], down[2], (int)P.an.planted[s], P.an.stepT[s], (f.p[0].z - f.gz[0]) * 100.f, (f.p[1].z - f.gz[1]) * 100.f,
                        (f.p[2].z - f.gz[2]) * 100.f, best * 100.f, f.yaw * 57.3f, -9.f, s ? P.an.footR : P.an.footL, P.an.groundAhead[s],
                        f.gz[0] - P.pos.z, f.gz[2] - P.pos.z, 0.f, 0, 0, f.p[0].x, f.p[0].y, 0.f, 0.f);
#endif
#ifndef LM_HEAD
                {
                    // (pz: the sole's pin, cm; pe: how far the shown heel is from its footprint, cm - the leg short of it -
                    // for a planted or held foot; hd: held)
                    const vec3 hw = toWorld(P, vec3(P.an.plantP[s].x, P.an.plantP[s].y, 0.f));
                    fprintf(stderr, "pc%.3f cy%.2f k%d pz%5.1f pe%5.1f hd%d sf%.2f/%.2f/%d%d | ", length(P.an.plantCorr[s]), P.an.corrYaw[s], P.an.dbgPlantKind[s], P.an.pinZ[s] * 100.f,
                            (P.an.planted[s] || P.an.footHold[s]) ? length(vec2(f.p[0].x - hw.x, f.p[0].y - hw.y)) * 100.f : -1.f, (int)P.an.footHold[s],
                            P.an.stepFitLand[s], P.an.stepFitShift[s], (int)P.an.stepScanAsked[s], (int)P.an.stepScanHave[s]);
                }
#else
                fprintf(stderr, "pc%.3f cy%.2f k0 | ", length(P.an.plantCorr[s]), P.an.corrYaw[s]);
#endif
#ifdef ANIM_DBG_HOOK
                if (s == 1) fprintf(stderr, "f%d ", P.an.dbgFrame);
#endif
                if (s == 1)
                    fprintf(stderr, "t%.3f L%d yaw%6.1f tr%5.2f v%4.2f md%5.2f,%5.2f ph%.3f mw%.2f lag%5.2f ht%5.2f sk%.3f\n", t, label, P.yaw * 57.3f, P.turnRate,
                            P.in.speed, P.in.localMoveDir.x, P.in.localMoveDir.y, P.an.phase, P.an.moveW, P.an.bodyLag, P.an.hipTurn, P.an.legSink);
            }
            if (contact) {
                if (in[s]) {
                    float best = 1e9f;
                    for (int k = 0; k < 3; k++)
                        if (down[k]) best = Min(best, length(vec2(f.p[k].x - prev[s].p[k].x, f.p[k].y - prev[s].p[k].y)));
                    float dy = fabsf(wrapA(f.yaw - prev[s].yaw));
                    slide[s] += best;
                    pivot[s] += dy;
                    maxF[s] = Max(maxF[s], best);
                    frames[s]++;
                    winSlide[label] += best;
                    winPivot[label] += dy;
                } else {
                    in[s] = true;
                    t0[s] = t;
                    slide[s] = pivot[s] = maxF[s] = 0.f;
                    frames[s] = 0;
                    label0[s] = label;
                }
            } else if (in[s]) {
                close(s, t);
            }
            prev[s] = f;
        }
    }
    void close(int s, float t) {
        in[s] = false;
        if (frames[s] < 4) {
            scuffs[label0[s]]++;
            return;
        }
        Step st;
        st.foot = s;
        st.label = label0[s];
        st.t0 = t0[s];
        st.t1 = t;
        st.slide = slide[s];
        st.pivot = pivot[s];
        st.maxFrame = maxF[s];
        steps.push_back(st);
    }
    void finish(float t) {
        for (int s = 0; s < 2; s++)
            if (in[s]) close(s, t);
    }
};

struct Agg {
    std::vector<float> slide, pivot;
    float winSlide = 0.f, winPivot = 0.f, winTime = 0.f, lowest = 1e9f, floatMax = 0.f;
    int floatFrames = 0, scuffs = 0, frames = 0;
    void addTracker(const Tracker& tr, int label) {
        for (const Step& s : tr.steps)
            if (s.label == label) {
                if (gSteps) printf("  step run %d foot %d t %.3f-%.3f slide %.2f cm pivot %.1f deg maxframe %.2f\n", gRun - 1, s.foot, s.t0, s.t1, s.slide * 100.f, s.pivot * 57.3f, s.maxFrame * 100.f);
                slide.push_back(s.slide * 100.f);
                pivot.push_back(s.pivot * 57.2958f);
            }
        winSlide += tr.winSlide[label] * 100.f;
        winPivot += tr.winPivot[label] * 57.2958f;
        winTime += tr.winTime[label];
        lowest = Min(lowest, tr.lowest[label]);
        floatMax = Max(floatMax, tr.floatMax[label]);
        floatFrames += tr.floatFrames[label];
        scuffs += tr.scuffs[label];
    }
    static float mean(const std::vector<float>& v) {
        double s = 0;
        for (float x : v) s += x;
        return v.empty() ? 0.f : (float)(s / v.size());
    }
    static float pct(std::vector<float> v, float q) {
        if (v.empty()) return 0.f;
        std::sort(v.begin(), v.end());
        return v[Min((size_t)(v.size() * q), v.size() - 1)];
    }
    void report(const std::string& k, bool perStep = true) const {
        if (perStep) {
            M(k + ".steps", (float)slide.size());
            M(k + ".slide_cm_step_mean", mean(slide));
            M(k + ".slide_cm_step_p90", pct(slide, 0.9f));
            M(k + ".slide_cm_step_max", pct(slide, 1.f));
            M(k + ".pivot_deg_step_mean", mean(pivot));
            M(k + ".pivot_deg_step_max", pct(pivot, 1.f));
        }
        M(k + ".slide_cm_total_per_person", winSlide / Max(1.f, (float)nPeople));
        M(k + ".pivot_deg_total_per_person", winPivot / Max(1.f, (float)nPeople));
        M(k + ".lowest_sole_mm", (lowest < 1e8f ? lowest : 0.f) * 1000.f);
        M(k + ".planted_float_frames", (float)floatFrames);
        M(k + ".planted_float_max_mm", floatMax * 1000.f);
        M(k + ".scuffs", (float)scuffs);
    }
    int nPeople = 0;
};

// ------------------------------------------------------------------------------------------------ scenarios
// A scenario drives one person: desired velocity (world), optional facing, turn rate; the label of the frame (0 =
// warm-up, not measured).
struct Ctl {
    vec2 desired = vec2(0);
    bool face = false;
    float faceYaw = 0.f, rate = 6.f, hold = 1.f;
    float blockV = -1.f;   // (held: the speed of the one ahead along the walk; LM_SEPVEL: the game's separation takes half the
                           // closing speed off the velocity - GameWorld::updatePeds after the change)
    int label = 0;
};
using Script = std::function<void(float t, const Person& P, Ctl& c)>;

static void runOne(const Who& w, u32 animSeed, const Terrain& T, float secs, const Script& sc, Tracker& tr,
                   const std::function<void(float, Person&)>& extra = nullptr, float yaw0 = 0.f, bool knownRate = true,
                   vec2 start = vec2(0.f)) {
    std::unique_ptr<Person> P = makePerson(w, animSeed);
    gTraceNow = gTraceOn && gRun == gTraceRun;
    gScanLog = gTraceNow;
#ifdef ANIM_DBG_HOOK
    Anim::gAnimDbg = gTraceNow;   // (a debug tree's own prints, for the traced run)
#endif
    gRun++;
    P->yaw = yaw0;
    P->turnRateKnown = knownRate;
    P->pos.x = start.x;
    P->pos.y = start.y;
    P->pos.z = T.h(start.x, start.y);
    const int n = (int)(secs / kDt);
    for (int f = 0; f < n; f++) {
        float t = f * kDt;
        Ctl c;
        sc(t, *P, c);
        control(*P, c.desired, c.face ? &c.faceYaw : nullptr, c.rate, T, c.hold, c.blockV);
        animate(*P, T);
        tr.add(*P, T, t, c.label);
        if (extra) extra(t, *P);
        // (the separation runs after the ped's update: its velocity change shows in the next update)
        static const bool kSepVel = getenv("LM_SEPVEL") != nullptr;
        if (kSepVel && c.blockV >= 0.f && length2(c.desired) > 1e-4f) {
            const vec2 dir = normalize(c.desired);
            const float vc = dot(P->vel, dir) - c.blockV;
            if (vc > 0.f) P->vel = P->vel - dir * (vc * 0.5f);
        }
    }
    tr.finish(secs);
    gTraceNow = false;
#ifdef ANIM_DBG_HOOK
    Anim::gAnimDbg = false;
#endif
}

static vec2 dirOf(float yaw) { return vec2(-sinf(yaw), cosf(yaw)); }

static void steady() {
    const float speeds[] = {0.9f, 1.4f, 2.0f, 3.0f, 5.0f, 7.0f, 0.5f, 0.7f};
    const char* names[] = {"walk_slow_0.9", "walk_1.4", "walk_fast_2.0", "jog_3.0", "run_5.0", "sprint_7.0", "walk_stroll_0.5", "walk_stroll_0.7"};
    for (int i = 0; i < 8; i++) {
        std::string k = std::string("flat.") + names[i];
        if (!want(k.c_str())) continue;
        Agg ag;
        // (how far a swinging foot is shown from the pose's - a toe-off hold kept on while the pose swings on leaves it
        // behind, to catch up later in the swing)
        float swingCorrMax = 0.f, swingCorrSum = 0.f, sideHands = 0.f, sideFeet = 0.f;
        int swingN = 0;
        for (int p = 0; p < kNP; p++) {
            Tracker tr;
            Terrain T;
            float v = speeds[i];
            runOne(kPeople[p], kPeople[p].seed * 7u + 1u, T, 8.f, [&](float t, const Person&, Ctl& c) {
                c.desired = vec2(0.f, v);
                c.label = t > 2.5f ? 1 : 0;
            }, tr, [&](float t, Person& P) {
                if (t < 2.5f) return;
                // (how far out to the side the hands, elbows and feet go: the body's width walking - for doors)
                for (int s = 0; s < 2; s++) {
                    const int hb[4] = {s ? B_HAND_R : B_HAND_L, s ? B_FOREARM_R : B_FOREARM_L, s ? B_MIDDLE3_R : B_MIDDLE3_L, s ? B_THUMB3_R : B_THUMB3_L};
                    for (int k = 0; k < 4; k++) sideHands = Max(sideHands, fabsf(P.m[hb[k]].c[3].x));
                    FootW f;
                    footWorld(P, T, s, f);
                    for (int k = 0; k < 3; k++) sideFeet = Max(sideFeet, fabsf(dot(vec2(f.p[k].x - P.pos.x, f.p[k].y - P.pos.y), vec2(cosf(P.yaw), sinf(P.yaw)))));
                }
                for (int s = 0; s < 2; s++)
                    if (!P.an.planted[s] && !P.an.footHold[s] && P.an.stepT[s] < 0.f) {
                        const float c = length(P.an.plantCorr[s]);
                        swingCorrMax = Max(swingCorrMax, c);
                        swingCorrSum += c;
                        swingN++;
                    }
            });
            ag.addTracker(tr, 1);
            ag.nPeople++;
        }
        ag.report(k);
        M(k + ".swing_corr_mean_cm", swingN ? 100.f * swingCorrSum / swingN : 0.f);
        M(k + ".swing_corr_max_cm", 100.f * swingCorrMax);
        M(k + ".half_width_hands_cm", 100.f * sideHands);
        M(k + ".half_width_feet_cm", 100.f * sideFeet);
    }
}

// anim_test's gait runs (runGait, and the limp at 1 m/s on a hurt right leg): the walk at a steady pace on flat
// ground, the animator not told the root's own move
static void agait() {
    const float speeds[] = {0.9f, 1.2f, 1.4f, 1.7f, 2.4f, 1.0f};
    const char* names[] = {"agait.0.9", "agait.1.2", "agait.1.4", "agait.1.7", "agait.2.4", "agait.limp_1.0"};
    for (int i = 0; i < 6; i++) {
        if (!want(names[i])) continue;
        Agg ag;
        for (int p = 0; p < kNP; p++) {
            Tracker tr;
            Terrain T;
            const float v = speeds[i];
            const bool limp = i == 5;
            runOne(kPeople[p], kPeople[p].seed * 7u + 5u, T, 10.f, [&](float t, const Person&, Ctl& c) {
                c.desired = vec2(0.f, v);
                c.label = t > 4.f ? 15 : 0;
            }, tr, [&](float, Person& P) {
                P.rootMoveOff = true;
                if (limp) P.in.legHurt[1] = 1.f;
            });
            ag.addTracker(tr, 15);
            ag.nPeople++;
        }
        ag.report(names[i]);
    }
}

static void slopes() {
    // hill: 10 degrees (17.6 %), up 8 m, 2 m level, down 8 m; labels by where the ped is
    const float speeds[] = {1.4f, 3.0f, 5.0f};
    const char* names[] = {"walk_1.4", "jog_3.0", "run_5.0"};
    for (int i = 0; i < 3; i++) {
        std::string k = std::string("hill10.") + names[i];
        if (!want(k.c_str())) continue;
        Agg up, down;
        for (int p = 0; p < kNP; p++) {
            Tracker tr;
            Terrain T;
            T.kind = 1;
            T.a = 10.f * kPi / 180.f;
            float v = speeds[i];
            float secs = 21.f / v + 1.5f;
            runOne(kPeople[p], kPeople[p].seed * 7u + 1u, T, secs, [&](float t, const Person& P, Ctl& c) {
                c.desired = vec2(0.f, Min(v, t * 11.f));
                float y = P.pos.y;
                c.label = (y > 3.f && y < 9.5f) ? 2 : ((y > 13.f && y < 19.5f) ? 3 : 0);
            }, tr);
            up.addTracker(tr, 2);
            down.addTracker(tr, 3);
            up.nPeople++;
            down.nPeople++;
        }
        up.report(k + ".up");
        down.report(k + ".down");
    }
    {
        std::string k = "cross10.walk_1.4";
        if (want(k.c_str())) {
            Agg ag;
            for (int p = 0; p < kNP; p++) {
                Tracker tr;
                Terrain T;
                T.kind = 2;
                T.a = 10.f * kPi / 180.f;
                runOne(kPeople[p], kPeople[p].seed * 7u + 1u, T, 8.f, [&](float t, const Person&, Ctl& c) {
                    c.desired = vec2(0.f, 1.4f);
                    c.label = t > 2.5f ? 4 : 0;
                }, tr);
                ag.addTracker(tr, 4);
                ag.nPeople++;
            }
            ag.report(k);
        }
    }
    {
        // a 15 cm curb: up onto it, 5 m along, down off it (a sidewalk crossing)
        std::string k = "curb15.walk_1.4";
        if (want(k.c_str())) {
            Agg up, down;
            for (int p = 0; p < kNP; p++) {
                Tracker tr;
                Terrain T;
                T.kind = 4;
                runOne(kPeople[p], kPeople[p].seed * 7u + 1u, T, 8.5f, [&](float t, const Person& P, Ctl& c) {
                    c.desired = vec2(0.f, Min(1.4f, t * 11.f));
                    float y = P.pos.y;
                    c.label = (y > 2.2f && y < 4.f) ? 14 : ((y > 7.2f && y < 9.f) ? 15 : 0);
                }, tr);
                up.addTracker(tr, 14);
                down.addTracker(tr, 15);
                up.nPeople++;
                down.nPeople++;
            }
            up.report(k + ".up");
            down.report(k + ".down");
        }
    }
    {
        // stairs: 12 steps of 16 x 30 cm up, a 2 m landing, 12 down; and a flight like the game's metro stairs (17 steps of
        // 17 x 29 cm, each person at its own pace 1.2 .. 1.45 m/s). Besides the usual: how the feet that are down stand
        // on the treads - across a riser (heel and toe tip over treads at different heights), sunk into one (a sole point
        // more than 10 mm under the tread over it), or lifted off (all three over 15 mm above) - in foot-frames
        struct SC {
            const char* name;
            float rise, tread;
            int n;
            int paced;   // 0: 1.3 m/s; 1: each at its own pace; 2: that, braking and weaving as in a crowd
        };
        // (stairs17.crowd: every 2.2 s a brake to 55 % of the pace for about 0.6 s, as behind someone slower, and a weave
        // of +-0.3 m/s across, as round someone - the landing then differs from the swing's plan. stairs17.blocked: every
        // 2.6 s held back to a fifth of the pace for 0.5 s, the velocity - and so the gait - unchanged, as the game's ped-ped
        // separation holds a walker behind a slower one)
        static const SC kSC[5] = {{"stairs.walk_1.3", 0.16f, 0.3f, 12, 0}, {"stairs17.walk", 0.17f, 0.29f, 17, 1},
                                  {"stairs17.crowd", 0.17f, 0.29f, 17, 2}, {"stairs17.blocked", 0.17f, 0.29f, 17, 3},
                                  {"stairs17.stops", 0.17f, 0.29f, 17, 4}};
        for (const SC& sc : kSC) {
        std::string k = sc.name;
        if (want(k.c_str())) {
            Agg up, down;
            int across[2] = {0, 0}, sunk[2] = {0, 0}, sunk3[2] = {0, 0}, lifted[2] = {0, 0}, downN[2] = {0, 0};
            // (the toe tip's roll forward over a stance, from where it was planted; and where the sunk frames fall: with
            // the heel up - the roll over the ball - or not)
            std::vector<float> roll[2];
            int sunkHeelUp[2] = {0, 0};
            // (quick steps - a planted foot dragged too far stepping over in a low arc - on the flights, and the planted
            // frames below one riser: deeper than a heel in the riser behind)
            int quick[2] = {0, 0}, deep[2] = {0, 0};
            // (plants on the flights by kind - LM_KINDS: with a plan, without, held re-plant, quick step - and the deepest
            // sole over each kind's stance)
            int kinds[2][6] = {}, kindDeep[2][6] = {};
#ifdef LM_LAND
            // (where the landing heels come down against their plans, along the scan: gait plants and held re-plants; the
            // tread shift still to go when the spot was fixed; the phase from the strike then)
            std::vector<float> landErr[2][2], landRes[2], landPh[2];
            int landNoPlan[2] = {0, 0};
#endif
            // (the crowd case four times over, its brakes and weaves at other moments: more landings to count)
            for (int pp = 0; pp < kNP * (sc.paced >= 2 ? 4 : 1); pp++) {
                // (LM_SEEDOFF=n: other brake / hold moments and animation seeds - the spread of the results)
                static const int kSeedOff = getenv("LM_SEEDOFF") ? atoi(getenv("LM_SEEDOFF")) : 0;
                const int p = pp % kNP, rep = pp / kNP + 4 * kSeedOff;
                Tracker tr;
                Terrain T;
                T.kind = 3;
                T.kRise = sc.rise;
                T.kTread = sc.tread;
                T.kN = sc.n;
                int lab = 0;
                bool was[2] = {false, false}, stepping[2] = {false, false};
                int curKind[2] = {0, 0};
                float curLow[2] = {0.f, 0.f};
                int curLab[2] = {0, 0};
                float toe0[2] = {0.f, 0.f}, toeMax[2] = {0.f, 0.f};
                int labAt[2] = {0, 0};
#ifdef LM_LAND
                int seqSeen[2] = {0, 0};
                float lastErr[2] = {99.f, 99.f}, lastShift[2] = {0.f, 0.f};
#endif
                // (LM_SV=<m/s>: the stairs at that pace instead - the AI's stair pace - each person a little apart)
                static const float kSV = getenv("LM_SV") ? (float)atof(getenv("LM_SV")) : -1.f;
                const float v = kSV > 0.f ? kSV + 0.02f * (float)p : (sc.paced ? 1.2f + 0.05f * (float)p : 1.3f);
                runOne(kPeople[p], kPeople[p].seed * 7u + 1u + (u32)rep, T, 4.f + (2.f * sc.n * sc.tread + 2.f) / v * (sc.paced >= 4 ? 1.9f : (sc.paced >= 2 ? 1.3f : 1.f)), [&](float t, const Person& P, Ctl& c) {
                    c.desired = vec2(0.f, Min(v, t * 11.f));
                    if (sc.paced == 2 && t > 2.f) {
                        const float u = fmodf(t + 0.37f * (float)p + 0.55f * (float)rep, 2.2f);
                        auto ss = [](float a, float b, float x) { float q = Saturate((x - a) / (b - a)); return q * q * (3.f - 2.f * q); };
                        const float brake = ss(0.f, 0.15f, u) * (1.f - ss(0.6f, 0.75f, u));
                        c.desired = vec2(0.3f * sinf(t * 2.33f + (float)p + 1.7f * (float)rep), v * (1.f - 0.45f * brake));
                    }
                    if (sc.paced == 4 && t > 2.f) {
                        const float u = fmodf(t + 0.53f * (float)p + 0.85f * (float)rep, 4.5f);
                        if (u < 2.f) c.desired = vec2(0.f, 0.f);   // (stopped on the flight, as behind someone standing)
                    }
                    if (sc.paced == 3 && t > 2.f) {
                        const float u = fmodf(t + 0.43f * (float)p + 0.65f * (float)rep, 2.6f);
                        c.blockV = u < 0.5f ? 0.2f * v : -1.f;   // (held behind someone at a fifth of the pace)
                    }
                    float y = P.pos.y;
                    const float top = Terrain::kY0 + T.kN * T.kTread, land = top + 2.f;
                    c.label = (y > Terrain::kY0 + 0.3f && y < top - 0.2f) ? 5 : ((y > land + 0.3f && y < land + T.kN * T.kTread - 0.2f) ? 6 : 0);
                    lab = c.label;
                }, tr, [&](float t, Person& P) {
                    for (int s = 0; s < 2; s++) {
                        const bool pl = P.an.planted[s];
                        FootW f;
                        footWorld(P, T, s, f);
                        if (pl && !was[s]) toe0[s] = toeMax[s] = f.p[2].y, labAt[s] = lab;
                        if (pl) toeMax[s] = Max(toeMax[s], f.p[2].y);
                        if (!pl && was[s] && (labAt[s] == 5 || labAt[s] == 6)) roll[labAt[s] == 5 ? 0 : 1].push_back((toeMax[s] - toe0[s]) * 100.f);
                        was[s] = pl;
                    }
#ifndef LM_HEAD
                    for (int s = 0; s < 2; s++) {
                        // (a stance by its plant kind: counted once it ends, deep if its sole went below a riser)
                        const bool pl = P.an.planted[s];
                        if (pl && curKind[s] == 0) {
                            curKind[s] = Clamp(P.an.dbgPlantKind[s], 0, 5);
                            curLow[s] = 1e9f;
                            curLab[s] = lab;
                        }
                        if (pl) {
                            FootW f;
                            footWorld(P, T, s, f);
                            for (int q = 0; q < 3; q++) curLow[s] = Min(curLow[s], f.p[q].z - f.gz[q]);
                        }
                        if (!pl && curKind[s] != 0) {
                            if (curLab[s] == 5 || curLab[s] == 6) {
                                const int w = curLab[s] == 5 ? 0 : 1;
                                kinds[w][curKind[s]]++;
                                if (curLow[s] < -sc.rise - 0.01f) kindDeep[w][curKind[s]]++;
                            }
                            curKind[s] = 0;
                        }
                    }
#endif
#ifdef LM_LAND
                    for (int s = 0; s < 2; s++) {
                        if (P.an.dbgLandSeq[s] == seqSeen[s]) continue;
                        seqSeen[s] = P.an.dbgLandSeq[s];
                        if (lab != 5 && lab != 6) continue;
                        const int w = lab == 5 ? 0 : 1;
                        lastErr[s] = 99.f;
                        lastShift[s] = P.an.dbgLandShift[s] * 100.f;
                        if (!P.an.dbgLandPlan[s] || !P.scanHave[s]) {
                            landNoPlan[w]++;
                            continue;
                        }
                        const float Lf = P.an.footHeel + P.an.footBall;
                        const vec3 plan = P.scanW0[s] + P.scanWD[s] * (0.25f - 0.5f * Lf + P.an.dbgLandShift[s]);
                        const vec3 hw = toWorld(P, P.an.plantP[s]);
                        const float e = dot(vec2(hw.x - plan.x, hw.y - plan.y), vec2(P.scanWD[s].x, P.scanWD[s].y)) * 100.f;
                        landErr[w][P.an.dbgLandKind[s] == 3 ? 1 : 0].push_back(e);
                        lastErr[s] = e;
                        landRes[w].push_back(P.an.dbgLandResid[s] * 100.f);
                        landPh[w].push_back(P.an.dbgLandPh[s]);
                    }
#endif
                    for (int s = 0; s < 2; s++) {
                        const bool st = P.an.stepT[s] >= 0.f;
                        if (st && !stepping[s] && (lab == 5 || lab == 6)) quick[lab == 5 ? 0 : 1]++;
                        stepping[s] = st;
#ifdef LM_LAND
                        // (LM_QSLOG: every frame of a quick step on a flight - its progress, the sole points over their
                        // ground, the foot's ground height, along positions of heel and toe tip, the landing's along position)
                        if (st && (lab == 5 || lab == 6) && getenv("LM_QSLOG")) {
                            FootW f;
                            footWorld(P, T, s, f);
                            const vec3 to = toWorld(P, P.an.stepTo[s]), from = toWorld(P, P.an.stepFrom[s]);
                            fprintf(stderr, "QS run %d %s t%.3f foot %d u %.2f h%6.1f b%6.1f t%6.1f g %.2f %.2f %.2f fz %.3f gIn %.3f yh %.3f yt %.3f yFrom %.3f yTo %.3f lift %.3f pz %.3f | n %d dz %.3f o pl%d hold%d st%.2f seen %.2f mw %.2f\n",
                                    gRun - 1, lab == 5 ? "up" : "dn", t, s, P.an.stepT[s], (f.p[0].z - f.gz[0]) * 100.f, (f.p[1].z - f.gz[1]) * 100.f,
                                    (f.p[2].z - f.gz[2]) * 100.f, f.gz[0] - P.pos.z, f.gz[1] - P.pos.z, f.gz[2] - P.pos.z, s ? P.an.footR : P.an.footL,
                                    s ? P.in.groundOffsetR : P.in.groundOffsetL, f.p[0].y, f.p[2].y, from.y, to.y, P.an.stepLift[s], P.pos.z, P.an.dbgN, P.an.dbgDz,
                                    (int)P.an.planted[1 - s], (int)P.an.footHold[1 - s], P.an.stepT[1 - s], P.an.stairSeen, P.an.moveW);
                        }
#endif
                    }
                    if (lab != 5 && lab != 6) return;
                    const int w = lab == 5 ? 0 : 1;
                    for (int s = 0; s < 2; s++) {
                        if (!P.an.planted[s]) continue;
                        FootW f;
                        footWorld(P, T, s, f);
                        downN[w]++;
                        const float low = soleClear(f, T);
                        if (fabsf(f.gz[0] - f.gz[2]) > 0.03f) across[w]++;
                        if (low < -0.01f) sunk[w]++;
                        if (low < -0.03f) sunk3[w]++;
#ifdef LM_LAND
                        if (low < -0.03f && getenv("LM_SINKLOG")) {
                            // (where on its tread: the heel's along position past the tread's rear edge, and the toe tip's short
                            // of its front edge, cm, along +y)
                            auto tr = [&](float y) {
                                const float top = Terrain::kY0 + T.kN * T.kTread, land = top + 2.f;
                                return y < top ? fmodf(y - Terrain::kY0, T.kTread) : fmodf(y - land, T.kTread);
                            };
                            fprintf(stderr, "SINK run %d %s t%.3f foot %d kind %d age %.3f h%5.1f b%5.1f t%5.1f heelInTread %5.1f toeInTread %5.1f stepT %.2f v %.2f landErr %.1f shift %.1f\n",
                                    gRun - 1, w ? "dn" : "up", t, s, P.an.dbgPlantKind[s], P.an.plantAge[s], (f.p[0].z - f.gz[0]) * 100.f,
                                    (f.p[1].z - f.gz[1]) * 100.f, (f.p[2].z - f.gz[2]) * 100.f, tr(f.p[0].y) * 100.f, tr(f.p[2].y) * 100.f,
                                    P.an.stepT[s], length(P.vel), lastErr[s], lastShift[s]);
                        }
#endif
                        if (low < -sc.rise - 0.01f) deep[w]++;
                        if (low < -0.03f && f.p[0].z - f.gz[0] > 0.02f && f.p[2].z - f.gz[2] < -0.03f && f.p[1].z - f.gz[1] > -0.01f) sunkHeelUp[w]++;
                        if (low > 0.015f) lifted[w]++;
#ifdef LM_LAND
                        if (low > 0.015f && getenv("LM_LIFTLOG")) {
                            auto tr = [&](float y) {
                                const float top = Terrain::kY0 + T.kN * T.kTread, land = top + 2.f;
                                return y < top ? fmodf(y - Terrain::kY0, T.kTread) : fmodf(y - land, T.kTread);
                            };
                            fprintf(stderr, "LIFT run %d %s t%.3f foot %d kind %d age %.3f h%5.1f b%5.1f t%5.1f heelInTread %5.1f ballInTread %5.1f toeInTread %5.1f bp%d v %.2f\n",
                                    gRun - 1, w ? "dn" : "up", t, s, P.an.dbgPlantKind[s], P.an.plantAge[s], (f.p[0].z - f.gz[0]) * 100.f,
                                    (f.p[1].z - f.gz[1]) * 100.f, (f.p[2].z - f.gz[2]) * 100.f, tr(f.p[0].y) * 100.f, tr(f.p[1].y) * 100.f, tr(f.p[2].y) * 100.f,
                                    (int)P.an.ballProbe[s], length(P.vel));
                        }
#endif
                    }
                });
                up.addTracker(tr, 5);
                down.addTracker(tr, 6);
                up.nPeople++;
                down.nPeople++;
            }
            up.report(k + ".up");
            down.report(k + ".down");
            for (int w = 0; w < 2; w++) {
                std::string n = k + (w ? ".down" : ".up");
                M(n + ".planted_across_riser_pct", downN[w] ? 100.f * across[w] / downN[w] : 0.f);
                M(n + ".planted_sunk_pct", downN[w] ? 100.f * sunk[w] / downN[w] : 0.f);
                M(n + ".planted_sunk_3cm_pct", downN[w] ? 100.f * sunk3[w] / downN[w] : 0.f);
                M(n + ".planted_lifted_pct", downN[w] ? 100.f * lifted[w] / downN[w] : 0.f);
                M(n + ".sunk_3cm_toe_roll_pct", downN[w] ? 100.f * sunkHeelUp[w] / downN[w] : 0.f);
                M(n + ".quick_steps", (float)quick[w]);
#ifdef LM_LAND
                {
                    auto P_ = [](std::vector<float> e, float q) { std::sort(e.begin(), e.end()); return e.empty() ? 0.f : e[(size_t)(q * (float)(e.size() - 1) + 0.5f)]; };
                    for (int q = 0; q < 2; q++) {
                        const std::vector<float>& e = landErr[w][q];
                        const std::string kn = n + (q ? ".land_held" : ".land_gait");
                        M(kn + "_n", (float)e.size());
                        if (e.empty()) continue;
                        double sum = 0.0;
                        for (float x : e) sum += x;
                        M(kn + "_err_mean_cm", (float)(sum / (double)e.size()));
                        M(kn + "_err_p10_cm", P_(e, 0.1f));
                        M(kn + "_err_med_cm", P_(e, 0.5f));
                        M(kn + "_err_p90_cm", P_(e, 0.9f));
                        M(kn + "_err_max_cm", P_(e, 1.f));
                    }
                    std::vector<float> ar;
                    double ph = 0.0;
                    for (float x : landRes[w]) ar.push_back(fabsf(x));
                    for (float x : landPh[w]) ph += x;
                    M(n + ".land_resid_abs_p90_cm", P_(ar, 0.9f));
                    M(n + ".land_resid_abs_max_cm", P_(ar, 1.f));
                    M(n + ".land_phase_mean", landPh[w].empty() ? 0.f : (float)(ph / (double)landPh[w].size()));
                    M(n + ".land_noplan", (float)landNoPlan[w]);
                }
#endif
                M(n + ".planted_below_a_riser_frames", (float)deep[w]);
                if (getenv("LM_KINDS"))
                    for (int q = 1; q <= 5; q++) {
                        static const char* kn[6] = {"", "plan", "noplan", "held", "quick", "standing"};
                        M(n + ".stances_" + kn[q], (float)kinds[w][q]);
                        M(n + ".stances_" + kn[q] + "_below_a_riser", (float)kindDeep[w][q]);
                    }
                std::vector<float> r = roll[w];
                std::sort(r.begin(), r.end());
                double rs = 0.0;
                for (float x : r) rs += x;
                M(n + ".toe_roll_cm_mean", r.empty() ? 0.f : (float)(rs / r.size()));
                M(n + ".toe_roll_cm_p90", r.empty() ? 0.f : r[Min(r.size() - 1, (size_t)(r.size() * 0.9f))]);
                M(n + ".toe_roll_cm_max", r.empty() ? 0.f : r.back());
            }
        }
        }
    }
}

// starts and stops: first lift latency, slide during the transition, steps to stand, final stance
static void startsStops() {
    if (want("start.walk")) {
        Agg ag;
        float lat = 0.f, latMax = 0.f, lean = 0.f;
        int unloadedFirst = 0;
        for (int p = 0; p < kNP; p++) {
            for (int rep = 0; rep < 3; rep++) {
                Tracker tr;
                Terrain T;
                float tStart = 3.f + 1.7f * rep;
                float firstLift = -1.f;
                int firstFoot = -1;
                float standW0 = -1.f;
                runOne(kPeople[p], kPeople[p].seed * 7u + 11u + rep, T, tStart + 2.5f, [&](float t, const Person& P, Ctl& c) {
                    c.desired = t >= tStart ? vec2(0.f, 1.4f) : vec2(0.f);
                    c.label = t >= tStart - 0.5f && t < tStart + 1.6f ? 7 : 0;
                }, tr, [&](float t, Person& P) {
                    if (t < tStart) standW0 = P.an.standW;
                    if (t >= tStart && firstLift < 0.f) {
                        // the first foot clearly off the ground
                        for (int s = 0; s < 2; s++) {
                            FootW f;
                            footWorld(P, T, s, f);
                            float low = Min(f.p[0].z, Min(f.p[1].z, f.p[2].z));
                            if (low > 0.02f && firstLift < 0.f) firstLift = t - tStart, firstFoot = s;
                        }
                    }
                });
                ag.addTracker(tr, 7);
                lat += firstLift;
                latMax = Max(latMax, firstLift);
                // the unloaded foot steps first (weight on the right -> the left foot)
                if ((standW0 > 0.6f && firstFoot == 0) || (standW0 < 0.4f && firstFoot == 1) || (standW0 >= 0.4f && standW0 <= 0.6f)) unloadedFirst++;
                (void)lean;
            }
            ag.nPeople += 3;
        }
        ag.report("start.walk_1.4");
        M("start.walk_1.4.first_lift_s_mean", lat / (kNP * 3));
        M("start.walk_1.4.first_lift_s_max", latMax);
        M("start.walk_1.4.unloaded_foot_first_pct", 100.f * unloadedFirst / (kNP * 3));
    }
    const float stopV[] = {1.4f, 3.0f};
    const char* stopN[] = {"stop.walk_1.4", "stop.jog_3.0"};
    for (int i = 0; i < 2; i++) {
        if (!want(stopN[i])) continue;
        Agg ag;
        float settle = 0.f, settleMax = 0.f, sep = 0.f, sepMax = 0.f, steps = 0.f, offAxis = 0.f;
        int runs = 0;
        for (int p = 0; p < kNP; p++) {
            for (int rep = 0; rep < 3; rep++) {
                Tracker tr;
                Terrain T;
                float tStop = 3.f + 0.37f * rep;   // stops at different points of the stride
                float lastMove = 0.f;
                int touch = 0;
                vec3 fl, fr;
                float v = stopV[i];
                runOne(kPeople[p], kPeople[p].seed * 7u + 21u + rep, T, tStop + 3.f, [&](float t, const Person& P, Ctl& c) {
                    c.desired = t < tStop ? vec2(0.f, v) : vec2(0.f);
                    c.label = t >= tStop && t < tStop + 2.5f ? 8 : 0;
                }, tr, [&](float t, Person& P) {
                    if (t < tStop) return;
                    // any foot off the ground or moving: still settling
                    for (int s = 0; s < 2; s++) {
                        FootW f;
                        footWorld(P, T, s, f);
                        if (Min(f.p[0].z, f.p[1].z) > 0.008f) lastMove = t - tStop;
                    }
                    touch += __builtin_popcount(P.an.footEvents);
                    vec3 a = toWorld(P, P.m[B_FOOT_L].c[3].xyz()), b = toWorld(P, P.m[B_FOOT_R].c[3].xyz());
                    fl = a;
                    fr = b;
                });
                ag.addTracker(tr, 8);
                settle += lastMove;
                settleMax = Max(settleMax, lastMove);
                steps += touch;
                float fa = fabsf(fl.y - fr.y);   // fore-aft offset of the feet at the end
                sep += fa;
                sepMax = Max(sepMax, fa);
                offAxis += fabsf(fl.x - fr.x);
                runs++;
            }
            ag.nPeople += 3;
        }
        ag.report(stopN[i]);
        M(std::string(stopN[i]) + ".settle_s_mean", settle / runs);
        M(std::string(stopN[i]) + ".settle_s_max", settleMax);
        M(std::string(stopN[i]) + ".touchdowns_after_stop", steps / runs);
        M(std::string(stopN[i]) + ".final_foreaft_cm_mean", 100.f * sep / runs);
        M(std::string(stopN[i]) + ".final_foreaft_cm_max", 100.f * sepMax);
        M(std::string(stopN[i]) + ".final_width_cm_mean", 100.f * offAxis / runs);
    }
}

// turning on the spot (standing, facing a new heading at the AI's 6 rad/s or slower) and sharp turns while walking
static void turns() {
    struct TS {
        const char* name;
        float angle, rate;
        bool known;
    };
    const TS spot[] = {{"turnspot.90_at_6", 90.f, 6.f, true}, {"turnspot.180_at_6", 180.f, 6.f, true}, {"turnspot.90_at_2", 90.f, 2.f, true},
                       {"turnspot.45_at_6", 45.f, 6.f, true}, {"turnspot.90_at_8_norate", 90.f, 8.f, false}};
    for (const TS& ts : spot) {
        if (!want(ts.name)) continue;
        Agg ag;
        float settle = 0.f, nSteps = 0.f, bodyErr = 0.f;
        int runs = 0;
        for (int p = 0; p < kNP; p++) {
            for (int rep = 0; rep < 2; rep++) {
                Tracker tr;
                Terrain T;
                float tT = 3.f + 1.3f * rep, dir = rep ? -1.f : 1.f;
                float lastMove = 0.f;
                int touch = 0;
                float target = dir * ts.angle * kPi / 180.f;
                runOne(kPeople[p], kPeople[p].seed * 7u + 31u + rep, T, tT + 3.5f, [&](float t, const Person& P, Ctl& c) {
                    c.face = true;
                    c.faceYaw = t >= tT ? target : 0.f;
                    c.rate = ts.rate;
                    c.label = t >= tT && t < tT + 3.f ? 9 : 0;
                }, tr, [&](float t, Person& P) {
                    if (t < tT) return;
                    touch += __builtin_popcount(P.an.footEvents);
                    for (int s = 0; s < 2; s++) {
                        FootW f;
                        footWorld(P, T, s, f);
                        if (Min(f.p[0].z, f.p[1].z) > 0.008f) lastMove = t - tT;
                    }
                    if (t > tT + 3.4f) {
                        // the pelvis faces the new heading at the end
                        vec3 pf = transformDir(P.m[B_PELVIS], vec3(0, 1, 0));
                        float py = atan2f(-pf.x, pf.y) + P.yaw;
                        bodyErr = Max(bodyErr, fabsf(wrapA(py - target)) * 57.3f);
                    }
                }, 0.f, ts.known);
                ag.addTracker(tr, 9);
                settle += lastMove;
                nSteps += touch;
                runs++;
            }
            ag.nPeople += 2;
        }
        ag.report(ts.name);
        M(std::string(ts.name) + ".settle_s_mean", settle / runs);
        M(std::string(ts.name) + ".touchdowns", nSteps / runs);
        M(std::string(ts.name) + ".pelvis_heading_err_deg_max", bodyErr);
    }
    struct WT {
        const char* name;
        float angle, v;
    };
    const WT walk[] = {{"turnwalk.45_at_1.4", 45.f, 1.4f}, {"turnwalk.90_at_1.4", 90.f, 1.4f}, {"turnwalk.135_at_1.4", 135.f, 1.4f},
                       {"turnwalk.180_at_1.4", 180.f, 1.4f}, {"turnwalk.90_at_3.0", 90.f, 3.f}, {"turnwalk.90_at_5.0", 90.f, 5.f}};
    for (const WT& wt : walk) {
        if (!want(wt.name)) continue;
        Agg ag;
        float minSpd = 0.f, backT = 0.f;
        int runs = 0;
        for (int p = 0; p < kNP; p++) {
            for (int rep = 0; rep < 3; rep++) {
                Tracker tr;
                Terrain T;
                float tT = 3.f + 0.29f * rep, dir = rep == 1 ? -1.f : 1.f;
                float a = dir * wt.angle * kPi / 180.f;
                float back = 0.f;
                runOne(kPeople[p], kPeople[p].seed * 7u + 41u + rep, T, tT + 3.f, [&](float t, const Person& P, Ctl& c) {
                    c.desired = (t < tT ? dirOf(0.f) : dirOf(a)) * wt.v;
                    c.label = t >= tT - 0.2f && t < tT + 1.8f ? 10 : 0;
                }, tr, [&](float t, Person& P) {
                    if (t >= tT && t < tT + 1.8f && P.in.localMoveDir.y < -0.3f && P.in.speed > 0.2f) back += kDt;
                });
                ag.addTracker(tr, 10);
                backT += back;
                runs++;
            }
            ag.nPeople += 3;
        }
        ag.report(wt.name);
        M(std::string(wt.name) + ".backwards_s_mean", backT / runs);
        (void)minSpd;
    }
    // the game keeps the last turnTo rate when a ped stops mid-turn (no desired velocity, nothing to face): a stale
    // turnRate with the yaw not changing
    if (want("stale_turnrate")) {
        Agg ag;
        for (int p = 0; p < kNP; p++) {
            Tracker tr;
            Terrain T;
            runOne(kPeople[p], kPeople[p].seed * 7u + 51u, T, 8.f, [&](float t, const Person& P, Ctl& c) {
                // walking, a turn starts and the ped stops in the same frame (desired 0 after one frame of turning)
                if (t < 3.f) c.desired = vec2(0.f, 1.4f);
                else if (t < 3.f + kDt * 1.5f) c.desired = dirOf(1.5f) * 1.4f;
                else c.desired = vec2(0.f);
                c.label = t >= 3.5f ? 11 : 0;
            }, tr);
            ag.addTracker(tr, 11);
            ag.nPeople++;
        }
        ag.report("stale_turnrate.stand_after", false);
    }
}

// turns as a walk starts or stops (the locomotion tour's biggest slides: peds turning 1.7-3.2 rad/s with the walk at
// 0.2-0.9 of its weight - setting off from a crossing towards the next goal, pulling up into a stop while turning,
// slowing round a corner): the yaw follows the desired velocity at the game's 6 rad/s, the velocity at its 11 / 16 m/s^2
static void startStopTurns() {
    struct SS {
        const char* name;
        int kind;      // 0 start: standing, then walking off at the angle; 1 stop: walking, then slowing at the angle to a stop;
                       // 2 corner: walking, slowing to 0.45 m/s round the angle, then walking on
        float angle;
    };
    const SS sc[] = {{"turnstart.90", 0, 90.f},  {"turnstart.180", 0, 180.f}, {"turnstop.90", 1, 90.f},
                     {"turnstop.180", 1, 180.f}, {"corner.90_slow", 2, 90.f}, {"corner.150_slow", 2, 150.f}};
    for (const SS& c0 : sc) {
        if (!want(c0.name)) continue;
        Agg ag;
        for (int p = 0; p < kNP; p++) {
            for (int rep = 0; rep < 3; rep++) {
                Tracker tr;
                Terrain T;
                const float tS = 3.f + 0.31f * rep, dir = rep == 1 ? -1.f : 1.f, a = dir * c0.angle * kPi / 180.f;
                runOne(kPeople[p], kPeople[p].seed * 7u + 61u + rep, T, tS + 3.5f, [&](float t, const Person& P, Ctl& c) {
                    if (c0.kind == 0) c.desired = t < tS ? vec2(0.f) : dirOf(a) * 1.4f;
                    else if (c0.kind == 1) c.desired = t < tS ? dirOf(0.f) * 1.4f : (t < tS + 0.7f ? dirOf(a) * 0.35f : vec2(0.f));
                    else c.desired = t < tS ? dirOf(0.f) * 1.4f : dirOf(a) * (t < tS + 0.8f ? 0.45f : 1.4f);
                    c.label = t >= tS - 0.2f && t < tS + 3.f ? 12 : 0;
                }, tr);
                ag.addTracker(tr, 12);
            }
            ag.nPeople += 3;
        }
        ag.report(c0.name);
    }
    // a group follower keeping its place (the locotour's ped 13: 0-2 m/s, 0.47 on average, the walk at 0.42 of its weight,
    // the turn rate changing sign every 5 frames): the pace wavering about a slow walk, the way swinging +-20-35 degrees
    for (int kind = 0; kind < 2; kind++) {
        const char* name = kind ? "shuffle.follow_fast" : "shuffle.follow";
        if (!want(name)) continue;
        Agg ag;
        for (int p = 0; p < kNP; p++) {
            for (int rep = 0; rep < 3; rep++) {
                Tracker tr;
                Terrain T;
                const float ph = 1.3f * (float)p + 2.1f * (float)rep;
                runOne(kPeople[p], kPeople[p].seed * 7u + 71u + rep, T, 9.f, [&](float t, const Person& P, Ctl& c) {
                    if (t < 2.f) {
                        c.desired = vec2(0.f);
                    } else {
                        const float yaw = 0.35f * sinf(t * 9.f + ph) + 0.2f * (sinf(t * 5.3f + 2.f * ph) > 0.f ? 1.f : -1.f);
                        const float v = Max(0.f, (kind ? 0.75f : 0.45f) + 0.3f * sinf(t * 4.1f + ph) + 0.15f * sinf(t * 11.3f + 3.f * ph));
                        c.desired = dirOf(yaw) * v;
                    }
                    c.label = t >= 2.5f ? 13 : 0;
                }, tr);
                ag.addTracker(tr, 13);
            }
            ag.nPeople += 3;
        }
        ag.report(name);
    }
}

// two people passing on a sidewalk (pednav's social force: repulsion with ~0.7 m personal space, keep right when
// head-on), head-on with a small lateral offset, and overtaking
struct Walk2 {
    float lat, dirY, spd;
};
// the game's passBy search as it was before the walking-together fix (LM_PASS_OLD=1): a companion at the elbow counted
static const bool gPassOld = getenv("LM_PASS_OLD") != nullptr;
static void passing() {
    struct PS {
        const char* name;
        Walk2 a, b;
        float yb;       // b's start y
        bool social;    // pednav's social force between them (off: two companions keeping their places side by side)
    };
    const PS cases[] = {{"pass.headon_0.2", {0.f, 1.f, 1.35f}, {0.2f, -1.f, 1.4f}, 14.f, true},
                        {"pass.headon_0.5", {0.f, 1.f, 1.35f}, {0.5f, -1.f, 1.4f}, 14.f, true},
                        {"pass.overtake_0.3", {0.f, 1.f, 1.15f}, {0.3f, 1.f, 1.65f}, -3.5f, true},
                        {"pass.abreast_0.6", {0.f, 1.f, 1.3f}, {0.6f, 1.f, 1.3f}, 0.f, false},
                        {"pass.abreast_0.85", {0.f, 1.f, 1.3f}, {0.85f, 1.f, 1.3f}, 0.f, false},
                        {"pass.pace_0.6", {0.f, 1.f, 1.3f}, {0.6f, 1.f, 1.5f}, -0.4f, false}};
    for (const PS& ps : cases) {
        if (!want(ps.name)) continue;
        Agg ag;
        float minGap = 1e9f, maxLat = 0.f, maxHead = 0.f, chestTurn = 0.f, minCenter = 1e9f, twistMax = 0.f, shiftMax = 0.f;
        int runs = 0;
        for (int pi = 0; pi < kNP; pi += 2) {
            std::unique_ptr<Person> A = makePerson(kPeople[pi], 101u + pi), B = makePerson(kPeople[pi + 1], 202u + pi);
            Terrain T;
            A->pos = vec3(ps.a.lat, 0.f, 0.f);
            A->yaw = ps.a.dirY > 0.f ? 0.f : kPi;
            B->pos = vec3(ps.b.lat, ps.yb, 0.f);
            B->yaw = ps.b.dirY > 0.f ? 0.f : kPi;
            A->vel = vec2(0.f, ps.a.dirY * ps.a.spd);
            B->vel = vec2(0.f, ps.b.dirY * ps.b.spd);
            Tracker ta, tb;
            const float secs = 12.f;
            for (int f = 0; f < (int)(secs / kDt); f++) {
                float t = f * kDt;
                Person* P[2] = {A.get(), B.get()};
                const Walk2* W[2] = {&ps.a, &ps.b};
                vec2 des[2];
                for (int i = 0; i < 2; i++) {
                    Person& me = *P[i];
                    const Person& o = *P[1 - i];
                    vec2 pos(me.pos.x, me.pos.y);
                    vec2 tgt(W[i]->lat, me.pos.y + 1.6f * W[i]->dirY);
                    vec2 to = tgt - pos;
                    vec2 d = normalize(to) * W[i]->spd;
                    vec2 rel = pos - vec2(o.pos.x, o.pos.y);
                    float dist = length(rel);
                    vec2 push(0.f);
                    if (ps.social && dist < 3.5f && dist > 1e-3f) {
                        vec2 n = rel / dist;
                        push = push + n * (1.6f * expf(-(dist - 0.6f) / 0.35f));
                        vec2 rv = d - o.vel;
                        if (dot(rv, -n) > 0.3f && dist < 2.5f) {
                            vec2 mv = normalize(d + vec2(1e-3f, 0.f));
                            push = push + vec2(mv.y, -mv.x) * 0.45f;
                        }
                    }
                    d = d + push;
                    float m = length(d), cap = Max(W[i]->spd * 1.35f, 1.8f);
                    if (m > cap) d = d * (cap / m);
                    des[i] = d;
                }
                for (int i = 0; i < 2; i++) control(*P[i], des[i], nullptr, 6.f, T);
                for (int i = 0; i < 2; i++) {
                    // the game's passBy (peds.cpp animatePed): the other person closing within a metre
                    Person& p = *P[i];
                    const Person& o = *P[1 - i];
                    p.in.passWeight = 0.f;
                    vec2 d2(o.pos.x - p.pos.x, o.pos.y - p.pos.y), rv = o.vel - p.vel;
                    float dist = length(d2), rv2 = length2(rv);
                    if (!gPassOld && rv2 < 0.09f) continue;   // (the only other one here: lean only while it goes by)
                    float tca = rv2 > 1e-3f ? Clamp(-dot(d2, rv) / rv2, 0.f, 1.5f) : 0.f;
                    if (dist < 2.6f && length(d2 + rv * tca) <= 1.f) {
                        vec2 fwd(-sinf(p.yaw), cosf(p.yaw)), rightV(cosf(p.yaw), sinf(p.yaw));
                        vec3 c = vec3(d2.x, d2.y, 0.f) + rotate(qz(o.yaw), o.m[B_CHEST].c[3].xyz());
                        p.in.passBy = vec3(dot(vec2(c.x, c.y), rightV), dot(vec2(c.x, c.y), fwd), c.z);
                        p.in.passVel = vec2(dot(rv, rightV), dot(rv, fwd));
                        float kk = Saturate((2.4f - dist) / 1.3f);
                        p.in.passWeight = kk * kk * (3.f - 2.f * kk);
                    }
                }
                for (int i = 0; i < 2; i++) animate(*P[i], T);
                float dy = fabsf(A->pos.y - B->pos.y);
                int label = dy < 2.5f && t > 1.f ? 12 : 0;
                ta.add(*A, T, t, label);
                tb.add(*B, T, t, label);
                if (label) {
                    // closest approach of the two bodies: shoulders, elbows, hands (the arms swing), as spheres
                    static const int kB[6] = {B_UPPERARM_L, B_UPPERARM_R, B_FOREARM_L, B_FOREARM_R, B_HAND_L, B_HAND_R};
                    static const float kR[6] = {0.07f, 0.07f, 0.05f, 0.05f, 0.04f, 0.04f};
                    for (int i = 0; i < 6; i++)
                        for (int j = 0; j < 6; j++) {
                            vec3 a = toWorld(*A, A->m[kB[i]].c[3].xyz()), b = toWorld(*B, B->m[kB[j]].c[3].xyz());
                            minGap = Min(minGap, length(a - b) - kR[i] - kR[j]);
                        }
                    minCenter = Min(minCenter, length(vec2(A->pos.x - B->pos.x, A->pos.y - B->pos.y)));
                    for (int i = 0; i < 2; i++) {
                        twistMax = Max(twistMax, fabsf(P[i]->an.passYaw) * 57.3f);
                        shiftMax = Max(shiftMax, fabsf(P[i]->an.passShift) * 100.f);
                        maxLat = Max(maxLat, fabsf(P[i]->vel.x));
                        float head = fabsf(wrapA(P[i]->yaw - (W[i]->dirY > 0.f ? 0.f : kPi)));
                        maxHead = Max(maxHead, head * 57.3f);
                        // the shoulder line against the hips (a turned shoulder lets a close pass by)
                        vec3 cx = transformDir(P[i]->m[B_CHEST], vec3(1, 0, 0)), px = transformDir(P[i]->m[B_PELVIS], vec3(1, 0, 0));
                        float ct = fabsf(wrapA(atan2f(cx.y, cx.x) - atan2f(px.y, px.x)));
                        if (dy < 0.6f) chestTurn = Max(chestTurn, ct * 57.3f);
                    }
                }
            }
            ta.finish(secs);
            tb.finish(secs);
            ag.addTracker(ta, 12);
            ag.addTracker(tb, 12);
            ag.nPeople += 2;
            runs++;
        }
        ag.report(ps.name);
        M(std::string(ps.name) + ".min_arm_gap_cm", minGap * 100.f);
        M(std::string(ps.name) + ".min_center_dist_cm", minCenter * 100.f);
        M(std::string(ps.name) + ".max_lateral_speed", maxLat);
        M(std::string(ps.name) + ".max_heading_dev_deg", maxHead);
        M(std::string(ps.name) + ".chest_turn_at_pass_deg", chestTurn);
        M(std::string(ps.name) + ".pass_twist_max_deg", twistMax);
        M(std::string(ps.name) + ".pass_sidestep_max_cm", shiftMax);
    }
}

// a standing crowd: how often each person changes something (weight shift, settling step, posture, fidget, glance),
// how long the longest stretch without anything is, how in step the crowd is
static void crowd() {
    if (!want("crowd")) return;
    const int N = 16;
    const float secs = 120.f;
    std::vector<std::unique_ptr<Person>> P;
    for (int i = 0; i < N; i++) {
        Who w = {1000u + i * 37u, i % 6, 0.15f + 0.05f * (i % 13), (int)(i & 1)};
        P.push_back(makePerson(w, 5000u + i * 13u));
    }
    Terrain T;
    std::vector<float> lastEvent(N, 0.f), longest(N, 0.f);
    int shifts = 0, settles = 0, postures = 0, fidgets = 0, glances = 0;
    std::vector<float> prevTarget(N), shiftTimes;
    std::vector<int> prevIdle(N, -1), prevFidget(N, -1);
    std::vector<bool> prevGlance(N, false);
    std::vector<int> usedIdle(64, 0);
    float sway = 0.f;
    std::vector<float> pelMin(N, 1e9f), pelMax(N, -1e9f);
    Tracker tr[16];
    for (int f = 0; f < (int)(secs / kDt); f++) {
        float t = f * kDt;
        for (int i = 0; i < N; i++) {
            Person& p = *P[i];
            p.in.stance = i < 12 ? 0 : 23;
            control(p, vec2(0.f), nullptr, 6.f, T);
            animate(p, T);
            tr[i].add(p, T, t, t > 2.f ? 13 : 0);
            if (t < 2.f) {
                prevTarget[i] = p.an.standTarget;
                continue;
            }
            bool ev = false;
            if (fabsf(p.an.standTarget - prevTarget[i]) > 0.01f) shifts++, ev = true, shiftTimes.push_back(t);
            prevTarget[i] = p.an.standTarget;
            if (p.an.footEvents) settles += __builtin_popcount(p.an.footEvents), ev = true;
            if (p.an.idleVar != prevIdle[i] && p.an.idleVar >= 0) {
                postures++, ev = true;
                usedIdle[p.an.idleVar & 63]++;
            }
            prevIdle[i] = p.an.idleVar;
            if (p.an.fidgetVar != prevFidget[i] && p.an.fidgetVar >= 0) {
                fidgets++, ev = true;
                usedIdle[p.an.fidgetVar & 63]++;
            }
            prevFidget[i] = p.an.fidgetVar;
            bool g = p.an.glanceT >= 0.f;
            if (g && !prevGlance[i]) glances++, ev = true;
            prevGlance[i] = g;
            if (ev) lastEvent[i] = t;
            longest[i] = Max(longest[i], t - lastEvent[i]);
            vec3 pel = toWorld(p, p.m[B_PELVIS].c[3].xyz());
            pelMin[i] = Min(pelMin[i], pel.x);
            pelMax[i] = Max(pelMax[i], pel.x);
        }
    }
    float mins = (secs - 2.f) / 60.f;
    M("crowd.weight_shifts_per_person_min", shifts / (N * mins));
    M("crowd.settle_steps_per_person_min", settles / (N * mins));
    M("crowd.postures_per_person_min", postures / (N * mins));
    M("crowd.fidgets_per_person_min", fidgets / (N * mins));
    M("crowd.glances_per_person_min", glances / (N * mins));
    float lmax = 0.f, lmean = 0.f;
    for (int i = 0; i < N; i++) lmax = Max(lmax, longest[i]), lmean += longest[i] / N;
    M("crowd.longest_still_s_mean", lmean);
    M("crowd.longest_still_s_max", lmax);
    int kinds = 0;
    for (int k = 0; k < 64; k++) kinds += usedIdle[k] > 0;
    M("crowd.distinct_postures_fidgets", (float)kinds);
    for (int i = 0; i < N; i++) sway += (pelMax[i] - pelMin[i]) / N;
    M("crowd.pelvis_sideways_range_cm_mean", sway * 100.f);
    // in step: shifts starting within 0.25 s of another person's
    std::sort(shiftTimes.begin(), shiftTimes.end());
    int close = 0;
    for (size_t i = 1; i < shiftTimes.size(); i++) close += shiftTimes[i] - shiftTimes[i - 1] < 0.25f;
    M("crowd.shift_pairs_within_0.25s_pct", shiftTimes.size() > 1 ? 100.f * close / (shiftTimes.size() - 1) : 0.f);
    Agg ag;
    for (int i = 0; i < N; i++) {
        tr[i].finish(secs);
        ag.addTracker(tr[i], 13);
    }
    ag.nPeople = N;
    ag.report("crowd.feet", false);
}

// look-at: someone walking past 2 m in front (left to right at 1.4 m/s, the game's lookWeight 0.6), a siren 70 deg to
// the left appearing at 1 s (lookWeight 0.5): head tracking error, latency, the eyes leading

// pushed about without a velocity (GameWorld::updatePeds' ped-ped separation, a collider's push-out): standing, then
// walking with a sideways jostle; with and without the animator told the root's own move
static void pushes() {
    struct PC {
        const char* name;
        float walk;      // m/s along +y
        float push;      // m per frame
        bool off;        // the animator not told
    };
    const PC cases[] = {{"push.stand", 0.f, 0.012f, false}, {"push.stand_velonly", 0.f, 0.012f, true},
                        {"push.walk", 1.3f, 0.008f, false}, {"push.walk_velonly", 1.3f, 0.008f, true}};
    for (const PC& pc : cases) {
        if (!want(pc.name)) continue;
        Agg ag;
        for (int p = 0; p < kNP; p++) {
            Tracker tr;
            Terrain T;
            std::unique_ptr<Person> P = makePerson(kPeople[p], kPeople[p].seed * 7u + 71u);
            gTraceNow = gTraceOn && gRun == gTraceRun;
            gRun++;
            P->rootMoveOff = pc.off;
            const int n = (int)(6.f / kDt);
            for (int f = 0; f < n; f++) {
                float t = f * kDt;
                control(*P, vec2(0.f, pc.walk), nullptr, 6.f, T);
                // the push: sideways (standing: one way for 1.5 s; walking: a jostle that changes side every 0.4 s)
                if (t > 2.f && t < 4.5f) {
                    float sx = pc.walk > 0.f ? ((int)((t - 2.f) / 0.4f) % 2 ? -1.f : 1.f) : (t < 3.5f ? 1.f : 0.f);
                    P->pos.x += sx * pc.push * (kDt * 60.f);
                }
                animate(*P, T);
                tr.add(*P, T, t, t > 2.f && t < 5.5f ? 16 : 0);
            }
            tr.finish(6.f);
            gTraceNow = false;
            ag.addTracker(tr, 16);
            ag.nPeople++;
        }
        ag.report(pc.name);
    }
}

static void lookat() {
    if (!want("look")) return;
    float errSum = 0.f, errMax = 0.f, lat90 = 0.f, lat90Max = 0.f, eyeLead = 0.f, overshoot = 0.f;
    float gzSum[2] = {0.f, 0.f}, gzMax[2] = {0.f, 0.f}, lagSum[2] = {0.f, 0.f};
    int errN = 0, runs = 0, gzN[2] = {0, 0};
    for (int p = 0; p < kNP; p++) {
        // pass-by: someone walking past 2 m in front (head error against the whole angle, as before), and someone
        // running past 1.2 m in front; for both the gaze (the eyes' line) against the target and the head's lag
        // behind its own share of the turn (headShare: the eyes take the first ~7 deg and 15% of the rest)
        for (int c = 0; c < 2; c++) {
            std::unique_ptr<Person> P = makePerson(kPeople[p], kPeople[p].seed * 7u + 61u + c * 5u);
            Terrain T;
            const float ly = c ? 1.2f : 2.f, lv = c ? 3.f : 1.4f, x0 = c ? -8.f : -6.f;
            for (int f = 0; f < (int)(12.f / kDt); f++) {
                float t = f * kDt;
                vec3 tgt(x0 + lv * Max(0.f, t - 2.f), ly, 1.6f);
                P->in.lookAt = tgt;
                P->in.lookWeight = t > 2.f && tgt.x < 6.f ? 0.6f : 0.f;
                control(*P, vec2(0.f), nullptr, 6.f, T);
                animate(*P, T);
                if (P->in.lookWeight > 0.f) {
                    vec3 hp = P->m[B_HEAD].c[3].xyz(), hf = transformDir(P->m[B_HEAD], vec3(0, 1, 0));
                    vec3 d = tgt - hp;
                    float want = atan2f(-d.x, d.y), got = atan2f(-hf.x, hf.y);
                    // the part the head should take (the eyes take the first ~7 deg, the neck limits at ~77)
                    float wh = Clamp(want, -1.35f, 1.35f);
                    if (fabsf(want) < 1.2f && t > 3.f) {
                        float e = fabsf(wrapA(wh - got)) * 57.3f;
                        if (!c) {
                            errSum += e;
                            errN++;
                            errMax = Max(errMax, e);
                        }
                        vec3 ep = P->m[B_EYE_L].c[3].xyz(), ef = transformDir(P->m[B_EYE_L], vec3(0, 1, 0)), de = tgt - ep;
                        float g = fabsf(wrapA(atan2f(-de.x, de.y) - atan2f(-ef.x, ef.y))) * 57.3f;
                        float share = Max(0.f, fabsf(want) - 0.12f) * 0.85f;
                        gzSum[c] += g;
                        gzMax[c] = Max(gzMax[c], g);
                        lagSum[c] += fabsf(wrapA((want < 0.f ? -share : share) - got)) * 57.3f;
                        gzN[c]++;
                    }
                }
            }
        }
        Terrain T;
        // siren: a target 70 deg left, far away, from 1 s
        std::unique_ptr<Person> Q = makePerson(kPeople[p], kPeople[p].seed * 7u + 62u);
        float reached = -1.f, peak = 0.f, firstEye = -1.f, firstHead = -1.f;
        const float a = 70.f * kPi / 180.f;
        for (int f = 0; f < (int)(5.f / kDt); f++) {
            float t = f * kDt;
            Q->in.lookAt = vec3(-sinf(a) * 25.f, cosf(a) * 25.f, 2.f);
            Q->in.lookWeight = t >= 1.f ? 0.5f : 0.f;
            control(*Q, vec2(0.f), nullptr, 6.f, T);
            animate(*Q, T);
            if (t >= 1.f) {
                vec3 hf = transformDir(Q->m[B_HEAD], vec3(0, 1, 0));
                float got = atan2f(-hf.x, hf.y);
                peak = Max(peak, got);
                if (reached < 0.f && got > 0.9f * (a - 0.12f) * 0.85f) reached = t - 1.f;
                vec3 ef = transformDir(Q->m[B_EYE_L], vec3(0, 1, 0));
                float ey = atan2f(-ef.x, ef.y);
                if (firstEye < 0.f && ey > 0.2f) firstEye = t - 1.f;
                if (firstHead < 0.f && got > 0.2f) firstHead = t - 1.f;
            }
        }
        lat90 += reached;
        lat90Max = Max(lat90Max, reached);
        overshoot = Max(overshoot, (peak - (a - 0.12f) * 0.85f) * 57.3f);
        eyeLead += firstHead - firstEye;
        runs++;
    }
    M("look.passby_head_err_deg_mean", errN ? errSum / errN : 0.f);
    M("look.passby_head_err_deg_max", errMax);
    for (int c = 0; c < 2; c++) {
        std::string n = c ? "look.runby_" : "look.passby_";
        M(n + "gaze_err_deg_mean", gzN[c] ? gzSum[c] / gzN[c] : 0.f);
        M(n + "gaze_err_deg_max", gzMax[c]);
        M(n + "head_lag_deg_mean", gzN[c] ? lagSum[c] / gzN[c] : 0.f);
    }
    M("look.siren_head_90pct_s_mean", lat90 / runs);
    M("look.siren_head_90pct_s_max", lat90Max);
    M("look.siren_overshoot_deg", overshoot);
    M("look.siren_eyes_lead_head_s", eyeLead / runs);
}

// Interiors (the hand-built buildings: stairs, landings, doorways, small rooms): a walker following a path at an indoor
// pace - steering at the point 0.6 m further along it, slowing into its end - its body turning as the game turns it
// (LM_TURNPROF). switchback.*: two 10-step flights side by side (17 x 29 cm) with a 180-degree turn on the landing
// between; lstair.*: two flights with a 90-degree turn on a square landing; spiral.*: 13 treads of 22.5 degrees (19 cm
// rise, 27 cm deep on the walking line 0.7 m out) round a column; room.doorway: two 90-degree turns through doorways
// 1.5 m apart on the flat; room.turnback: across a small room to a wall, a moment there, round and back. Stair stats as on
// the straight stairs (planted frames sunk 1 / 3 cm, below a riser, lifted) over the stair's part
struct PathFollow {
    std::vector<vec2> pts;
    std::vector<float> acc;   // (arc length at each point)
    void build() {
        acc.assign(pts.size(), 0.f);
        for (size_t i = 1; i < pts.size(); i++) acc[i] = acc[i - 1] + length(pts[i] - pts[i - 1]);
    }
    float total() const { return acc.empty() ? 0.f : acc.back(); }
    vec2 at(float sArc) const {
        sArc = Clamp(sArc, 0.f, total());
        size_t i = 1;
        while (i + 1 < pts.size() && acc[i] < sArc) i++;
        const float seg = Max(acc[i] - acc[i - 1], 1e-5f);
        return lerp(pts[i - 1], pts[i], Saturate((sArc - acc[i - 1]) / seg));
    }
    // (the arc length of the point on the path nearest q, searched forward from the last one: a path that comes back on
    // itself is followed in order)
    float sNear = 0.f;
    float nearest(vec2 q) {
        float best = 1e9f, bs = sNear;
        for (size_t i = 1; i < pts.size(); i++) {
            if (acc[i] < sNear - 0.01f) continue;
            const vec2 ab = pts[i] - pts[i - 1];
            const float L2 = Max(dot(ab, ab), 1e-8f);
            float u = Saturate(dot(q - pts[i - 1], ab) / L2);
            float sA = acc[i - 1] + u * sqrtf(L2);
            if (sA < sNear) {
                sA = sNear;
                u = (sA - acc[i - 1]) / sqrtf(L2);
            }
            const float d = length(pts[i - 1] + ab * u - q);
            if (d < best - 1e-4f) best = d, bs = sA;
            if (acc[i - 1] > sNear + 1.5f) break;   // (only a little way ahead)
        }
        sNear = Max(sNear, bs);
        return sNear;
    }
};

// (the hand-built world's stairs, to US / Florida codes: switchbacks in every stair core - 17.5 x 28 cm, two flights a
// storey, the landing as deep as the flight is wide - L stairs with a landing or three winders in houses, spirals of
// 1.5-2 m (walked about 0.5 m out, 22.5-30 degrees and 19-22 cm a tread), grand curved stairs of 2-4 m)
struct IS {
    const char* name;
    int kind;   // terrain: 0 flat, 5 switchback, 6 spiral / curve, 7 L stair, 8 L stair with winders
    int dir;    // 0 up (room.doorway), 1 down (room.turnback)
    float v;
    float rise, tread, width, land;   // (switchback / L: the riser, the tread, the flight's width, the landing's depth)
    int n;                            // (treads a flight; a spiral's or curve's treads)
    float R, ang, rIn, rOut;          // (spiral / curve: the walking line's radius, a tread's angle, the radii)
};
static void runIS(const IS& c0);

static void interior() {
    const IS sc[] = {
        {"switchback.up_1.0", 5, 0, 1.0f, 0.175f, 0.28f, 1.1f, 1.1f, 9, 0, 0, 0, 0},
        {"switchback.down_1.0", 5, 1, 1.0f, 0.175f, 0.28f, 1.1f, 1.1f, 9, 0, 0, 0, 0},
        {"switchback_tight.up_1.0", 5, 0, 1.0f, 0.18f, 0.27f, 0.9f, 0.9f, 9, 0, 0, 0, 0},
        {"switchback_tight.down_1.0", 5, 1, 1.0f, 0.18f, 0.27f, 0.9f, 0.9f, 9, 0, 0, 0, 0},
        {"lstair.up_1.0", 7, 0, 1.0f, 0.175f, 0.28f, 1.0f, 1.0f, 8, 0, 0, 0, 0},
        {"lstair.down_1.0", 7, 1, 1.0f, 0.175f, 0.28f, 1.0f, 1.0f, 8, 0, 0, 0, 0},
        {"winder.up_0.9", 8, 0, 0.9f, 0.18f, 0.28f, 1.0f, 1.0f, 7, 0, 0, 0, 0},
        {"winder.down_0.9", 8, 1, 0.9f, 0.18f, 0.28f, 1.0f, 1.0f, 7, 0, 0, 0, 0},
        {"spiral.up_0.7", 6, 0, 0.7f, 0.2f, 0, 0, 0, 10, 0.55f, 0.5236f, 0.1f, 0.85f},
        {"spiral.down_0.7", 6, 1, 0.7f, 0.2f, 0, 0, 0, 10, 0.55f, 0.5236f, 0.1f, 0.85f},
        {"spiral_tight.up_0.6", 6, 0, 0.6f, 0.2f, 0, 0, 0, 13, 0.5f, 0.3927f, 0.1f, 0.8f},
        {"spiral_tight.down_0.6", 6, 1, 0.6f, 0.2f, 0, 0, 0, 13, 0.5f, 0.3927f, 0.1f, 0.8f},
        {"curved.up_1.0", 6, 0, 1.0f, 0.17f, 0, 0, 0, 16, 3.0f, 0.1f, 2.3f, 3.7f},
        {"curved.down_1.0", 6, 1, 1.0f, 0.17f, 0, 0, 0, 16, 3.0f, 0.1f, 2.3f, 3.7f},
        {"room.doorway_1.1", 0, 0, 1.1f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {"room.turnback_1.1", 0, 1, 1.1f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    for (const IS& c0 : sc)
        if (want(c0.name)) runIS(c0);
}

static void runIS(const IS& c0) {
    {
        Agg ag;
        int planted = 0, sunk = 0, sunk3 = 0, deep = 0, lifted = 0, quick = 0, inWall = 0;
        float halfHands = 0.f, halfFeet = 0.f;
        // (LM_IV: the pace on the stairs - the game's walkers keep their flat pace there; climbing one tread a step is
        // about 0.45-0.55 m/s along the flight)
        static const float kIV = getenv("LM_IV") ? (float)atof(getenv("LM_IV")) : -1.f;
        // (LM_IREP=n: each person n times - another animation seed, the start 7 cm further back each time, the pace 4 %
        // up or down - so that the gait meets the turns and the edges at other phases: a wider sample)
        static const int kRep = getenv("LM_IREP") ? Max(1, atoi(getenv("LM_IREP"))) : 1;
        for (int pr = 0; pr < kNP * kRep; pr++) {
            const int p = pr % kNP, rep = pr / kNP;
            const float vStairs = (kIV > 0.f && c0.kind != 0 ? kIV : c0.v) * (1.f + 0.04f * (float)((rep % 3) - (rep > 0 ? 1 : 0)));
            Tracker tr;
            Terrain T;
            T.kind = c0.kind;
            T.kRise = c0.rise;
            T.kN = c0.n;
            static const bool kSbWalls = getenv("LM_SBWALLS") != nullptr;
            T.walls = kSbWalls && c0.kind == 5;
            if (c0.kind == 6) {
                T.kSpiralA = c0.ang;
                T.kRin = c0.rIn;
                T.kRout = c0.rOut;
            } else {
                T.kTread = c0.tread;
                T.kW = c0.width;
                T.kLand = c0.land;
            }
            const float top = Terrain::kY0 + T.kN * T.kTread, xs = 0.5f * T.kW;
            PathFollow pf;
            const bool turnback = c0.kind == 0 && c0.dir == 1;
            if (c0.kind == 5) {
                const float yl = top + 0.5f * T.kLand, xa = -0.5f * T.kW, xb = 0.5f * T.kW, rr = Min(0.25f, 0.3f * T.kLand);
                pf.pts = {vec2(xa, Terrain::kY0 - 2.5f), vec2(xa, yl - rr), vec2(xa + 0.15f, yl + 0.4f * rr), vec2(0.f, yl + rr),
                          vec2(xb - 0.15f, yl + 0.4f * rr), vec2(xb, yl - rr), vec2(xb, Terrain::kY0 - 2.f)};
            } else if (c0.kind == 7 || c0.kind == 8) {
                const float yl = top + 0.5f * T.kW;
                pf.pts = {vec2(0.f, Terrain::kY0 - 2.5f), vec2(0.f, yl - 0.25f), vec2(0.12f, yl - 0.04f), vec2(0.35f, yl),
                          vec2(xs + T.kN * T.kTread + 2.f, yl)};
                if (c0.kind == 8) {   // (round the winders on the walking line, 0.45 m from the inner corner)
                    pf.pts = {vec2(0.f, Terrain::kY0 - 2.5f), vec2(0.f, top)};
                    const vec2 C(xs, top);
                    for (float ph = kPi - 0.15f; ph > 0.5f * kPi + 0.05f; ph -= 0.15f) pf.pts.push_back(C + vec2(cosf(ph), sinf(ph)) * xs);
                    pf.pts.push_back(vec2(xs, top + xs));
                    pf.pts.push_back(vec2(xs + T.kN * T.kTread + 2.f, top + xs));
                }
            } else if (c0.kind == 6) {
                const float R = c0.R, thEnd = T.kN * T.kSpiralA;
                pf.pts.push_back(vec2(R, -1.6f));
                const float dth = Min(0.15f, 0.12f / R);
                for (float th = 0.f; th < thEnd + 0.12f / R; th += dth) pf.pts.push_back(vec2(R * cosf(th), R * sinf(th)));
                const float thx = thEnd + Min(0.35f, 0.3f / R);
                pf.pts.push_back(vec2(R * cosf(thx), R * sinf(thx)));
                pf.pts.push_back(vec2((R + 2.f) * cosf(thx), (R + 2.f) * sinf(thx)));
            } else if (c0.kind == 3) {   // (a straight flight: up it to the middle of the 2 m landing, or from there down the other)
                const float land = top + 2.f;
                pf.pts = c0.dir == 0 ? std::vector<vec2>{vec2(0.f, Terrain::kY0 - 2.5f), vec2(0.f, top + 1.f)}
                                     : std::vector<vec2>{vec2(0.f, top + 1.f), vec2(0.f, land + T.kN * T.kTread + 2.5f)};
            } else if (!turnback) {
                pf.pts = {vec2(0.f, 0.f), vec2(0.f, 3.f), vec2(1.5f, 3.f), vec2(1.5f, 5.f)};   // (two doorways: right, then left)
            } else {
                pf.pts = {vec2(0.f, 0.f), vec2(0.f, 2.6f)};   // (across the room; then back along it)
            }
            if (c0.kind != 0 && c0.kind != 3 && c0.dir == 1) std::reverse(pf.pts.begin(), pf.pts.end());
            if (rep > 0 && pf.pts.size() >= 2) {   // (LM_IREP: the start further back along the first leg)
                const vec2 d0r = normalize(pf.pts[1] - pf.pts[0]);
                pf.pts[0] = pf.pts[0] - d0r * (0.07f * (float)rep);
            }
            pf.build();
            const vec2 st = pf.pts[0], d0 = normalize(pf.pts[1] - pf.pts[0]);
            const float yaw0 = atan2f(-d0.x, d0.y);
            T.zRef = T.kind == 6 && c0.dir == 1 ? T.kN * T.kRise : 0.f;   // (the start: on the floor above going down)
            float pause = -1.f;
            int leg = 0;   // (room.turnback: 0 there, 1 waiting at the wall, 2 back)
            bool stepping[2] = {false, false};
            int lab = 0;
            auto onStairs = [&](vec2 q) {
                if (c0.kind == 5) return q.y > Terrain::kY0 + 0.1f;
                if (c0.kind == 3) return c0.dir == 0 ? q.y > Terrain::kY0 + 0.1f : q.y < top + 2.f + T.kN * T.kTread + 0.3f;
                if (c0.kind == 7 || c0.kind == 8) return q.y > Terrain::kY0 + 0.1f && q.x < xs + T.kN * T.kTread - 0.1f;
                if (T.kind == 6) {
                    float th = atan2f(q.y, q.x);
                    if (th < 0.f) th += kTwoPi;
                    return length(q) < T.kRout + 0.1f && th < T.kN * T.kSpiralA + 0.3f;
                }
                return true;
            };
            runOne(kPeople[p], kPeople[p].seed * 7u + 91u + 1009u * (u32)rep, T, (T.kind == 6 ? 20.f : 16.f) + 0.2f * (float)rep, [&](float t, const Person& P, Ctl& c) {
                const vec2 pos(P.pos.x, P.pos.y);
                c.label = 0;
                lab = 0;
                if (t < 1.f) {
                    c.desired = vec2(0.f);
                    return;
                }
                if (turnback) {
                    const vec2 wall(0.f, 2.6f), back(0.f, 0.f);
                    if (leg == 0 && length(wall - pos) < 0.12f) leg = 1, pause = t;
                    if (leg == 1 && t - pause > 0.6f) leg = 2;
                    const vec2 to = (leg == 2 ? back : wall) - pos;
                    const float dl = length(to), v = leg == 1 ? 0.f : Min(c0.v, 1.2f * dl);
                    c.desired = dl > 0.03f ? to * (v / dl) : vec2(0.f);
                } else {
                    const float sN = pf.nearest(pos), aim = Min(sN + 0.6f, pf.total());
                    vec2 to = pf.at(aim) - pos;
                    // (a spiral or a curve: along its walking line - the way the line goes just ahead, steered back onto it;
                    // a pursuit 0.6 m ahead would cut inside a tight one. LM_PURSUIT: the pursuit)
                    static const bool kPursuit = getenv("LM_PURSUIT") != nullptr;
                    if (T.kind == 6 && !kPursuit) {
                        const vec2 a = pf.at(sN + 0.05f), b = pf.at(sN + 0.3f);
                        if (length(b - a) > 0.02f) to = normalize(b - a) + (pf.at(sN) - pos) * 3.f;
                    }
                    const float left = pf.total() - sN, dl = length(to), v = Min(vStairs, 1.2f * left);
                    c.desired = dl > 0.03f && left > 0.05f ? to * (v / dl) : vec2(0.f);
                }
                c.label = onStairs(pos) ? 20 : 0;
                lab = c.label;
            }, tr, [&](float tt, Person& P) {
                // (rooms: how far out to the side of the body the hands and the feet go - for the doors' width)
                if (c0.kind == 0 && tt > 1.f)
                    for (int s = 0; s < 2; s++) {
                        const int hb[4] = {s ? B_HAND_R : B_HAND_L, s ? B_FOREARM_R : B_FOREARM_L, s ? B_MIDDLE3_R : B_MIDDLE3_L, s ? B_THUMB3_R : B_THUMB3_L};
                        for (int k = 0; k < 4; k++) halfHands = Max(halfHands, fabsf(P.m[hb[k]].c[3].x));
                        FootW f;
                        footWorld(P, T, s, f);
                        for (int k = 0; k < 3; k++) halfFeet = Max(halfFeet, fabsf(dot(vec2(f.p[k].x - P.pos.x, f.p[k].y - P.pos.y), vec2(cosf(P.yaw), sinf(P.yaw)))));
                    }
                for (int s = 0; s < 2; s++) {
                    const bool stp = P.an.stepT[s] >= 0.f;
                    if (stp && !stepping[s] && lab == 20) quick++;
                    stepping[s] = stp;
                    if (lab != 20 || !P.an.planted[s]) continue;
                    FootW f;
                    footWorld(P, T, s, f);
                    const float low = soleClear(f, T);
                    // LM_IEV: each planted frame on the stairs sunk past 3 cm or lifted past 3 cm (where, how, doing what)
                    static const bool kIev = getenv("LM_IEV") != nullptr;
                    static const float kIevT = getenv("LM_IEVT") ? 0.01f * (float)atof(getenv("LM_IEVT")) : 0.03f;   // (LM_IEVT=<cm>: the threshold)
                    if (kIev && (low < -kIevT || low > kIevT))
                        fprintf(stderr, "IEV %s p%d run %d t%.3f foot %d low %5.1f h%5.1f b%5.1f t%5.1f at %6.2f,%6.2f fy %6.1f gz %5.2f/%5.2f/%5.2f age %.2f st %5.2f v %.2f tr %5.2f yaw %6.1f\n",
                                c0.name, p, gRun - 1, tt, s, low * 100.f, (f.p[0].z - f.gz[0]) * 100.f, (f.p[1].z - f.gz[1]) * 100.f, (f.p[2].z - f.gz[2]) * 100.f,
                                f.p[0].x, f.p[0].y, f.yaw * 57.3f, f.gz[0], f.gz[1], f.gz[2], P.an.plantAge[s], P.an.stepT[s], length(P.vel), P.turnRate, P.yaw * 57.3f);
                    planted++;
                    if (low < -0.01f) sunk++;
                    if (low < -0.03f) sunk3++;
                    if (c0.kind != 0 && low < -T.kRise - 0.01f) deep++;
                    if (low > 0.015f) lifted++;
                    // (LM_SBWALLS: a planted foot with its heel, ball or toe tip in a wall's footprint)
                    if (T.walls && (T.inWall(f.p[0].x, f.p[0].y) || T.inWall(f.p[1].x, f.p[1].y) || T.inWall(f.p[2].x, f.p[2].y))) {
                        inWall++;
                        static const bool kWlog = getenv("LM_WALLLOG") != nullptr;
                        if (kWlog)
                            fprintf(stderr, "INWALL %s p%d run %d t%.3f foot %d | heel %5.2f %5.2f %+6.1f ball %5.2f %5.2f %+6.1f toe %5.2f %5.2f %+6.1f | root %5.2f %5.2f z%.2f v %.2f yaw %6.1f tr %5.2f | age %.2f\n",
                                    c0.name, p, gRun - 1, tt, s, f.p[0].x, f.p[0].y, (f.p[0].z - f.gz[0]) * 100.f, f.p[1].x, f.p[1].y, (f.p[1].z - f.gz[1]) * 100.f, f.p[2].x, f.p[2].y,
                                    (f.p[2].z - f.gz[2]) * 100.f, P.pos.x, P.pos.y, P.pos.z, length(P.vel), P.yaw * 57.3f, P.turnRate, P.an.plantAge[s]);
                    }
                }
            }, yaw0, true, st);
            ag.addTracker(tr, 20);
            ag.nPeople++;
        }
        ag.report(c0.name);
        M(std::string(c0.name) + ".planted_sunk_pct", planted ? 100.f * sunk / planted : 0.f);
        M(std::string(c0.name) + ".planted_sunk_3cm_pct", planted ? 100.f * sunk3 / planted : 0.f);
        M(std::string(c0.name) + ".planted_deep_frames", (float)deep);
        M(std::string(c0.name) + ".planted_lifted_pct", planted ? 100.f * lifted / planted : 0.f);
        M(std::string(c0.name) + ".quick_steps", (float)quick);
        if (c0.kind == 5) M(std::string(c0.name) + ".planted_in_wall_frames", (float)inWall);
        if (c0.kind == 0) {
            M(std::string(c0.name) + ".half_width_hands_cm", halfHands * 100.f);
            M(std::string(c0.name) + ".half_width_feet_cm", halfFeet * 100.f);
        }
    }
}

// LM_KIT=1: the building kit's sweep - which stair geometry the feet handle well: straight flights by riser and tread,
// switchback landings by depth, L-stair landings and winders by the flight's width, spirals by the walking line's
// radius, the tread's angle and the riser, curved stairs by radius (kit.* scenarios, up and down)
static void kit() {
    if (!getenv("LM_KIT")) return;
    static std::vector<std::string> names;   // (the scenarios keep their names' text)
    std::vector<IS> v;
    names.reserve(1000);
    auto add = [&](const std::string& nm, IS c) {
        names.push_back(nm);
        c.name = names.back().c_str();
        if (want(c.name)) v.push_back(c);
    };
    char b[128];
    const char* ud[2] = {"up", "down"};
    for (float r : {0.15f, 0.165f, 0.18f, 0.195f, 0.21f, 0.225f})
        for (float t : {0.24f, 0.26f, 0.28f, 0.30f, 0.33f})
            for (int d = 0; d < 2; d++) {
                snprintf(b, sizeof b, "kit.straight.r%.3f_t%.2f.%s", r, t, ud[d]);
                add(b, {nullptr, 3, d, 1.0f, r, t, 1.2f, 2.f, 10, 0, 0, 0, 0});
            }
    for (float w : {0.9f, 1.0f, 1.2f})
        for (float l : {0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.4f})
            for (int d = 0; d < 2; d++) {
                if (l < w - 0.25f) continue;
                snprintf(b, sizeof b, "kit.switchback.w%.1f_l%.1f.%s", w, l, ud[d]);
                add(b, {nullptr, 5, d, 1.0f, 0.175f, 0.28f, w, l, 9, 0, 0, 0, 0});
            }
    for (float w : {0.8f, 0.9f, 1.0f, 1.2f})
        for (int d = 0; d < 2; d++) {
            snprintf(b, sizeof b, "kit.lstair.w%.1f.%s", w, ud[d]);
            add(b, {nullptr, 7, d, 1.0f, 0.175f, 0.28f, w, w, 8, 0, 0, 0, 0});
            snprintf(b, sizeof b, "kit.winder.w%.1f.%s", w, ud[d]);
            add(b, {nullptr, 8, d, 0.9f, 0.18f, 0.28f, w, w, 7, 0, 0, 0, 0});
        }
    for (float R : {0.45f, 0.5f, 0.55f, 0.6f, 0.7f, 0.8f})
        for (float deg : {22.5f, 26.f, 30.f})
            for (float r : {0.19f, 0.22f})
                for (int d = 0; d < 2; d++) {
                    const float a = deg * kPi / 180.f;
                    snprintf(b, sizeof b, "kit.spiral.R%.2f_a%.1f_r%.2f.%s", R, deg, r, ud[d]);
                    add(b, {nullptr, 6, d, 0.65f, r, 0, 0, 0, (int)(5.0f / a), R, a, 0.1f, R + 0.3f});
                }
    for (float R : {1.0f, 1.5f, 2.0f, 3.0f, 4.0f})
        for (int d = 0; d < 2; d++) {
            snprintf(b, sizeof b, "kit.curved.R%.1f.%s", R, ud[d]);
            add(b, {nullptr, 6, d, 1.0f, 0.17f, 0, 0, 0, 14, R, 0.28f / R, R - 0.6f, R + 0.6f});
        }
    for (const IS& c : v) runIS(c);
}

// LM_REPLAY=<locotour log>: a ped's own motion from the locotour's per-frame trace (LOCO_TRACEPED) played to each
// person - the yaw, the turn rate and the speed and way the animator was given, the root's move (the logged
// position, to the centimetre, eased along the logged velocity) - on flat ground. LM_REPLAY_UID (13), LM_REPLAY_T0 /
// LM_REPLAY_T1 (the tour's time window; measured from 1.5 s in), LM_REPLAY_WHO (one person). Run at the trace's rate
// (LM_HZ=30). The log's own walk weight and phase print beside the replay's with LM_TRACE
static void replay() {
    const char* fn = getenv("LM_REPLAY");
    if (!fn || !want("replay")) return;
    const unsigned uid = getenv("LM_REPLAY_UID") ? (unsigned)atoi(getenv("LM_REPLAY_UID")) : 13u;
    const float w0 = getenv("LM_REPLAY_T0") ? (float)atof(getenv("LM_REPLAY_T0")) : -1e9f, w1 = getenv("LM_REPLAY_T1") ? (float)atof(getenv("LM_REPLAY_T1")) : 1e9f;
    const int who = getenv("LM_REPLAY_WHO") ? atoi(getenv("LM_REPLAY_WHO")) : -1;
    struct Fr {
        float t, x, y, v, f, s, yaw, tr, in, mdx, mdy, mw, ph;
        int stance;
    };
    std::vector<Fr> fr;
    FILE* fp = fopen(fn, "r");
    if (!fp) {
        printf("replay: cannot open %s\n", fn);
        return;
    }
    static char line[8192];
    while (fgets(line, sizeof line, fp)) {
        const char* q = strstr(line, "locotour trace ");
        if (!q) continue;
        unsigned u = 0;
        Fr r;
        if (sscanf(q, "locotour trace %u t%f pos %f %f v %f (f%f s%f) yaw %f tr%f in %f md %f,%f mw %f ph %f stance %d", &u, &r.t, &r.x, &r.y, &r.v, &r.f, &r.s,
                   &r.yaw, &r.tr, &r.in, &r.mdx, &r.mdy, &r.mw, &r.ph, &r.stance) != 15 ||
            u != uid || r.t < w0 || r.t > w1)
            continue;
        if (!fr.empty() && r.t <= fr.back().t + 1e-4f) continue;
        if (!fr.empty() && r.t > fr.back().t + 1.5f * kDt) break;   // (one stretch of consecutive frames)
        fr.push_back(r);
    }
    fclose(fp);
    if (fr.size() < 60) {
        printf("replay: %d frames of ped %u\n", (int)fr.size(), uid);
        return;
    }
    printf("replay: ped %u, %d frames, t %.3f - %.3f\n", uid, (int)fr.size(), fr.front().t, fr.back().t);
    Agg ag;
    for (int p = 0; p < kNP; p++) {
        if (who >= 0 && p != who) continue;
        Tracker tr;
        Terrain T;
        std::unique_ptr<Person> P = makePerson(kPeople[p], kPeople[p].seed * 7u + 81u);
        gTraceNow = gTraceOn && gRun == gTraceRun;
        gRun++;
        const float x0 = fr[0].x, y0 = fr[0].y;
        P->yaw = fr[0].yaw * kPi / 180.f;
        P->pos = vec3(0.f);
        for (size_t k = 0; k < fr.size(); k++) {
            const Fr& r = fr[k];
            const float yaw = wrapA(r.yaw * kPi / 180.f);
            const vec2 fwd(-sinf(yaw), cosf(yaw)), rt(cosf(yaw), sinf(yaw));
            // (the position: along the logged velocity, pulled onto the logged centimetres)
            const vec2 vl = fwd * r.f + rt * r.s, want(r.x - x0, r.y - y0);
            vec2 np = vec2(P->pos.x, P->pos.y) + vl * kDt;
            np = np + (want - np) * 0.25f;
            P->pos.x = np.x;
            P->pos.y = np.y;
            P->yaw = yaw;
            P->turnRate = r.tr;
            // (the animator's speed and way as logged: a velocity of that length that way)
            const vec2 md = length(vec2(r.mdx, r.mdy)) > 0.5f ? normalize(vec2(r.mdx, r.mdy)) : vec2(0.f, 1.f);
            P->vel = (rt * md.x + fwd * md.y) * r.in;
            P->in.stance = r.stance;
            animate(*P, T);
            const float t = r.t - fr[0].t;
            tr.add(*P, T, t, t >= 1.5f ? 14 : 0);
            if (gTraceNow) fprintf(stderr, "REPLAY t%.3f tour %.3f mw %.2f/%.2f ph %.3f/%.3f snap %.2f\n", t, r.t, P->an.moveW, r.mw, P->an.phase, r.ph, P->an.snapW);
        }
        tr.finish(fr.back().t - fr[0].t);
        gTraceNow = false;
        ag.addTracker(tr, 14);
        ag.nPeople++;
    }
    ag.report("replay");
}

int main(int argc, char** argv) {
    gFilter = argc > 1 ? argv[1] : nullptr;
    gTraceOn = getenv("LM_TRACE") != nullptr;
    if (gTraceOn) gTraceRun = atoi(getenv("LM_TRACE"));
    gSteps = getenv("LM_STEPS") != nullptr;
    double t0 = TimeSeconds();
    steady();
    agait();
    slopes();
    startsStops();
    turns();
    startStopTurns();
    interior();
    kit();
    replay();
    passing();
    crowd();
    lookat();
    pushes();
    printf("time %.1f s\n", TimeSeconds() - t0);
    printf("animator %.3f us per update over %ld updates\n", gAnimN ? gAnimSec * 1e6 / gAnimN : 0.0, gAnimN);
    printf("ground scans %ld (one per %.0f updates)\n", gScans, gScans ? (double)gAnimN / gScans : 0.0);
    return 0;
}
