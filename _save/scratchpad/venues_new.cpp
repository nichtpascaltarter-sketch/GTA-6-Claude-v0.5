// ---- venues: places with a working crowd of their own - the Port Isle gate, its truck holding area and the yard along
// Port Boulevard, the airport forecourt (curb and taxi rank), the Sawgrass causeway and its airboat landing. Scenario
// slots laid out in the frame of the place (site elements: the gate and its booths, the rigs waiting for the gate call,
// the stack lane mouths and block boards, the terminal curb, the rank shelter; road junctions), each with its people,
// what they do there and their hours. A venue fills when the player comes near (out of sight, or at once during a
// warmup fade) and empties again once they are far away. Nobody stands on a live lane (every slot is checked against the
// lane graph, buildings and water when the venue is laid out), and everybody stands at the level of the ground there -
// never on a canopy or a roof above it.
enum VenueLook : u8 { VL_WORKER = 0, VL_CIVIL, VL_BUSINESS, VL_BEACH, VL_TRAVELER };
enum VenueProp : u8 { VP_NONE = 0, VP_TRUCK, VP_TAXI, VP_AIRBOAT, VP_CAR };

struct VenueSlot {
    vec2 pos;             // where they stand (or start)
    float yaw = 0.f;      // facing there
    vec2 pos2;            // pacing: the other spot / travelers: the terminal door / walk-ins: where they come from
    float yaw2 = 0.f;
    float z = 0.f;        // the level of the ground at pos (pad, sidewalk, road edge, terrain): filled in by the layout
    u8 mode = VM_STAND, look = VL_CIVIL, prop = VP_NONE;
    bool tight = false;   // may stand close to a lane edge (a causeway shoulder, the kerb beside a cab)
    bool follows = false; // only with the slot before it filled (the other half of a pair)
    bool walkIn = false;  // with the spot in view they come walking from pos2 (a door, the garage) instead of appearing
    float h0 = 0.f, h1 = 24.f;   // hours present (h0 > h1 wraps midnight)
    float chance = 1.f;          // filled on a given visit
    float every = 0.f;           // > 0: a stream (travelers) - a new one about this often (s) while the venue is near
    vec2 propPos;                // the vehicle that goes with the slot, parked, nobody in it
    float propYaw = 0.f;
    float propZ = 0.f;
    vec2 propOff;                // ... and the ped beside it: x to the vehicle's right (> 0) or left, y forward (from its box)
    // runtime
    int ped = -1;
    u32 pedUid = 0;
    int veh = -1;
    u32 vehUid = 0;
    float cooldown = 0.f;
    float timer = 0.f;    // pairs: how long the hug lasts
    u8 state = 0;         // pairs: 0 waiting for the other, 1 together, 2 gone to the car
};

struct Venue {
    const char* name = "";
    vec2 c;
    float fillR = 200.f, releaseR = 310.f;
    std::vector<VenueSlot> slots;
    bool active = false;
    u32 visits = 0;
    // a taxi rank: the cabs (VP_TAXI slots, each driver waiting beside their cab), the line (VM_QUEUE slots, head first)
    // and when the next fare takes the front cab
    int cab0 = -1, cabs = 0, q0 = -1, qn = 0, dispatcher = -1;
    float rankT = 25.f;
};

struct VenueSet {
    bool built = false;
    std::vector<Venue> v;
};
VenueSet gVenues;

bool venueHours(float tod, float h0, float h1) { return h0 <= h1 ? (tod >= h0 && tod < h1) : (tod >= h0 || tod < h1); }

// the level people stand at here: a pad (plazas, aprons, decks), the road or sidewalk surface, else the terrain
float venueStandZ(const GameWorld& g, vec2 p) {
    float z = g.map->heightAt(p.x, p.y), t = 0.f;
    if (g.roads->surfaceHeight(p, &t, z + 6.f)) z = Max(z, t);
    if (World::gSites && World::gSites->padHeight(p, &t, z + 6.f)) z = Max(z, t);
    return z;
}

// a place a person can stand: on the ground (not in water, not inside a building), clear of every traffic lane
bool venueSpotOk(const GameWorld& g, vec2 p, bool tight = false) {
    if (g.map->isWater(p.x, p.y)) {
        float z = 0.f;
        if (!g.roads->surfaceHeight(p, &z, 1e9f) && !(World::gSites && World::gSites->padHeight(p, &z, 1e9f))) return false;   // decks, piers
    }
    if (World::gBuildings && World::gBuildings->pointInBuilding(p, 0.4f)) return false;
    float u = 0.f, lat = 0.f;
    int ln = g.laneGraph.nearestLane(p, vec2(0.f), 10.f, &u, &lat);
    if (ln >= 0) {
        const AI::Lane& L = g.laneGraph.lanes[ln];
        bool within = u > L.u0 - 1.f && u < L.u1 + 1.f;
        if (within && fabsf(lat) < L.width * 0.5f + (tight ? 0.35f : 0.9f)) return false;
    }
    return true;
}

// a kerb lane a car can wait in with its hazards on (a cab at the rank, a drop-off): the rightmost lane of its road,
// with a lane beside it for the traffic to pass in
bool venueKerbLaneOk(const GameWorld& g, vec2 p, vec2 fwd) {
    float u = 0.f, lat = 0.f;
    int ln = g.laneGraph.nearestLane(p, fwd, 6.f, &u, &lat);
    if (ln < 0) return false;
    const AI::Lane& L = g.laneGraph.lanes[ln];
    return fabsf(lat) < 0.8f && L.right < 0 && L.left >= 0 && u > L.u0 + 4.f && u < L.u1 - 4.f;
}

VenueSlot mkSlot(vec2 pos, vec2 face, u8 mode, u8 look, float h0, float h1, float chance) {
    VenueSlot s;
    s.pos = s.pos2 = pos;
    s.yaw = s.yaw2 = AI::dirYaw(length2(face) > 1e-6f ? normalize(face) : vec2(0.f, 1.f));
    s.mode = mode;
    s.look = look;
    s.h0 = h0;
    s.h1 = h1;
    s.chance = chance;
    return s;
}

