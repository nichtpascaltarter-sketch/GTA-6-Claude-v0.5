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
            float low = 1e9f;
            for (int k = 0; k < 3; k++) {
                float hgt = f.p[k].z - f.gz[k];
                down[k] = hgt < kDown;
                low = Min(low, hgt);
            }
            lowest[label] = Min(lowest[label], low);
            // a foot the animator holds planted but shown off the ground
            if (P.an.planted[s] && low > 0.015f) {
                floatFrames[label]++;
                floatMax[label] = Max(floatMax[label], low);
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
                fprintf(stderr, "pc%.3f cy%.2f k%d | ", length(P.an.plantCorr[s]), P.an.corrYaw[s], P.an.dbgPlantKind[s]);
#else
                fprintf(stderr, "pc%.3f cy%.2f k0 | ", length(P.an.plantCorr[s]), P.an.corrYaw[s]);
#endif
                if (s == 1)
                    fprintf(stderr, "t%.3f L%d yaw%6.1f tr%5.2f v%4.2f md%5.2f,%5.2f ph%.3f mw%.2f lag%5.2f ht%5.2f\n", t, label, P.yaw * 57.3f, P.turnRate,
                            P.in.speed, P.in.localMoveDir.x, P.in.localMoveDir.y, P.an.phase, P.an.moveW, P.an.bodyLag, P.an.hipTurn);
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
    int label = 0;
};
using Script = std::function<void(float t, const Person& P, Ctl& c)>;

static void runOne(const Who& w, u32 animSeed, const Terrain& T, float secs, const Script& sc, Tracker& tr,
                   const std::function<void(float, Person&)>& extra = nullptr, float yaw0 = 0.f, bool knownRate = true) {
    std::unique_ptr<Person> P = makePerson(w, animSeed);
    gTraceNow = gTraceOn && gRun == gTraceRun;
    gScanLog = gTraceNow;
    gRun++;
    P->yaw = yaw0;
    P->turnRateKnown = knownRate;
    P->pos.z = T.h(0.f, 0.f);
    const int n = (int)(secs / kDt);
    for (int f = 0; f < n; f++) {
        float t = f * kDt;
        Ctl c;
        sc(t, *P, c);
        control(*P, c.desired, c.face ? &c.faceYaw : nullptr, c.rate, T, c.hold);
        animate(*P, T);
        tr.add(*P, T, t, c.label);
        if (extra) extra(t, *P);
    }
    tr.finish(secs);
    gTraceNow = false;
}

static vec2 dirOf(float yaw) { return vec2(-sinf(yaw), cosf(yaw)); }

static void steady() {
    const float speeds[] = {0.9f, 1.4f, 2.0f, 3.0f, 5.0f, 7.0f};
    const char* names[] = {"walk_slow_0.9", "walk_1.4", "walk_fast_2.0", "jog_3.0", "run_5.0", "sprint_7.0"};
    for (int i = 0; i < 6; i++) {
        std::string k = std::string("flat.") + names[i];
        if (!want(k.c_str())) continue;
        Agg ag;
        for (int p = 0; p < kNP; p++) {
            Tracker tr;
            Terrain T;
            float v = speeds[i];
            runOne(kPeople[p], kPeople[p].seed * 7u + 1u, T, 8.f, [&](float t, const Person&, Ctl& c) {
                c.desired = vec2(0.f, v);
                c.label = t > 2.5f ? 1 : 0;
            }, tr);
            ag.addTracker(tr, 1);
            ag.nPeople++;
        }
        ag.report(k);
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
        static const SC kSC[4] = {{"stairs.walk_1.3", 0.16f, 0.3f, 12, 0}, {"stairs17.walk", 0.17f, 0.29f, 17, 1},
                                  {"stairs17.crowd", 0.17f, 0.29f, 17, 2}, {"stairs17.blocked", 0.17f, 0.29f, 17, 3}};
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
            // (the crowd case four times over, its brakes and weaves at other moments: more landings to count)
            for (int pp = 0; pp < kNP * (sc.paced >= 2 ? 4 : 1); pp++) {
                const int p = pp % kNP, rep = pp / kNP;
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
                const float v = sc.paced ? 1.2f + 0.05f * (float)p : 1.3f;
                runOne(kPeople[p], kPeople[p].seed * 7u + 1u + (u32)rep, T, 4.f + (2.f * sc.n * sc.tread + 2.f) / v * (sc.paced >= 2 ? 1.3f : 1.f), [&](float t, const Person& P, Ctl& c) {
                    c.desired = vec2(0.f, Min(v, t * 11.f));
                    if (sc.paced == 2 && t > 2.f) {
                        const float u = fmodf(t + 0.37f * (float)p + 0.55f * (float)rep, 2.2f);
                        auto ss = [](float a, float b, float x) { float q = Saturate((x - a) / (b - a)); return q * q * (3.f - 2.f * q); };
                        const float brake = ss(0.f, 0.15f, u) * (1.f - ss(0.6f, 0.75f, u));
                        c.desired = vec2(0.3f * sinf(t * 2.33f + (float)p + 1.7f * (float)rep), v * (1.f - 0.45f * brake));
                    }
                    if (sc.paced == 3 && t > 2.f) {
                        const float u = fmodf(t + 0.43f * (float)p + 0.65f * (float)rep, 2.6f);
                        c.hold = u < 0.5f ? 0.2f : 1.f;
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
                    for (int s = 0; s < 2; s++) {
                        const bool st = P.an.stepT[s] >= 0.f;
                        if (st && !stepping[s] && (lab == 5 || lab == 6)) quick[lab == 5 ? 0 : 1]++;
                        stepping[s] = st;
                    }
                    if (lab != 5 && lab != 6) return;
                    const int w = lab == 5 ? 0 : 1;
                    for (int s = 0; s < 2; s++) {
                        if (!P.an.planted[s]) continue;
                        FootW f;
                        footWorld(P, T, s, f);
                        downN[w]++;
                        float low = 1e9f, high = -1e9f;
                        for (int q = 0; q < 3; q++) {
                            float h = f.p[q].z - f.gz[q];
                            low = Min(low, h);
                            high = Max(high, h);
                        }
                        if (fabsf(f.gz[0] - f.gz[2]) > 0.03f) across[w]++;
                        if (low < -0.01f) sunk[w]++;
                        if (low < -0.03f) sunk3[w]++;
                        if (low < -sc.rise - 0.01f) deep[w]++;
                        if (low < -0.03f && f.p[0].z - f.gz[0] > 0.02f && f.p[2].z - f.gz[2] < -0.03f && f.p[1].z - f.gz[1] > -0.01f) sunkHeelUp[w]++;
                        if (low > 0.015f) lifted[w]++;
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

int main(int argc, char** argv) {
    gFilter = argc > 1 ? argv[1] : nullptr;
    gTraceOn = getenv("LM_TRACE") != nullptr;
    if (gTraceOn) gTraceRun = atoi(getenv("LM_TRACE"));
    gSteps = getenv("LM_STEPS") != nullptr;
    double t0 = TimeSeconds();
    steady();
    slopes();
    startsStops();
    turns();
    passing();
    crowd();
    lookat();
    pushes();
    printf("time %.1f s\n", TimeSeconds() - t0);
    printf("animator %.3f us per update over %ld updates\n", gAnimN ? gAnimSec * 1e6 / gAnimN : 0.0, gAnimN);
    printf("ground scans %ld (one per %.0f updates)\n", gScans, gScans ? (double)gAnimN / gScans : 0.0);
    return 0;
}
