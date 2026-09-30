// Pedestrian sidewalk navigation: walkers follow sidewalks, turn corners and cross streets on crosswalks (waiting for
// the walk signal at signalized intersections, for a gap in traffic elsewhere), with personal lateral preferences,
// destination bias and social-force avoidance of other pedestrians, the player and vehicles.
#include "ai_core.h"

namespace AI {

bool PedCore::place(Walker& w, vec2 p, u32 seed, float maxDist) {
    float x = 0.f, lat = 0.f;
    int l = g->nearestWalk(p, maxDist, &x, &lat);
    if (l < 0) return false;
    const WalkLink& L = g->walkLinks[l];
    w = Walker();
    w.seed = seed;
    w.link = l;
    w.fromA = (hash32(seed) & 1) != 0;
    w.x = w.fromA ? x : L.length - x;
    float hw = L.halfWidth;
    w.lat = (hashToFloat(hash32(seed * 3u + 1u)) * 2.f - 1.f) * hw * 0.75f;
    w.speed = 1.15f + hashToFloat(hash32(seed * 5u + 2u)) * 0.4f;
    w.jaywalker = hashToFloat(hash32(seed * 7u + 3u)) < 0.06f;
    w.state = WS_WALK;
    return true;
}

int PedCore::endNode(const Walker& w) const {
    if (w.link < 0) return -1;
    const WalkLink& L = g->walkLinks[w.link];
    return w.fromA ? L.b : L.a;
}

float PedCore::linkRemaining(const Walker& w) const {
    if (w.link < 0) return 0.f;
    return g->walkLinks[w.link].length - w.x;
}

vec2 PedCore::target(const Walker& w, float ahead) const {
    const WalkLink& L = g->walkLinks[w.link];
    float x = Min(w.x + ahead, L.length);
    return g->walkPos(w.link, x, w.lat, w.fromA).xy();
}

bool PedCore::crossingClear(const Walker& w, int link) const {
    const WalkLink& L = g->walkLinks[link];
    if (L.kind == WL_ZEBRA) {
        // a zebra: the traffic stops for anyone on it (traffic_core.cpp) - step off once whatever is coming can stop in
        // time, nothing is rolling over the stripes and the car nearest has seen us (a moment at the kerb)
        if (!traffic) return true;
        if (w.waitTimer < 0.8f) return false;
        vec2 a = g->walkNodes[L.a].p.xy(), b = g->walkNodes[L.b].p.xy();
        vec2 c = (a + b) * 0.5f;
        vec2 across = normalize(b - a + vec2(1e-4f, 0.f));
        vec2 along = vec2(-across.y, across.x);
        float halfW = L.length * 0.5f + 1.f;
        bool clear = true;
        float r = 60.f;
        traffic->hash.query(traffic->bodies, c - vec2(r), c + vec2(r), [&](int bi) {
            if (!clear) return;
            const Body& bd = traffic->bodies[bi];
            if (bd.kind != BK_CAR) return;
            vec2 rel = bd.pos - c;
            if (fabsf(dot(rel, across)) > halfW + 1.f) return;
            float lon = dot(rel, along), vlon = dot(bd.vel, along);
            float dist = fabsf(lon) - bd.halfLen - L.halfWidth - 0.5f;
            float closing = lon > 0.f ? -vlon : vlon;
            if (dist < 1.f && bd.speed > 0.5f) clear = false;                                         // on the stripes, moving
            else if (closing > 1.f && dist < closing * closing / (2.f * 3.5f) + closing * 0.6f + 2.f) clear = false;   // could not stop
            // a car already waiting at the stripes goes first once they are clear (people take turns with the traffic
            // instead of streaming over for ever) - unless it has been standing there a long while (a jam: walk past it)
            else if (dist < 2.5f && bd.speed < 0.5f && w.waitTimer < 12.f) clear = false;
        });
        return clear;
    }
    if (L.kind != WL_CROSSWALK) return true;
    PedSignal ps = g->pedSignal(L.node, L.approach, time);
    if (ps == PED_WALK) return true;
    bool gapNeeded = ps == PED_UNCONTROLLED || w.jaywalker;
    if (!gapNeeded) return false;
    if (!traffic) return true;
    // traffic gap: no vehicle reaches the crosswalk while we cross
    const WalkNode& A = g->walkNodes[L.a];
    const WalkNode& B = g->walkNodes[L.b];
    vec2 c = (A.p.xy() + B.p.xy()) * 0.5f;
    vec2 across = normalize(B.p.xy() - A.p.xy());
    vec2 along = vec2(-across.y, across.x);   // road direction
    float crossTime = L.length / Max(w.speed * 1.25f, 0.8f) + 1.5f;
    float halfW = L.length * 0.5f + 1.f;
    bool clear = true;
    float r = 70.f;
    traffic->hash.query(traffic->bodies, c - vec2(r), c + vec2(r), [&](int bi) {
        if (!clear) return;
        const Body& b = traffic->bodies[bi];
        if (b.kind != BK_CAR) return;
        vec2 rel = b.pos - c;
        if (fabsf(rel.x) > r || fabsf(rel.y) > r) return;
        float lat = dot(rel, across);
        float lon = dot(rel, along);
        if (fabsf(lat) > halfW + 1.f) return;
        float vlon = dot(b.vel, along);
        // distance to the crossing line along the road, closing speed
        float dist = fabsf(lon) - b.halfLen - 1.5f;
        float closing = lon > 0.f ? -vlon : vlon;
        if (dist < 1.5f && b.speed > 0.6f) clear = false;     // on the crosswalk and moving
        else if (closing > 1.0f && dist / closing < crossTime) clear = false;
    });
    return clear;
}

void PedCore::chooseNext(Walker& w, int nodeId) {
    const WalkNode& N = g->walkNodes[nodeId];
    int came = w.link;
    int opts[12];
    float wts[12];
    int n = 0;
    for (int l : N.links) {
        if (n >= 12) break;
        if (l == came && N.links.size() > 1) continue;
        const WalkLink& L = g->walkLinks[l];
        bool crossing = L.kind == WL_CROSSWALK || L.kind == WL_ZEBRA;
        float wt = L.kind == WL_SIDEWALK || L.kind == WL_PATH ? 1.f : (L.kind == WL_CORNER ? 0.75f : (L.kind == WL_ZEBRA ? 0.6f : 0.4f));
        if (crossing && w.avoidCrossing) wt *= 0.05f;
        // avoid immediately walking back across the street we just crossed
        if (crossing && came >= 0 && (g->walkLinks[came].kind == WL_CROSSWALK || g->walkLinks[came].kind == WL_ZEBRA)) wt *= 0.1f;
        if (w.hasDest) {
            int other = L.a == nodeId ? L.b : L.a;
            float d0 = length(N.p.xy() - w.dest), d1 = length(g->walkNodes[other].p.xy() - w.dest);
            wt *= d1 < d0 - 0.5f ? 6.f : 0.4f;
        }
        opts[n] = l;
        wts[n] = wt;
        n++;
    }
    if (n == 0) {
        // dead end of the network: turn around
        w.fromA = !w.fromA;
        w.x = 0.f;
        w.lat = -w.lat;
        return;
    }
    float total = 0.f;
    for (int i = 0; i < n; i++) total += wts[i];
    w.seed = hash32(w.seed + 0x9e37u);
    float r = hashToFloat(w.seed) * total;
    int pick = opts[n - 1];
    for (int i = 0; i < n; i++) {
        r -= wts[i];
        if (r <= 0.f) {
            pick = opts[i];
            break;
        }
    }
    const WalkLink& P = g->walkLinks[pick];
    w.prevNode = nodeId;
    w.link = pick;
    w.fromA = P.a == nodeId;
    w.x = 0.f;
    if (P.kind == WL_CROSSWALK || P.kind == WL_ZEBRA) {
        w.state = WS_WAIT_CROSS;
        w.waitTimer = 0.f;
        w.lat = Clamp(w.lat, -P.halfWidth, P.halfWidth);
    } else {
        w.state = WS_WALK;
        w.lat = Clamp(w.lat, -P.halfWidth, P.halfWidth);
        w.hurry = 1.f;
    }
}

vec2 PedCore::step(Walker& w, vec2 pos, float dt, int selfBody, float* faceYaw) {
    if (w.link < 0) return vec2(0, 0);
    const WalkLink& L = g->walkLinks[w.link];
    vec2 desired(0, 0);
    // follow the link: advance x only while close to the path point
    vec2 onPath = g->walkPos(w.link, w.x, w.lat, w.fromA).xy();
    float off = length(pos - onPath);
    if (w.state == WS_WAIT_CROSS) {
        // stand at the curb facing the road
        w.waitTimer += dt;
        vec2 face = g->walkTangent(w.link, 0.f, w.fromA);
        if (faceYaw) *faceYaw = dirYaw(face);
        vec2 to = onPath - pos;
        if (length(to) > 0.4f) desired = normalize(to) * 0.8f;
        if (crossingClear(w, w.link)) {
            w.state = WS_CROSSING;
            w.hurry = 1.2f;
        } else if (w.waitTimer > 45.f) {
            // give up and walk elsewhere
            int node = w.fromA ? L.a : L.b;
            w.avoidCrossing = true;
            w.link = -1;
            for (int l : g->walkNodes[node].links)
                if (g->walkLinks[l].kind != WL_CROSSWALK && g->walkLinks[l].kind != WL_ZEBRA) {
                    w.link = l;
                    w.fromA = g->walkLinks[l].a == node;
                    w.x = 0.f;
                    w.state = WS_WALK;
                    break;
                }
            if (w.link < 0) {
                w.link = g->walkNodes[node].links.empty() ? -1 : g->walkNodes[node].links[0];
                w.state = WS_WALK;
            }
            w.waitTimer = 0.f;
        }
        return desired;
    }
    if (w.state == WS_IDLE) return vec2(0, 0);
    float spd = w.speed * w.hurry;
    if (w.state == WS_CROSSING && L.kind == WL_CROSSWALK) {
        PedSignal ps = g->pedSignal(L.node, L.approach, time);
        if (ps != PED_WALK && ps != PED_UNCONTROLLED) w.hurry = 1.45f;
    }
    if (off < 1.4f) w.x += spd * dt;
    else if (off < 3.f) w.x += spd * dt * 0.4f;
    // stuck detection (blocked by a wall/prop): pick another direction
    w.stuckTimer += dt;
    w.flipCd = Max(0.f, w.flipCd - dt);
    if (w.stuckTimer > 2.f) {
        if (fabsf(w.x - w.lastProgress) < 0.8f && off > 1.2f && w.flipCd <= 0.f) {
            w.fromA = !w.fromA;
            w.x = Max(0.f, L.length - w.x);
            w.lat = -w.lat;
            w.state = WS_WALK;
            w.flipCd = 7.f;
        }
        w.lastProgress = w.x;
        w.stuckTimer = 0.f;
    }
    if (w.x >= L.length) {
        int node = w.fromA ? L.b : L.a;
        w.x = L.length;
        if (L.kind == WL_CROSSWALK || L.kind == WL_ZEBRA) w.avoidCrossing = false;
        chooseNext(w, node);
        if (w.link < 0) return vec2(0, 0);
        if (w.state == WS_WAIT_CROSS) return vec2(0, 0);
    }
    vec2 tgt = target(w, 1.6f);
    vec2 to = tgt - pos;
    float dl = length(to);
    if (dl > 0.05f) desired = to / dl * spd;
    // ---- local avoidance
    if (traffic) {
        vec2 push(0, 0), detour(0, 0);
        float r = 3.5f;
        vec2 myVel = desired;
        traffic->hash.query(traffic->bodies, pos - vec2(r + 7.f), pos + vec2(r + 7.f), [&](int bi) {
            if (bi == selfBody) return;
            const Body& b = traffic->bodies[bi];
            vec2 rel = pos - b.pos;
            if (b.kind == BK_PED) {
                float d = length(rel);
                if (d > r || d < 1e-3f) return;
                vec2 n = rel / d;
                // repulsion (personal space ~0.7 m)
                push += n * (1.6f * expf(-(d - 0.6f) / 0.35f));
                // anticipate head-on encounters: both keep right
                vec2 rv = myVel - b.vel;
                if (dot(rv, -n) > 0.3f && d < 2.5f) push += rightOf(normalize(myVel + vec2(1e-3f, 0.f))) * 0.45f;
            } else if (b.speed < 0.15f) {
                // stopped vehicle (e.g. on the crosswalk): walk around its nearer end (one inching forward at us counts
                // as moving: step out of its way)
                vec2 bf = b.fwd, br = rightOf(bf);
                vec2 lp = pos - b.pos;
                float lx = dot(lp, br), ly = dot(lp, bf);
                float ex = b.halfWid + 0.9f, ey = b.halfLen + 0.9f;
                vec2 dir = length2(desired) > 1e-4f ? normalize(desired) : w.lastDir;
                // our path through the next ~2.5 m crosses the expanded box?
                bool blocking = false;
                for (float t = 0.f; t <= 2.5f && !blocking; t += 0.5f) {
                    vec2 q = lp + dir * t;
                    if (fabsf(dot(q, br)) < ex && fabsf(dot(q, bf)) < ey) blocking = true;
                }
                if (blocking) {
                    // walk along the car on our side to its nearer end, then carry on
                    float endSign = ly >= 0.f ? 1.f : -1.f;
                    vec2 corner = b.pos + bf * (endSign * (b.halfLen + 1.1f)) + br * Clamp(lx, -ex, ex);
                    vec2 toC = corner - pos;
                    float dc = length(toC);
                    if (dc > 0.3f) detour = toC / dc;
                }
            } else {
                // vehicles: keep out of their path
                vec2 bf = b.fwd;
                vec2 local(dot(-rel, rightOf(bf)), dot(-rel, bf));
                float ex = b.halfWid + 0.8f, ey = b.halfLen + 0.8f + Max(0.f, dot(b.vel, bf)) * 0.6f;
                float lx = fabsf(local.x), ly = local.y;
                bool ahead = ly < 0.f;  // ped in front of the vehicle when rel points along +fwd
                (void)ahead;
                vec2 relV = pos - b.pos;
                float along = dot(relV, bf), side = dot(relV, rightOf(bf));
                if (fabsf(side) < ex && along > -b.halfLen - 0.8f && along < ey && (b.speed > 0.5f || along < b.halfLen + 0.5f)) {
                    float s = side >= 0.f ? 1.f : -1.f;
                    push += rightOf(bf) * (s * 2.5f);
                }
                (void)lx;
            }
        });
        if (length2(detour) > 0.f) desired = detour * Max(spd, 1.2f) + push * 0.3f;
        else desired += push;
        float m = length(desired);
        float cap = Max(spd * 1.35f, 1.8f);
        if (m > cap) desired *= cap / m;
    }
    if (faceYaw && length2(desired) > 0.04f) *faceYaw = dirYaw(desired);
    w.lastDir = length2(desired) > 1e-4f ? normalize(desired) : w.lastDir;
    return desired;
}

}  // namespace AI