void buildVenues(GameWorld& g) {
    gVenues.built = true;
    gVenues.v.clear();
    if (!World::gSites) return;
    const World::SiteSet& S = *World::gSites;
    auto findElem = [&](u16 kind, int variant, vec2 around, float maxD) -> const World::SiteElem* {
        const World::SiteElem* best = nullptr;
        float bd = maxD;
        for (const World::SiteElem& e : S.elems)
            if (e.kind == kind && (variant < 0 || e.variant == variant) && length(e.c - around) < bd) {
                bd = length(e.c - around);
                best = &e;
            }
        return best;
    };
    int kept = 0, dropped = 0;
    auto finish = [&](Venue& V) {
        std::vector<VenueSlot> ok;
        int remap[64];
        for (int k = 0; k < 64; k++) remap[k] = -1;
        for (int si = 0; si < (int)V.slots.size(); si++) {
            VenueSlot& s = V.slots[si];
            bool tight = s.tight || s.mode == VM_TRAVEL_IN || s.mode == VM_TRAVEL_OUT || s.mode == VM_FAREWELL || s.mode == VM_SEEOFF;
            bool good = (s.prop != VP_NONE && length2(s.propOff) > 0.f) || venueSpotOk(g, s.pos, tight);
            if (good && s.mode == VM_PACE) good = venueSpotOk(g, s.pos2, tight);
            if (good && s.prop == VP_TRUCK && !venueSpotOk(g, s.propPos)) good = false;
            if (good && (s.prop == VP_TAXI || s.prop == VP_CAR) && !venueKerbLaneOk(g, s.propPos, AI::yawDir(s.propYaw))) good = false;
            if (good && s.prop == VP_AIRBOAT && !g.map->isWater(s.propPos.x, s.propPos.y)) good = false;
            // a pair goes as a pair
            if (good && s.follows && (ok.empty() || remap[si - 1] < 0)) good = false;
            if (good && (int)ok.size() < 64) {
                s.z = venueStandZ(g, s.pos);
                s.propZ = s.prop != VP_NONE ? venueStandZ(g, s.propPos) : 0.f;
                remap[si] = (int)ok.size();
                ok.push_back(s);
                kept++;
            } else {
                dropped++;   // (a dropped first half of a pair takes its partner along, above)
            }
        }
        // the rank's slot ranges after the drops
        auto rangeRemap = [&](int& first, int& n) {
            int f = -1, c = 0;
            for (int k = 0; k < n; k++)
                if (first + k < 64 && remap[first + k] >= 0) {
                    if (f < 0) f = remap[first + k];
                    c++;
                }
            first = f;
            n = c;
        };
        if (V.cab0 >= 0) rangeRemap(V.cab0, V.cabs);
        if (V.q0 >= 0) rangeRemap(V.q0, V.qn);
        if (V.dispatcher >= 0) V.dispatcher = V.dispatcher < 64 ? remap[V.dispatcher] : -1;
        if (V.cabs == 0 || V.qn == 0) V.cab0 = V.q0 = -1;
        V.slots.swap(ok);
        if (!V.slots.empty()) gVenues.v.push_back(V);
    };
    // ---------------------------------------------------------------- Port Isle: guards outside the gate booths, a clerk
    // checking the arriving rigs, a smoke break behind the inbound booth; truckers at their cabs in the holding area
    // waiting for the gate call; and the yard along Port Boulevard - dock workers in hard hats and hi-vis walking the
    // barrier line between the stack lane mouths and the gate, a banksman and a crew at the lane mouths, a checker at
    // the block boards, a driver and a lasher at a rig waiting in a lane mouth
    if (const World::SiteElem* gate = findElem(World::SK_PORT_GATE, -1, vec2(4008.f, -160.f), 400.f)) {
        Venue V;
        V.name = "port";
        vec2 o = gate->c, ax = gate->ax, ay = perp(ax);
        auto F = [&](float x, float y) { return o + ax * x + ay * y; };
        auto D = [&](float x, float y) { return ax * x + ay * y; };
        float bs = gate->hy - 3.f;   // booth centres either side of the gate road (port.cpp genPortGate)
        V.c = F(32.f, -32.f);
        V.fillR = 240.f;
        V.releaseR = 340.f;
        for (int s = -1; s <= 1; s += 2) V.slots.push_back(mkSlot(F(2.55f, s * (bs + 0.5f)), D(0.f, (float)-s), VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(F(-2.7f, -(bs - 0.6f)), D(0.f, 1.f), VM_GUARD, VL_WORKER, 6.f, 22.f, 0.9f));
        V.slots.push_back(mkSlot(F(-3.4f, bs + 2.2f), D(1.f, 0.f), VM_PHONE, VL_WORKER, 6.f, 22.f, 0.6f));
        for (int k = 0; k < 3; k++) {
            float a = 0.6f + k * kTwoPi / 3.f;
            vec2 off(cosf(a) * 0.85f, sinf(a) * 0.85f);
            V.slots.push_back(mkSlot(F(4.8f + off.x, -(bs + 3.6f) + off.y), -D(off.x, off.y), k == 1 ? VM_TALK : VM_SMOKE, VL_WORKER, 7.f, 18.f, 0.85f));
        }
        // the holding area south of the gate: rigs facing the gate (their tractor bumper, port.cpp genPortTruck)
        std::vector<const World::SiteElem*> rigs;
        for (const World::SiteElem& e : S.elems)
            if (e.kind == World::SK_PORT_TRUCK && dot(e.c - o, ay) < -20.f && dot(e.c - o, ay) > -110.f && fabsf(dot(e.c - o, ax)) < 40.f && dot(e.ax, ay) > 0.9f)
                rigs.push_back(&e);
        std::sort(rigs.begin(), rigs.end(), [&](const World::SiteElem* a, const World::SiteElem* b) { return dot(a->c, ax) < dot(b->c, ax); });
        auto bumper = [](const World::SiteElem& e) { return 0.5f * (7.1f + (e.variant == 1 ? 6.4f : 12.5f) - 2.2f); };
        for (size_t k = 0; k < rigs.size() && k < 4; k++) {
            const World::SiteElem& r = *rigs[k];
            vec2 f = r.ax, left = perp(r.ax);
            float xf = bumper(r);
            if (k == 0) {
                V.slots.push_back(mkSlot(r.c + f * (xf - 3.f) + left * 1.95f, left, VM_PHONE, VL_CIVIL, 5.f, 21.f, 0.9f));   // at the driver's door
            } else if (k == 1) {
                V.slots.push_back(mkSlot(r.c + f * (xf + 1.5f) + left * 0.4f, f, VM_STAND, VL_CIVIL, 5.f, 21.f, 0.85f));   // eyes on the gate
            } else if (k == 2 && k + 1 < rigs.size()) {
                // two drivers in front of their cabs, passing the time
                const World::SiteElem& r2 = *rigs[k + 1];
                vec2 mid = (r.c + f * xf + r2.c + r2.ax * bumper(r2)) * 0.5f + f * 1.7f;
                vec2 across = normalize(r2.c - r.c + vec2(1e-4f, 0.f));
                VenueSlot a = mkSlot(mid - across * 0.5f, across, VM_TALK, VL_CIVIL, 5.f, 21.f, 0.8f);
                VenueSlot b = mkSlot(mid + across * 0.5f, -across, VM_TALK, VL_WORKER, 5.f, 21.f, 1.f);
                b.follows = true;
                V.slots.push_back(a);
                V.slots.push_back(b);
            } else if (k == 3) {
                V.slots.push_back(mkSlot(r.c + f * (xf + 0.42f), f, VM_LEAN, VL_CIVIL, 5.f, 21.f, 0.75f));   // back against the grille
            }
        }
        // the yard: lane-mouth stands (variant 3, the working bays off the boulevard) east of the gate, nearest first
        std::vector<const World::SiteElem*> mouths;
        for (const World::SiteElem& e : S.elems)
            if (e.kind == World::SK_PORT_DRESS && e.variant == 3 && e.hx > 15.f && e.hx < 30.f && dot(e.c - o, ax) > 60.f && length(e.c - o) < 170.f)
                mouths.push_back(&e);
        std::sort(mouths.begin(), mouths.end(), [&](const World::SiteElem* a, const World::SiteElem* b) { return length(a->c - F(60.f, -45.f)) < length(b->c - F(60.f, -45.f)); });
        float stripX = 0.f;
        bool strip = false;
        for (size_t m = 0; m < mouths.size() && m < 2; m++) {
            const World::SiteElem& st = *mouths[m];
            vec2 mx = st.ax, my = perp(st.ax);
            vec2 west = st.c - mx * st.hx;                  // the mouth's end at the boulevard
            if (!strip) {
                stripX = dot(west, ax) - 8.7f;             // between the kerb and the barrier line (sites.cpp: xw = mouth end - 6.5)
                strip = true;
            }
            vec2 bank = west + mx * 3.5f - my * 4.5f;
            V.slots.push_back(mkSlot(bank, -mx, VM_SPOTTER, VL_WORKER, 6.f, 20.f, 0.9f));   // banksman, guiding the rigs in
            VenueSlot a = mkSlot(west + mx * 7.f + my * 3.f, mx * 0.3f - my, VM_TALK, VL_WORKER, 6.f, 20.f, 0.85f);
            VenueSlot b = mkSlot(west + mx * 7.9f + my * 2.4f, -(mx * 0.3f - my) + mx * 0.4f, VM_TALK, VL_WORKER, 6.f, 20.f, 1.f);
            b.yaw = AI::dirYaw(normalize(a.pos - b.pos));
            a.yaw = AI::dirYaw(normalize(b.pos - a.pos));
            b.follows = true;
            V.slots.push_back(a);
            V.slots.push_back(b);
            // a rig waiting in this mouth: its driver at the door, a lasher at the back checking the twist-locks
            for (const World::SiteElem& e : S.elems) {
                if (e.kind != World::SK_PORT_TRUCK || !st.contains(e.c, 3.f)) continue;
                vec2 f = e.ax, left = perp(e.ax);
                float xf = bumper(e), total = 2.f * xf;
                V.slots.push_back(mkSlot(e.c + f * (xf - 3.f) + left * 1.95f, f, VM_PHONE, VL_WORKER, 6.f, 20.f, 0.85f));
                V.slots.push_back(mkSlot(e.c - f * (total * 0.5f - 0.7f) + left * 1.7f, -left, VM_WORK, VL_WORKER, 6.f, 20.f, 0.9f));
                break;
            }
        }
        if (strip && mouths.size() >= 1) {
            // walking the barrier line between the lane mouths and the gate road (both ways)
            float yM = dot(mouths[0]->c, ay);
            float yG = dot(o, ay) - gate->hy + 5.5f;   // just short of the gate road's kerb
            if (mouths.size() >= 2) yM = Min(yM, dot(mouths[1]->c, ay));
            for (int k = 0; k < 2; k++) {
                float x = stripX + (k ? -1.2f : -0.2f);
                vec2 a = ax * x + ay * (k ? yG - 4.f : yM + 1.f), b = ax * x + ay * (k ? yM - 2.f : yG);
                VenueSlot s = mkSlot(a, b - a, VM_PACE, VL_WORKER, 6.f, 19.f, 0.95f);
                s.pos2 = b;
                s.yaw2 = AI::dirYaw(normalize(a - b));
                V.slots.push_back(s);
            }
            // a checker with a tablet at the block boards facing the boulevard (variant 5, "A4S" ...)
            int boards = 0;
            for (const World::SiteElem& e : S.elems) {
                if (boards >= 2 || e.kind != World::SK_PORT_DRESS || e.variant != 5 || fabsf(dot(e.c, ax) - (stripX + 3.6f)) > 2.5f) continue;
                float y = dot(e.c, ay);
                if (y > yG - 12.f || y < yM - 20.f || e.text.empty() || e.text.back() != 'S') continue;
                V.slots.push_back(mkSlot(ax * (stripX + 1.0f) + ay * y, ax, boards ? VM_STAND : VM_PHONE, VL_WORKER, 6.f, 19.f, 0.9f));
                boards++;
            }
        }
        finish(V);
    }
    // ---------------------------------------------------------------- airport forecourt. The terminal curb: travelers
    // between the curb and the doors (both ways), drop-offs (a car at the kerb, a hug, the traveler in through the doors
    // and the driver back behind the wheel), a pick-up (a driver waiting beside the car for someone coming out), security
    // by the doors. The east plaza: the taxi rank - cabs at the kerb, their drivers beside them, a dispatcher, the line
    // under the shelter (the head takes the front cab, the line steps up, new arrivals come from the garage) - a trolley
    // collector, someone on the phone at the trolley corral
    if (const World::SiteElem* term = findElem(World::SK_TERMINAL, -1, vec2(627.5f, 1440.f), 600.f)) {
        Venue V;
        V.name = "airport";
        float fx = term->c.x + term->hy;   // landside facade (the element's axis runs along y)
        float curbX = fx + 18.5f;          // inside the raised walk under the canopy, clear of the kerb bollards
        float y0 = term->c.y - term->hx + 30.f, y1 = Min(term->c.y + term->hx - 30.f, 1480.f);
        V.c = vec2(curbX + 30.f, (y0 + y1) * 0.5f - 40.f);
        V.fillR = 270.f;
        V.releaseR = 370.f;
        const vec2 east(1.f, 0.f), west(-1.f, 0.f), north(0.f, 1.f), south(0.f, -1.f);
        // (the kerb bollards stand every 2.4 m from y 1164: pairs stand at a gap so the walk to the car goes between two)
        auto gapY = [](float y) { return 1164.f + 2.4f * floorf((y - 1164.f) / 2.4f) + 1.2f; };
        for (int k = 0; k < 5; k++) {
            float y = Lerp(y0, y1, (k + 0.5f) / 5.f);
            float door = y + ((k & 1) ? 6.f : -6.f);
            VenueSlot in = mkSlot(vec2(curbX - 0.8f, y + 3.f), west, VM_TRAVEL_IN, VL_TRAVELER, 5.f, 23.5f, 1.f);
            in.pos2 = vec2(fx + 0.8f, door);
            in.every = 16.f + k * 4.f;
            V.slots.push_back(in);
            if (k % 2 == 0) {
                VenueSlot out = mkSlot(vec2(curbX - 1.2f, y - 4.f), east, VM_TRAVEL_OUT, VL_TRAVELER, 5.f, 23.5f, 0.9f);
                out.pos2 = vec2(fx + 0.8f, door + 3.f);
                out.every = 24.f + k * 4.f;
                V.slots.push_back(out);
            }
        }
        // the curb drive's kerb lane beside a spot on the walk: where a car waits (hazards on) while its people say goodbye
        auto kerbCar = [&](float y, vec2& at, float& yaw) {
            float u = 0.f;
            int ln = g.laneGraph.nearestLane(vec2(curbX + 5.f, y), vec2(0.f), 6.f, &u);
            if (ln < 0) return false;
            at = g.laneGraph.lanePos(ln, u).xy();
            yaw = AI::dirYaw(g.laneGraph.laneTangent(ln, u));
            return true;
        };
        for (int k = 0; k < 3; k++) {
            float y = gapY(Lerp(y0, y1, 0.3f + k * 0.2f));
            vec2 car;
            float cyaw = 0.f;
            if (!kerbCar(y, car, cyaw)) continue;
            vec2 cf = AI::yawDir(cyaw);
            if (k != 1) {
                // a drop-off: the traveler (the car goes with them) and the driver who brought them
                VenueSlot a = mkSlot(vec2(curbX - 0.3f, y - 0.42f), north, VM_FAREWELL, VL_TRAVELER, 6.f, 22.f, 0.85f);
                a.pos2 = vec2(fx + 0.8f, y + 2.f);   // the traveler goes in through the doors
                a.prop = VP_CAR;
                a.propPos = car - cf * 0.6f;
                a.propYaw = cyaw;
                VenueSlot b = mkSlot(vec2(curbX - 0.3f, y + 0.42f), south, VM_SEEOFF, VL_CIVIL, 6.f, 22.f, 1.f);
                b.follows = true;
                b.tight = true;
                V.slots.push_back(a);
                V.slots.push_back(b);
            } else {
                // a pick-up: the driver waits beside the car, the one they came for walks out of the terminal to them
                VenueSlot c = mkSlot(vec2(curbX + 0.1f, y + 0.4f), west, VM_MEET, VL_CIVIL, 7.f, 23.f, 0.85f);
                c.prop = VP_CAR;
                c.propPos = car - cf * 0.6f;
                c.propYaw = cyaw;
                c.tight = true;
                VenueSlot d = mkSlot(vec2(curbX - 0.55f, y + 0.4f), east, VM_MEET, VL_TRAVELER, 7.f, 23.f, 1.f);
                d.follows = true;
                d.walkIn = true;
                d.pos2 = vec2(fx + 0.8f, y - 5.f);
                V.slots.push_back(c);
                V.slots.push_back(d);
            }
        }
        V.slots.push_back(mkSlot(vec2(fx + 6.f, (y0 + y1) * 0.5f), east, VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(vec2(fx + 7.f, y0 + 14.f), east, VM_GUARD, VL_WORKER, 0.f, 24.f, 0.7f));
        // the taxi rank on the east plaza (airport.cpp genForecourt variant 1: shelter facing the kerb, TAXI totem at its
        // kerb end, queue belts in front of it)
        if (const World::SiteElem* sh = findElem(World::SK_FORECOURT, 1, vec2(760.f, 1250.f), 300.f)) {
            vec2 face = sh->ax, rt = perp(-face);
            vec2 totem = sh->c + face * 2.4f - rt * 4.2f;
            float u = 0.f;
            int ln = g.laneGraph.nearestLane(totem + face * 9.f, rt, 8.f, &u);
            if (ln >= 0 && dot(g.laneGraph.laneTangent(ln, u), rt) > 0.8f) {
                const AI::Lane& L = g.laneGraph.lanes[ln];
                float uHead = g.laneGraph.projectPath(ln, totem, u, nullptr) + 1.1f;   // front cab's rear door level with the totem
                V.cab0 = (int)V.slots.size();
                for (int k = 0; k < 3; k++) {
                    float uc = uHead - k * 6.8f;
                    if (uc < L.u0 + 5.f) break;
                    vec2 cp = g.laneGraph.lanePos(ln, uc).xy();
                    // the drivers: the front one leaning on his cab by the door, the next two chatting between theirs
                    VenueSlot s = mkSlot(cp, rt, k == 0 ? VM_LEAN : VM_TALK, VL_CIVIL, 0.f, 24.f, k == 0 ? 1.f : 0.9f);
                    s.prop = VP_TAXI;
                    s.propPos = cp;
                    s.propYaw = AI::dirYaw(g.laneGraph.laneTangent(ln, uc));
                    s.propOff = k == 0 ? vec2(1.f, 0.25f) : (k == 1 ? vec2(1.f, -0.95f) : vec2(1.f, 0.95f));
                    s.tight = true;
                    V.slots.push_back(s);
                    V.cabs++;
                }
                V.dispatcher = (int)V.slots.size();
                V.slots.push_back(mkSlot(totem + face * 5.f - rt * 1.6f, face, VM_GUARD, VL_WORKER, 5.f, 24.f, 0.95f));
                // the line: under the shelter's front edge, between the belts and the posts, facing the head
                V.q0 = (int)V.slots.size();
                vec2 qa = sh->c + face * 1.0f - rt * 2.9f;
                for (int k = 0; k < 4; k++) {
                    VenueSlot q = mkSlot(qa + rt * (1.25f * k), -rt, VM_QUEUE, VL_TRAVELER, 5.f, 24.f, k < 3 ? 0.95f : 0.7f);
                    q.walkIn = true;
                    q.pos2 = sh->c - face * 4.3f + rt * 12.f;   // from the garage frontage
                    V.slots.push_back(q);
                    V.qn++;
                }
            }
        }
        if (const World::SiteElem* tc = findElem(World::SK_FORECOURT, 2, vec2(761.f, 1212.f), 60.f)) {
            vec2 X = tc->ax, Y = perp(tc->ax);
            // someone on the phone by the trolley corral; a trolley collector working between the corral and the rank
            V.slots.push_back(mkSlot(tc->c + Y * 2.6f + X * 1.5f, -Y, VM_PHONE, VL_TRAVELER, 6.f, 23.f, 0.7f));
            VenueSlot tr = mkSlot(tc->c + Y * 4.2f - X * 4.6f, -X, VM_PACE, VL_WORKER, 6.f, 23.f, 0.85f);
            tr.pos2 = tc->c + Y * 4.6f + X * 20.f;
            tr.yaw2 = AI::dirYaw(X);
            V.slots.push_back(tr);
        }
        finish(V);
    }
    // ---------------------------------------------------------------- Sawgrass: anglers along the causeway shoulders
    // either side of the junction (at the edge of the pavement, facing the marsh), birders at dawn on the causeway and by
    // the observation tower, the airboat operator at the top of the landing ramp and riders waiting for the first tour
    {
        vec2 marsh(-5000.f, 100.f);
        int node = -1;
        float bd = 400.f;
        for (int n = 0; n < (int)g.roads->nodes.size(); n++) {
            float d = length(g.roads->nodes[n].p - marsh);
            if (d >= bd) continue;
            int paved = 0;
            for (int ei : g.roads->nodes[n].edges) paved += g.roads->edges[ei].cls != World::RC_DIRT && g.roads->edges[ei].length >= 150.f;
            if (paved < 2) continue;
            bd = d;
            node = n;
        }
        Venue V;
        V.name = "sawgrass";
        V.c = node >= 0 ? g.roads->nodes[node].p : marsh;
        V.fillR = 240.f;
        V.releaseR = 350.f;
        if (node >= 0) {
            int placed = 0;
            for (int ei : g.roads->nodes[node].edges) {
                const World::RoadEdge& e = g.roads->edges[ei];
                if (e.cls == World::RC_DIRT || e.length < 150.f) continue;
                bool fromN0 = e.n0 == node;
                // (the causeway to the west runs past the scenic stop: most of them there, on its north side)
                const float atW[6] = {16.f, 29.f, 46.f, 63.f, 90.f, 121.f};
                for (int k = 0; k < 6 && placed < 11; k++) {
                    float sAt = atW[k];
                    float at = fromN0 ? sAt : e.length - sAt;
                    vec3 c3 = e.posAt(at);
                    vec2 t = normalize(e.tangentAt(at).xy() + vec2(1e-5f, 0.f));
                    vec2 r = AI::rightOf(t);
                    // the marsh side: open water or wet ground within 14 m (the causeway's shoulders both qualify)
                    int side = ((hash32((u32)ei * 31u + (u32)k) >> 3) & 1) ? 1 : -1;
                    if (dot(r * (float)side, vec2(0.f, 1.f)) < 0.f && k % 3 != 2) side = -side;   // (north side mostly)
                    float edge = e.halfWidth - 0.4f;           // on the paved shoulder, past the lane edge
                    vec2 spot = c3.xy() + r * ((float)side * edge);
                    int mode = k % 4 == 3 ? VM_WORK : (k % 5 == 4 ? VM_SIT : VM_WATCH);
                    VenueSlot s = mkSlot(spot, r * (float)side, (u8)mode, k % 2 ? VL_BEACH : VL_CIVIL, 5.f, 19.5f, 0.85f);
                    s.tight = true;
                    V.slots.push_back(s);
                    placed++;
                    if (k == 2) {
                        // a birder pair a few steps along, pointing out what flies over the sawgrass at first light
                        VenueSlot b1 = mkSlot(c3.xy() + t * 7.f + r * ((float)side * edge), r * (float)side + t * 0.4f, VM_SPOTTER, VL_CIVIL, 5.3f, 10.5f, 0.9f);
                        VenueSlot b2 = mkSlot(c3.xy() + t * 7.9f + r * ((float)side * edge), r * (float)side - t * 0.4f, VM_SPOTTER, VL_BEACH, 5.3f, 10.5f, 1.f);
                        b1.tight = b2.tight = true;
                        b2.follows = true;
                        V.slots.push_back(b1);
                        V.slots.push_back(b2);
                    }
                }
            }
        }
        if (const World::SiteElem* tower = findElem(World::SK_OBS_TOWER, -1, V.c, 700.f)) {
            for (int k = 0; k < 3; k++) {
                float a = 2.4f + k * 0.9f;
                vec2 off(cosf(a), sinf(a));
                V.slots.push_back(mkSlot(tower->c + off * (tower->hx + 2.5f + k * 1.2f), off, VM_SPOTTER, k == 1 ? VL_CIVIL : VL_BEACH, 5.3f, 10.5f, 0.85f));
            }
        }
        if (const World::SiteElem* dock = findElem(World::SK_DOCK, -1, V.c, 400.f)) {
            // the landing (leisure.cpp genDock): a ramp from the shore down to a deck out along ax; the operator at the top
            // of the ramp with an eye on the boats, riders waiting for a ride
            vec2 d = dock->ax, n = perp(dock->ax);
            vec2 top = dock->c - d * 4.6f;
            V.slots.push_back(mkSlot(top + n * 1.9f, d, VM_STAND, VL_CIVIL, 6.5f, 18.5f, 0.95f));
            for (int k = 0; k < 2; k++)
                V.slots.push_back(mkSlot(top - d * (2.2f + k * 0.9f) - n * (1.2f + k * 0.7f), k ? n : d, k ? VM_PHONE : VM_TALK, VL_BEACH, 8.f, 17.5f, 0.6f));
        }
        finish(V);
    }
    std::string what;
    for (const Venue& V : gVenues.v)
        what += StrFormat(" | %s %d%s", V.name, (int)V.slots.size(), V.cab0 >= 0 ? StrFormat(" (rank: %d cabs, line of %d)", V.cabs, V.qn).c_str() : "");
    LOG("population: venues laid out: %d (%d slots kept, %d dropped on lanes / water / buildings)%s", (int)gVenues.v.size(), kept, dropped, what.c_str());
}

bool venuePedLive(const GameWorld& g, const VenueSlot& s) {
    if (s.ped < 0 || s.ped >= (int)g.peds.size()) return false;
    const Ped& p = g.peds[s.ped];
    return p.used && p.uid == s.pedUid;
}

bool venueVehLive(const GameWorld& g, const VenueSlot& s) {
    return s.veh >= 0 && s.veh < (int)g.vehicles.size() && g.vehicles[s.veh].used && g.vehicles[s.veh].uid == s.vehUid;
}

int venueChar(GameWorld& g, u8 look, u32 seed) {
    switch (look) {
        case VL_WORKER: return g.randomCivilianChar(seed, 5);
        case VL_BUSINESS: return g.randomCivilianChar(seed, 3);
        case VL_BEACH: return g.randomCivilianChar(seed, 4);
        case VL_TRAVELER: return g.randomCivilianChar(seed >> 2, (seed % 5 == 0) ? 3 : ((seed % 5 == 1) ? 4 : 0));
        default: return g.randomCivilianChar(seed, 0);
    }
}

// an ordinary pedestrian from now on
void venueLetGo(GameWorld& g, int id) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    pa.activity = ACT_WALK;
    pa.navOk = false;
    pa.stance = 0;
    pa.venue = -1;
    pa.targetVeh = -1;
    p.brain.type = BRAIN_WANDER;
    p.brain.edge = -1;
}

// off to a vehicle's kerb-side door (population.cpp aiVenueStep takes them in), out of their slot
void venueStartBoard(GameWorld& g, int id, int veh, bool driver) {
    PedAI& pa = g.pedAI(id);
    pa.activity = ACT_VENUE;
    pa.venueMode = VM_BOARD;
    pa.venueDriver = driver;
    pa.targetVeh = veh;
    pa.venue = -1;
    pa.stance = 0;
    pa.clipTimer = 0.f;
    pa.actTimer = 25.f;   // (gives up after this)
}

void releaseVenueSlot(GameWorld& g, VenueSlot& s, bool despawn) {
    if (venuePedLive(g, s)) {
        Ped& p = g.peds[s.ped];
        PedAI& pa = g.pedAI(s.ped);
        if (despawn && p.state != PS_INVEHICLE) {
            g.despawnPed(s.ped);
        } else if (pa.activity == ACT_VENUE && pa.venue >= 0) {
            venueLetGo(g, s.ped);
        } else if (pa.venue >= 0) {
            pa.venue = -1;
        }
    }
    s.ped = -1;
    if (venueVehLive(g, s)) {
        Vehicle& v = g.vehicles[s.veh];
        bool occupied = false;
        for (int k = 0; k < 8; k++) occupied |= v.seats[k] >= 0;
        if (despawn && !occupied) g.despawnVehicle(s.veh, true);
        else v.persistent = false;   // the population recycles it like any parked car
    }
    s.veh = -1;
    s.state = 0;
}

// a venue vehicle with its driver aboard (and nobody else still walking to it) pulls away into the traffic; a cab with
// a fare heads somewhere across town
void venueTryDepart(GameWorld& g, int vid) {
    if (vid < 0 || vid >= (int)g.vehicles.size() || !g.vehicles[vid].used) return;
    Vehicle& v = g.vehicles[vid];
    int drv = v.seats[0];
    if (drv < 0) return;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const PedAI& q = g.ai.ped[i];
        if (g.peds[i].used && q.uid == g.peds[i].uid && q.activity == ACT_VENUE && q.venueMode == VM_BOARD && q.targetVeh == vid && g.peds[i].state == PS_ONFOOT) return;
    }
    v.parked = false;
    v.persistent = false;
    v.indicator = 0;
    v.sim.engineOn = true;
    g.peds[drv].brain.type = BRAIN_DRIVER;
    VehAI& va = g.vehAI(vid);
    va.role = VR_TRAFFIC;
    va.eventId = -1;
    float u = 0.f;
    int lane = g.laneGraph.nearestLane(v.sim.body.pos.toVec3().xy(), v.sim.forward().xy(), 8.f, &u);
    if (lane < 0 || !g.attachTraffic(vid, lane, u)) return;
    if (va.role == VR_TAXI)
        for (int s = 1; s < 8; s++) {
            int fare = v.seats[s];
            if (fare < 0) continue;
            // (as a hailed cab does, pedai.cpp): a ride 400-1500 m away
            u32 h = hash32(g.peds[fare].uid * 77u + (u32)g.time);
            float ang = hashToFloat(h) * kTwoPi, r = 400.f + hashToFloat(hash32(h)) * 1100.f;
            vec2 dest = v.sim.body.pos.toVec3().xy() + vec2(cosf(ang), sinf(ang)) * r;
            float ud = 0.f;
            int ln = g.laneGraph.nearestLane(dest, vec2(0.f), 300.f, &ud);
            va.fare = fare;
            va.dest = ln >= 0 ? g.laneGraph.lanePos(ln, ud).xy() : dest;
            va.task = 0;
            va.scene = -1;
            break;
        }
}

