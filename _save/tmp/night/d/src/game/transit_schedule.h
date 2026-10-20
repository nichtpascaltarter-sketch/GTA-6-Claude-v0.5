// SkyLine timetable math (pure, no engine dependencies): per service direction a speed profile over the loop (line
// speed, curve limits with cant, acceleration and braking into every station, dwells) and the train state as a function
// of the transit clock. Shared by game/transit_game.cpp and the native test tests/transit/test_transit.cpp.
#pragma once
#include "../world/transit.h"

namespace Game {
namespace Transit {
namespace tsched {

using namespace World::transit_dims;

constexpr float kAccel = 1.0f, kDecel = 1.1f, kLineSpeed = 22.f;

// ---- timetable of one service direction ------------------------------------------------------------------------------
struct Profile {
    int dir = 0;                  // 0 outer track counter-clockwise (s increasing), 1 inner track clockwise
    float q0s = 0.f;              // corridor s at q = 0 (the first station of this direction)
    int len = 0;                  // loop length in whole meters (q samples)
    std::vector<float> v;         // speed at q (m/s)
    std::vector<float> tq;        // time the train center passes q (arrivals at stations, dwells included before)
    std::vector<int> stQ;         // q of each station in service order
    std::vector<int> stIdx;       // MetroLine station index in service order
    std::vector<float> arr, dep;  // arrival / departure times in the cycle
    std::vector<int> stopAt;      // q -> service-order station index or -1
    float cycle = 0.f;
    float sOf(float q) const {
        const World::MetroLine& L = World::gTransit->metro;
        return L.wrap(dir == 0 ? q0s + q : q0s - q);
    }
    float lateral() const { return dir == 0 ? kTrackOffset : -kTrackOffset; }
};

struct TrainState {
    float q = 0.f, v = 0.f, a = 0.f;
    int stop = -1;          // service-order station while dwelling
    float dwellT = 0.f, dwell = 0.f;
    int next = 0;           // next service-order station
    float toNext = 0.f;     // seconds to the next arrival
    float doors = 0.f;      // 0 closed .. 1 open
};

// ---------------------------------------------------------------------------------------------------- profiles
inline void buildProfile(Profile& P, int dir) {
    const World::MetroLine& L = World::gTransit->metro;
    P.dir = dir;
    int nS = (int)L.stations.size();
    P.q0s = L.stations[0].s;
    P.len = (int)floorf(L.length);
    int n = P.len;
    // service order and station q
    P.stQ.clear();
    P.stIdx.clear();
    for (int k = 0; k < nS; k++) {
        int i = dir == 0 ? k : (nS - k) % nS;
        float q = dir == 0 ? L.wrap(L.stations[i].s - P.q0s) : L.wrap(P.q0s - L.stations[i].s);
        P.stQ.push_back(Clamp((int)roundf(q), 0, n - 1));
        P.stIdx.push_back(i);
    }
    P.stopAt.assign(n + 1, -1);
    for (int k = 0; k < nS; k++) P.stopAt[P.stQ[k]] = k;
    // curve speed limit over the whole train (cant allows a little more lateral acceleration)
    std::vector<float> lim(n + 1);
    for (int q = 0; q <= n; q++) {
        vec2 pos, tan;
        float z, bk, k;
        L.frame(P.sOf((float)q), pos, tan, &z, &bk, &k);
        float ak = fabsf(k);
        lim[q] = ak > 1e-4f ? Min(kLineSpeed, sqrtf((0.95f + 9.81f * fabsf(bk)) / ak)) : kLineSpeed;
    }
    std::vector<float> lim2(n + 1);
    for (int q = 0; q <= n; q++) {
        float m = lim[q];
        for (int d = -28; d <= 28; d += 4) m = Min(m, lim[((q + d) % n + n) % n]);
        lim2[q] = m;
    }
    P.v.assign(n + 1, 0.f);
    for (int q = 0; q <= n; q++) P.v[q] = P.stopAt[q % n] >= 0 ? 0.f : lim2[q];
    for (int pass = 0; pass < 2; pass++) {
        for (int q = 0; q < n; q++) {
            int q1 = q + 1;
            if (P.stopAt[q1 % n] >= 0) continue;
            P.v[q1] = Min(P.v[q1], sqrtf(P.v[q] * P.v[q] + 2.f * kAccel));
        }
        P.v[n] = P.v[0];
        for (int q = n; q > 0; q--) {
            int q0 = q - 1;
            if (P.stopAt[q0 % n] >= 0) continue;
            P.v[q0] = Min(P.v[q0], sqrtf(P.v[q] * P.v[q] + 2.f * kDecel));
        }
        P.v[0] = P.v[n] = 0.f;
    }
    // times (arrival at station 0 = t 0, its dwell first)
    P.tq.assign(n + 1, 0.f);
    P.arr.assign(nS, 0.f);
    P.dep.assign(nS, 0.f);
    float t = 0.f;
    for (int q = 0; q <= n; q++) {
        int si = P.stopAt[q % n];
        if (q < n) {
            if (si >= 0) {
                P.arr[si] = t;
                t += L.stations[P.stIdx[si]].dwell;
                P.dep[si] = t;
            }
        }
        P.tq[q] = si >= 0 && q < n ? P.arr[si] : t;
        if (q < n) t += 2.f / Max(P.v[q] + P.v[q + 1], 0.02f);
    }
    P.cycle = t;
}

// State at clock tau (cycle relative)
inline TrainState stateAt(const Profile& P, float tau) {
    TrainState s;
    const World::MetroLine& L = World::gTransit->metro;
    tau = fmodf(tau, P.cycle);
    if (tau < 0.f) tau += P.cycle;
    int nS = (int)P.stQ.size();
    for (int i = 0; i < nS; i++)
        if (tau >= P.arr[i] && tau < P.dep[i]) {
            s.q = (float)P.stQ[i];
            s.v = 0.f;
            s.stop = i;
            s.dwellT = tau - P.arr[i];
            s.dwell = P.dep[i] - P.arr[i];
            s.next = (i + 1) % nS;
            s.toNext = P.arr[s.next] - tau;
            if (s.toNext < 0.f) s.toNext += P.cycle;
            float D = s.dwell, t = s.dwellT;
            s.doors = t < D - 4.3f ? SmoothStep(1.2f, 3.0f, t) : 1.f - SmoothStep(D - 4.3f, D - 2.4f, t);
            return s;
        }
    // running: last q whose pass time <= tau
    int lo = 0, hi = P.len;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (P.tq[mid] <= tau) lo = mid;
        else hi = mid;
    }
    int si = P.stopAt[lo % P.len];
    float t0 = si >= 0 ? P.dep[si] : P.tq[lo];
    float t1 = P.tq[lo + 1];
    float f = Saturate((tau - t0) / Max(t1 - t0, 1e-4f));
    s.q = (float)lo + f;
    s.v = Lerp(P.v[lo], P.v[lo + 1], f);
    s.a = (P.v[lo + 1] - P.v[lo]) * Lerp(P.v[lo], P.v[lo + 1], f);   // dv/dt = v dv/dq
    int nxt = 0;
    for (int i = 0; i < nS; i++)
        if (P.stQ[i] > lo) {
            nxt = i;
            break;
        }
    if (P.stQ[nxt] <= lo) nxt = 0;
    s.next = nxt;
    s.toNext = P.arr[nxt] - tau;
    if (s.toNext < 0.f) s.toNext += P.cycle;
    (void)L;
    return s;
}

}  // namespace tsched
}  // namespace Transit
}  // namespace Game