void updateVenues(GameWorld& g, vec3 pp, float dt, bool warm, float tod) {
    if (!gVenues.built) buildVenues(g);
    for (int vi = 0; vi < (int)gVenues.v.size(); vi++) {
        Venue& V = gVenues.v[vi];
        float d = length(V.c - pp.xy());
        if (!V.active && d < V.fillR) {
            V.active = true;
            V.visits++;
            V.rankT = 20.f + hashToFloat(hash32(V.visits * 97u + (u32)vi)) * 20.f;
        }
        if (V.active && d > V.releaseR) {
            for (VenueSlot& s : V.slots) {
                bool seen = venuePedLive(g, s) && g.inCameraView(g.peds[s.ped].pos.toVec3() + vec3(0, 0, 1.f), 1.f);
                releaseVenueSlot(g, s, !seen);
                s.cooldown = 0.f;
            }
            V.active = false;
        }
        if (!V.active) continue;
        // ---- the rank: every so often the head of the line takes the front cab that has its driver at hand
        if (V.cab0 >= 0 && V.q0 >= 0) {
            V.rankT -= dt;
            VenueSlot& head = V.slots[V.q0];
            bool headReady = venuePedLive(g, head) && length(g.peds[head.ped].pos.toVec3().xy() - head.pos) < 0.6f;
            if (V.rankT <= 0.f && headReady) {
                V.rankT = 6.f;
                for (int k = 0; k < V.cabs; k++) {
                    VenueSlot& cs = V.slots[V.cab0 + k];
                    if (!venueVehLive(g, cs) || !venuePedLive(g, cs) || g.peds[cs.ped].state != PS_ONFOOT) continue;
                    int cab = cs.veh;
                    // the driver round to the kerb-side front door, the fare to the back; the line steps up
                    venueStartBoard(g, cs.ped, cab, true);
                    venueStartBoard(g, head.ped, cab, false);
                    cs.ped = -1;
                    cs.veh = -1;   // (leaves with its driver: a fresh cab and driver take the place later, out of sight)
                    cs.cooldown = 50.f + hashToFloat(hash32((u32)g.time * 13u + (u32)k)) * 60.f;
                    head.ped = -1;
                    for (int q = 0; q + 1 < V.qn; q++) {
                        VenueSlot& a = V.slots[V.q0 + q];
                        VenueSlot& b = V.slots[V.q0 + q + 1];
                        a.ped = b.ped;
                        a.pedUid = b.pedUid;
                        b.ped = -1;
                        if (venuePedLive(g, a)) {
                            PedAI& pa = g.pedAI(a.ped);
                            pa.venue = vi * 64 + V.q0 + q;
                            pa.anchor = a.pos;
                            pa.anchorYaw = a.yaw;
                        }
                    }
                    V.slots[V.q0 + V.qn - 1].cooldown = 6.f + hashToFloat(hash32((u32)g.time * 7u)) * 20.f;
                    if (V.dispatcher >= 0 && venuePedLive(g, V.slots[V.dispatcher])) {
                        int di = V.slots[V.dispatcher].ped;
                        if (g.peds[di].pendingAction < 0) g.peds[di].pendingAction = Anim::CLIP_WAVE;
                        g.pedAI(di).clipTimer = 5.f;
                    }
                    V.rankT = 35.f + hashToFloat(hash32((u32)g.time * 31u + 5u)) * 45.f;
                    break;
                }
            }
        }
        int budget = warm ? 40 : 1;
        for (int si = 0; si < (int)V.slots.size(); si++) {
            VenueSlot& s = V.slots[si];
            s.cooldown -= dt;
            if (s.ped >= 0) {
                // still ours? (fled from gunfire, got knocked down, walked off, boarded: let the slot go, a new face later)
                bool live = venuePedLive(g, s);
                const PedAI* pa = live ? &g.pedAI(s.ped) : nullptr;
                if (!live || g.peds[s.ped].health <= 0.f || !pa || pa->venue != vi * 64 + si) {
                    releaseVenueSlot(g, s, false);
                    s.cooldown = 50.f + hashToFloat(hash32(V.visits * 131u + si * 7u + (u32)g.time)) * 50.f;
                    continue;
                }
                // a meeting at the curb: once the one coming out has reached the car, a hug and a few words, then both in
                if (s.mode == VM_MEET && !s.follows && si + 1 < (int)V.slots.size() && V.slots[si + 1].follows && venueVehLive(g, s)) {
                    VenueSlot& o = V.slots[si + 1];
                    if (!venuePedLive(g, o)) continue;
                    Ped& a = g.peds[s.ped];
                    Ped& b = g.peds[o.ped];
                    PedAI& pa2 = g.pedAI(s.ped);
                    PedAI& pb = g.pedAI(o.ped);
                    if (s.state == 0 && length(b.pos.toVec3().xy() - o.pos) < 0.6f) {
                        s.state = 1;
                        s.timer = 6.f + hashToFloat(hash32(a.uid * 3u + b.uid)) * 6.f;
                        pa2.stance = pb.stance = 7;
                        pa2.anchorYaw = AI::dirYaw(normalize(o.pos - s.pos + vec2(1e-4f, 0.f)));
                        pb.anchorYaw = AI::dirYaw(normalize(s.pos - o.pos + vec2(1e-4f, 0.f)));
                    } else if (s.state == 1) {
                        s.timer -= dt;
                        if (s.timer <= 0.f) {
                            int car = s.veh;
                            venueStartBoard(g, s.ped, car, true);
                            venueStartBoard(g, o.ped, car, false);
                            s.ped = o.ped = -1;
                            s.veh = -1;
                            s.state = 2;
                            s.cooldown = 60.f + hashToFloat(hash32((u32)g.time * 5u + (u32)si)) * 60.f;
                            o.cooldown = 0.f;
                        }
                    }
                }
                continue;
            }
            if (s.cooldown > 0.f || budget <= 0 || !venueHours(tod, s.h0, s.h1)) continue;
            if (s.follows && (si == 0 || V.slots[si - 1].ped < 0)) continue;
            // the line fills from the head: nobody joins behind a gap
            if (s.mode == VM_QUEUE && si > V.q0 && V.slots[si - 1].ped < 0) continue;
            u32 h = hash32(V.visits * 2654435761u + (u32)si * 40503u + (s.every > 0.f ? (u32)(g.time / Max(s.every, 1.f)) : 0u) +
                           (s.mode == VM_QUEUE ? (u32)(g.time * 3.0) : 0u));
            if (hashToFloat(h) > s.chance) {
                s.cooldown = s.every > 0.f ? s.every : (s.mode == VM_QUEUE ? 15.f : 1e9f);   // not this visit (streams, the line: not this time round)
                continue;
            }
            // the vehicle that goes with the slot (a rig, a cab, a car at the kerb, the airboat), parked with nobody in
            // it - out of sight, on a free stretch of kerb
            if (s.prop != VP_NONE && !venueVehLive(g, s)) {
                using namespace Vehicles;
                VehicleClass cls = s.prop == VP_TRUCK ? VC_TRUCK : (s.prop == VP_TAXI ? VC_TAXI : (s.prop == VP_AIRBOAT ? VC_AIRBOAT : VC_SEDAN));
                if (s.prop == VP_CAR) {
                    static const VehicleClass kCars[4] = {VC_SEDAN, VC_SUV, VC_COMPACT, VC_PICKUP};
                    cls = kCars[(h >> 6) % 4];
                }
                int model = g.findVehicleModel(cls, h >> 3);
                if (model < 0 && s.prop == VP_CAR) model = g.findVehicleModel(VC_SEDAN, h >> 4);
                s.veh = -1;
                if (model >= 0) {
                    float vz = s.prop == VP_AIRBOAT ? 0.4f : g.groundHeight(s.propPos.x, s.propPos.y, s.propZ + 1.2f) + 0.4f;
                    bool vSeen = !warm && g.inCameraView(vec3(s.propPos, vz), 4.f) && length(s.propPos - pp.xy()) < 160.f;
                    std::vector<int> there;
                    g.vehiclesNear(s.propPos, 4.6f, there);
                    if (!vSeen && there.empty()) {
                        int vid = g.spawnVehicle(model, dvec3(s.propPos.x, s.propPos.y, vz), s.propYaw, false);
                        if (vid >= 0) {
                            Vehicle& v = g.vehicles[vid];
                            v.parked = true;
                            v.persistent = true;   // (the venue lets it go when the player leaves)
                            v.sim.engineOn = false;
                            v.indicator = s.prop == VP_CAR ? 2 : 0;   // hazards on at the kerb
                            s.veh = vid;
                            s.vehUid = v.uid;
                        }
                    }
                }
                if (s.veh < 0) {
                    s.cooldown = 3.f;
                    continue;
                }
            }
            // where they appear: travelers heading out come through a door (fine in view), walk-ins from where they come
            // from when their spot is in view, everyone else out of sight
            bool out = s.mode == VM_TRAVEL_OUT;
            vec2 at = out ? s.pos2 : s.pos;
            float atZ = s.z;
            float faceYaw = s.yaw;
            if (s.mode == VM_TRAVEL_IN && warm) at = s.pos + (s.pos2 - s.pos) * (hashToFloat(hash32(h + 5u)) * 0.85f);   // mid-way at a fade-in
            bool beside = s.prop != VP_NONE && length2(s.propOff) > 0.f && s.veh >= 0;
            if (beside) {
                // beside the vehicle: its kerb side (propOff), leaning back on it, or facing along it
                const Vehicle& v = g.vehicles[s.veh];
                vec3 bh = g.vassets[v.model].spec.boxHalf;
                vec2 vf = v.sim.forward().xy();
                vf = length2(vf) > 1e-6f ? normalize(vf) : vec2(0.f, 1.f);
                vec2 vr = AI::rightOf(vf);
                vec2 side = vr * (s.propOff.x >= 0.f ? 1.f : -1.f);
                at = v.sim.body.pos.toVec3().xy() + side * (bh.x + 0.55f) + vf * (s.propOff.y * bh.y);
                atZ = (float)v.sim.body.pos.z;
                faceYaw = AI::dirYaw(s.mode == VM_LEAN ? side : (s.propOff.y < 0.f ? -vf : vf));
            }
            float z = g.groundHeight(at.x, at.y, atZ + 1.2f);
            vec3 p3(at.x, at.y, z);
            bool seen = !warm && g.inCameraView(p3 + vec3(0, 0, 1.f), 1.5f) && length(at - pp.xy()) < 120.f;
            bool walking = false;
            if (seen && !out && s.walkIn) {
                // in view: they come walking from the door / the garage instead
                float z2 = g.groundHeight(s.pos2.x, s.pos2.y, venueStandZ(g, s.pos2) + 1.2f);
                at = s.pos2;
                p3 = vec3(at.x, at.y, z2);
                walking = true;
                seen = false;
            }
            if ((seen && !out) || (!warm && length(at - pp.xy()) < 10.f) || !freeStandingSpot(g, p3)) {
                s.cooldown = 2.f;
                continue;
            }
            int ci = venueChar(g, s.look, h >> 5);
            if (ci < 0) continue;
            float spawnYaw = out ? AI::dirYaw(normalize(s.pos - s.pos2 + vec2(1e-4f, 0.f))) : (walking ? AI::dirYaw(normalize(s.pos - s.pos2 + vec2(1e-4f, 0.f))) : faceYaw);
            int id = g.spawnPed(ci, dvec3(p3), spawnYaw, FAC_CIVILIAN);
            if (id < 0) continue;
            budget--;
            Ped& p = g.peds[id];
            PedAI& pa = g.pedAI(id);
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
            pa.role = s.look == VL_WORKER ? PR_WORKER : (s.look == VL_BUSINESS ? PR_BUSINESS : (s.look == VL_BEACH ? PR_BEACH : PR_CIVILIAN));
            if (s.mode == VM_TRAVEL_IN) {
                // off to the door and inside (population's door walkers)
                pa.activity = ACT_ENTER_VEH;
                pa.targetVeh = -1;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = dvec3(vec3(s.pos2, g.groundHeight(s.pos2.x, s.pos2.y, z + 2.f)));
                p.brain.speed = 1.25f + hashToFloat(hash32(h + 9u)) * 0.35f;
                p.brain.timer = 0.f;
                p.yaw = AI::dirYaw(normalize(s.pos2 - at + vec2(1e-4f, 0.f)));
                s.cooldown = s.every * (0.7f + hashToFloat(hash32(h + 3u)) * 0.6f);
                continue;   // (a stream: nothing to hold on to)
            }
            pa.activity = ACT_VENUE;
            pa.venue = vi * 64 + si;
            pa.venueMode = s.mode;
            pa.venueDriver = false;
            pa.targetVeh = -1;
            pa.anchor = beside ? at : s.pos;
            pa.anchorYaw = faceYaw;
            pa.anchorB = s.pos2;
            pa.anchorBYaw = s.yaw2;
            pa.clip = -1;
            pa.clipTimer = 2.f + hashToFloat(hash32(h + 17u)) * 6.f;
            switch (s.mode) {
                case VM_SMOKE: pa.stance = 10; break;
                case VM_TALK: pa.stance = 7; break;
                case VM_PHONE: pa.stance = 8; break;
                case VM_LEAN: pa.stance = 11; break;
                case VM_SIT: pa.stance = 21; break;
                case VM_TRAVEL_OUT: pa.stance = 8; break;
                case VM_QUEUE: pa.stance = 23; break;
                case VM_MEET: pa.stance = s.follows ? 0 : 8; break;   // (the driver on the phone: "we're outside")
                case VM_FAREWELL:
                case VM_SEEOFF: pa.stance = 7; break;
                default: pa.stance = 0; break;
            }
            // how long before the next move: pacing legs, the wait at the curb, a goodbye, a spell of work
            pa.actTimer = s.mode == VM_PACE ? 8.f + hashToFloat(hash32(h + 21u)) * 18.f
                        : s.mode == VM_TRAVEL_OUT ? 25.f + hashToFloat(hash32(h + 21u)) * 40.f
                        : s.mode == VM_FAREWELL ? 14.f + hashToFloat(hash32(h + 21u)) * 16.f
                        : s.mode == VM_WORK ? 4.f + hashToFloat(hash32(h + 21u)) * 8.f
                                              : 1e5f;
            if (s.follows && si > 0 && venuePedLive(g, V.slots[si - 1])) {
                // the other half of a pair: face them, and part a moment after they go; the one who drove takes the car back
                const VenueSlot& o = V.slots[si - 1];
                pa.actTimer = g.pedAI(o.ped).actTimer + 1.5f;
                pa.anchorYaw = AI::dirYaw(normalize(o.pos - s.pos + vec2(1e-4f, 0.f)));
                if (s.mode == VM_SEEOFF && venueVehLive(g, o)) pa.targetVeh = o.veh;
                if (!walking) p.yaw = pa.anchorYaw;
            }
            if (s.mode == VM_PACE && (h & 1)) {   // half of them start at the far end
                std::swap(pa.anchor, pa.anchorB);
                std::swap(pa.anchorYaw, pa.anchorBYaw);
            }
            if (!out && !walking) p.yaw = pa.anchorYaw;
            s.ped = id;
            s.pedUid = p.uid;
            s.state = 0;
            if (s.every > 0.f) s.cooldown = s.every;
        }
    }
}

// the venue ped count near the player (census / autoplay)
int venuePedsNear(const GameWorld& g, vec2 at, float r) {
    int n = 0;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& p = g.peds[i];
        if (!p.used || g.ai.ped[i].uid != p.uid || g.ai.ped[i].venue < 0) continue;
        n += length(p.pos.toVec3().xy() - at) < r;
    }
    return n;
}

}  // namespace pop_detail

using namespace pop_detail;

bool aiVenueStep(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    vec2 pos = p.pos.toVec3().xy();
    bool there = length(pa.anchor - pos) <= 0.35f;
    bool idle = there && pa.clipTimer <= 0.f && p.pendingAction < 0;
    u32 hq = hash32(p.uid * 5u + (u32)(g.time * 2.0));
    // a flashy car rolling by slowly: a point and a word - without leaving the post
    const Ped* pl = g.playerPed();
    if (pl && there && pl->state == PS_INVEHICLE && pl->vehicle >= 0 && p.pendingAction < 0 && pa.barkCooldown <= 0.f && pa.venueMode != VM_SIT &&
        pa.venueMode != VM_BOARD) {
        const Vehicle& pv = g.vehicles[pl->vehicle];
        Vehicles::VehicleClass cls = g.vassets[pv.model].spec.cls;
        bool flashy = cls == Vehicles::VC_SUPER || cls == Vehicles::VC_SPORTS || cls == Vehicles::VC_MUSCLE;
        if (flashy && pv.sim.speed() < 11.f && length(pv.sim.body.pos.toVec3().xy() - pos) < 14.f &&
            hashToFloat(hash32(p.uid * 131u + (u32)(g.time * 0.5))) < 0.05f) {
            p.pendingAction = Anim::CLIP_POINT;
            pa.clipTimer = 6.f;
            g.aiSay(id, BK_NICE_CAR, 0.7f);
        }
    }
    switch (pa.venueMode) {
        case VM_PACE:
            // over to the other end once the break here is over (the walk there counts against the next one)
            if (there && pa.actTimer <= 0.f) {
                std::swap(pa.anchor, pa.anchorB);
                std::swap(pa.anchorYaw, pa.anchorBYaw);
                pa.actTimer = 10.f + hashToFloat(hq) * 22.f + length(pa.anchor - pos) / 1.3f;
            }
            if (idle) {
                p.pendingAction = Anim::CLIP_IDLE_LOOK;
                pa.clipTimer = 5.f + hashToFloat(hq) * 7.f;
            }
            break;
        case VM_GUARD:
            if (idle) {
                // a vehicle rolling up slowly: wave it through; otherwise a look round, now and then pointing the way
                bool rolling = false;
                std::vector<int> close;
                g.vehiclesNear(pos, 18.f, close);
                for (int vi : close) {
                    const Vehicle& v = g.vehicles[vi];
                    float sp = v.sim.speed();
                    vec2 vp = v.sim.body.pos.toVec3().xy(), vf = v.sim.forward().xy();
                    if (sp > 0.8f && sp < 10.f && dot(pos - vp, vf) > 0.f) rolling = true;
                }
                p.pendingAction = rolling ? Anim::CLIP_WAVE : (hq % 5 == 0 ? Anim::CLIP_POINT : Anim::CLIP_IDLE_LOOK);
                pa.clipTimer = rolling ? 6.f : 6.f + hashToFloat(hq) * 8.f;
            }
            break;
        case VM_STAND:
        case VM_WATCH:
        case VM_SPOTTER:
        case VM_QUEUE:
            if (idle) {
                // anglers hold still for long spells; birders point things out to each other; the line shuffles
                p.pendingAction = pa.venueMode == VM_SPOTTER && hq % 3 != 0 ? Anim::CLIP_POINT : Anim::CLIP_IDLE_LOOK;
                pa.clipTimer = (pa.venueMode == VM_WATCH ? 14.f : (pa.venueMode == VM_QUEUE ? 5.f : 7.f)) + hashToFloat(hq) * 10.f;
            }
            break;
        case VM_WORK:
            // crouched over the job a while, then up for a look round (and back down)
            if (there && pa.actTimer <= 0.f) {
                bool down = pa.stance == 18;
                pa.stance = down ? 0 : 18;
                pa.actTimer = down ? 3.f + hashToFloat(hq) * 4.f : 6.f + hashToFloat(hq) * 9.f;
                if (down && p.pendingAction < 0) p.pendingAction = Anim::CLIP_IDLE_LOOK;
            }
            break;
        case VM_TRAVEL_OUT:
            if (there && pa.actTimer <= 0.f) {
                // done waiting on the phone: flag down a cab (the taxi logic takes it from here)
                pa.activity = ACT_HAIL_TAXI;
                pa.targetVeh = -1;
                pa.stance = 0;
                pa.venue = -1;
                pa.actTimer = 0.f;
                pa.clipTimer = 0.f;
                return true;
            }
            break;
        case VM_FAREWELL:
            if (pa.actTimer <= 0.f) {
                // goodbyes said: in through the doors (and out of the simulation, population.cpp)
                pa.activity = ACT_ENTER_VEH;
                pa.targetVeh = -1;
                pa.venue = -1;
                pa.stance = 0;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = dvec3(vec3(pa.anchorB, g.groundHeight(pa.anchorB.x, pa.anchorB.y, p.pos.toVec3().z + 2.f)));
                p.brain.speed = 1.3f;
                p.brain.timer = 0.f;
                return true;
            }
            break;
        case VM_SEEOFF:
            if (pa.actTimer <= 0.f) {
                if (pa.clipTimer > -50.f) {
                    p.pendingAction = Anim::CLIP_WAVE;   // a wave after them
                    pa.clipTimer = -100.f;
                } else if (pa.actTimer < -2.f) {
                    int car = pa.targetVeh;
                    if (car >= 0 && car < (int)g.vehicles.size() && g.vehicles[car].used && g.vehicles[car].seats[0] < 0) {
                        venueStartBoard(g, id, car, true);   // back behind the wheel, and away
                    } else {
                        venueLetGo(g, id);                   // off along the sidewalk
                    }
                    return true;
                }
            }
            break;
        case VM_BOARD: {
            // round to the kerb-side door (front for the driver, back for a passenger), in, and the vehicle leaves once
            // everybody is aboard
            int tv = pa.targetVeh;
            pa.actTimer -= dt;
            if (tv < 0 || tv >= (int)g.vehicles.size() || !g.vehicles[tv].used || g.vehicles[tv].sim.speed() > 1.f || pa.actTimer < 0.f ||
                (pa.venueDriver && g.vehicles[tv].seats[0] >= 0)) {
                venueLetGo(g, id);
                return true;
            }
            const Vehicle& v = g.vehicles[tv];
            const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
            vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(spec.boxHalf.x + 0.5f, pa.venueDriver ? spec.boxHalf.y * 0.18f : -spec.boxHalf.y * 0.22f, 0.f));
            pa.anchor = door.xy();
            pa.anchorYaw = AI::dirYaw(normalize(v.sim.body.pos.toVec3().xy() - door.xy() + vec2(1e-4f, 0.f)));
            pa.stance = 0;
            if (length(door.xy() - pos) < 0.6f) {
                int seat = pa.venueDriver ? 0 : g.freeSeat(tv, false);
                if (seat < 0 || (seat == 0 && !pa.venueDriver)) {
                    venueLetGo(g, id);
                    return true;
                }
                g.warpPedIntoVehicle(id, tv, seat);
                pa.activity = ACT_WALK;
                pa.venue = -1;
                pa.targetVeh = -1;
                pa.stance = 0;
                p.brain.type = pa.venueDriver ? BRAIN_DRIVER : BRAIN_NONE;
                venueTryDepart(g, tv);
                return true;
            }
            break;
        }
        default:
            break;   // (stances with loops of their own: smoke, phone, talk, lean, sit, meet)
    }
    return false;
}
