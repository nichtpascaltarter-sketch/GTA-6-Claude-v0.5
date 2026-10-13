// ---- venues: places with a working crowd of their own - the Port Isle gate, the airport forecourt, the Sawgrass
// causeway and its airboat landing. Scenario slots laid out in the frame of the place (a site element or a road
// junction), each with its people, what they do there and their hours. A venue fills when the player comes near (out
// of sight, or at once during a warmup fade) and empties again once they are far away. Nobody stands on a live lane:
// every slot is checked against the lane graph, buildings and water when the venue is laid out.
enum VenueLook : u8 { VL_WORKER = 0, VL_CIVIL, VL_BUSINESS, VL_BEACH, VL_TRAVELER };
enum VenueProp : u8 { VP_NONE = 0, VP_TRUCK, VP_TAXI, VP_AIRBOAT };

struct VenueSlot {
    vec2 pos;             // where they stand (or start)
    float yaw = 0.f;      // facing there
    vec2 pos2;            // pacing: the other spot / travelers: the terminal door / farewells: where they go after
    float yaw2 = 0.f;
    u8 mode = VM_STAND, look = VL_CIVIL, prop = VP_NONE;
    float h0 = 0.f, h1 = 24.f;   // hours present (h0 > h1 wraps midnight)
    float chance = 1.f;          // filled on a given visit
    float every = 0.f;           // > 0: a stream (travelers) - a new one about this often (s) while the venue is near
    vec2 propPos;                // the vehicle that goes with the slot, parked, nobody in it
    float propYaw = 0.f;
    vec2 propOff;                // ... and the ped beside it: x to the vehicle's right, y forward (from its box)
    bool follows = false;        // only with the slot before it filled (the other half of a pair)
    // runtime
    int ped = -1;
    u32 pedUid = 0;
    int veh = -1;
    u32 vehUid = 0;
    float cooldown = 0.f;
};

struct Venue {
    const char* name = "";
    vec2 c;
    float fillR = 200.f, releaseR = 310.f;
    std::vector<VenueSlot> slots;
    bool active = false;
    u32 visits = 0;
};

struct VenueSet {
    bool built = false;
    std::vector<Venue> v;
};
VenueSet gVenues;

bool venueHours(float tod, float h0, float h1) { return h0 <= h1 ? (tod >= h0 && tod < h1) : (tod >= h0 || tod < h1); }

// a place a person can stand: on the ground (not in water, not inside a building), clear of every traffic lane
bool venueSpotOk(const GameWorld& g, vec2 p, bool curb = false) {
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
        if (within && fabsf(lat) < L.width * 0.5f + (curb ? 0.35f : 0.9f)) return false;
    }
    return true;
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
    auto findElem = [&](u16 kind, vec2 around, float maxD) -> const World::SiteElem* {
        const World::SiteElem* best = nullptr;
        float bd = maxD;
        for (const World::SiteElem& e : S.elems)
            if (e.kind == kind && length(e.c - around) < bd) {
                bd = length(e.c - around);
                best = &e;
            }
        return best;
    };
    int kept = 0, dropped = 0;
    auto finish = [&](Venue& V) {
        std::vector<VenueSlot> ok;
        for (VenueSlot& s : V.slots) {
            bool curb = s.mode == VM_TRAVEL_IN || s.mode == VM_TRAVEL_OUT || s.mode == VM_FAREWELL;
            bool good = s.prop != VP_NONE || (venueSpotOk(g, s.pos, curb) && venueSpotOk(g, s.pos2, curb));
            if (good && (s.prop == VP_TRUCK || s.prop == VP_TAXI) && !venueSpotOk(g, s.propPos)) good = false;
            if (good && s.prop == VP_AIRBOAT && (!g.map->isWater(s.propPos.x, s.propPos.y) || !venueSpotOk(g, s.pos))) good = false;
            if (good) ok.push_back(s);
            (good ? kept : dropped)++;
        }
        V.slots.swap(ok);
        if (!V.slots.empty()) gVenues.v.push_back(V);
    };
    // ---------------------------------------------------------------- Port Isle gate (frame: x along the gate road into
    // the port, y to its left): guards at the booths, a smoke break behind the south booth, truckers at rigs parked
    // south of the gate, dock workers between the gate and the stacks north of the road, a checker at the OCR portal
    if (const World::SiteElem* gate = findElem(World::SK_PORT_GATE, vec2(4008.f, -160.f), 400.f)) {
        Venue V;
        V.name = "port gate";
        vec2 o = gate->c, ax = gate->ax, ay = perp(ax);
        auto F = [&](float x, float y) { return o + ax * x + ay * y; };
        auto D = [&](float x, float y) { return ax * x + ay * y; };
        float by = gate->hy - 3.f;   // booth line
        V.c = F(10.f, 0.f);
        V.slots.push_back(mkSlot(F(2.9f, by - 1.6f), D(0.f, -1.f), VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(F(-2.9f, -by + 1.6f), D(0.f, 1.f), VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(F(-3.2f, by + 1.9f), D(1.f, 0.f), VM_PHONE, VL_WORKER, 6.f, 22.f, 0.6f));
        for (int k = 0; k < 3; k++) {
            float a = 0.6f + k * kTwoPi / 3.f;
            vec2 off(cosf(a) * 0.85f, sinf(a) * 0.85f);
            V.slots.push_back(mkSlot(F(4.5f + off.x, -by - 3.4f + off.y), -D(off.x, off.y), k == 1 ? VM_TALK : VM_SMOKE, VL_WORKER, 7.f, 18.f, 0.85f));
        }
        for (int k = 0; k < 2; k++) {
            VenueSlot s = mkSlot(F(6.f + k * 20.f, -30.f), D(0.f, 1.f), k == 0 ? VM_LEAN : VM_PHONE, VL_CIVIL, 5.f, 21.f, 0.85f);
            s.prop = VP_TRUCK;
            s.propPos = s.pos;
            s.propYaw = AI::dirYaw(ax);
            s.propOff = vec2(-1.f, 0.3f);   // (by the driver's door on the left, towards the front: scaled by the box)
            V.slots.push_back(s);
        }
        const float paces[3][4] = {{12.f, 20.f, 63.f, 24.f}, {16.f, 30.f, 62.f, 15.f}, {-10.f, -21.f, -30.f, -34.f}};
        for (int k = 0; k < 3; k++) {
            VenueSlot s = mkSlot(F(paces[k][0], paces[k][1]), D(paces[k][2] - paces[k][0], paces[k][3] - paces[k][1]), VM_PACE, VL_WORKER, 6.f, 18.5f, 0.9f);
            s.pos2 = F(paces[k][2], paces[k][3]);
            s.yaw2 = AI::dirYaw(normalize(s.pos - s.pos2));
            V.slots.push_back(s);
        }
        V.slots.push_back(mkSlot(F(-gate->hx - 12.f, by + 1.f), D(0.f, -1.f), VM_STAND, VL_WORKER, 6.f, 20.f, 0.7f));
        finish(V);
    }
    // ---------------------------------------------------------------- airport forecourt: travelers between the curb and
    // the doors (both ways), farewells at the curb, security by the doors, taxi drivers at the rank on the lot's edge
    if (const World::SiteElem* term = findElem(World::SK_TERMINAL, vec2(627.5f, 1440.f), 600.f)) {
        Venue V;
        V.name = "airport forecourt";
        float fx = term->c.x + term->hy;   // landside facade (the element's axis runs along y)
        float curbX = fx + 18.5f;          // curb edge of the raised walk under the canopy
        float y0 = term->c.y - term->hx + 30.f, y1 = Min(term->c.y + term->hx - 30.f, 1480.f);
        V.c = vec2(curbX + 20.f, (y0 + y1) * 0.5f);
        V.fillR = 260.f;
        V.releaseR = 360.f;
        const vec2 east(1.f, 0.f), west(-1.f, 0.f), north(0.f, 1.f), south(0.f, -1.f);
        for (int k = 0; k < 7; k++) {
            float y = Lerp(y0, y1, (k + 0.5f) / 7.f);
            float door = y + ((k & 1) ? 6.f : -6.f);
            VenueSlot in = mkSlot(vec2(curbX - 0.8f, y + 3.f), west, VM_TRAVEL_IN, VL_TRAVELER, 5.f, 23.5f, 1.f);
            in.pos2 = vec2(fx + 0.8f, door);
            in.every = 16.f + k * 3.f;
            V.slots.push_back(in);
            if (k % 2 == 0) {
                VenueSlot out = mkSlot(vec2(curbX - 1.2f, y - 4.f), east, VM_TRAVEL_OUT, VL_TRAVELER, 5.f, 23.5f, 0.9f);
                out.pos2 = vec2(fx + 0.8f, door + 3.f);
                out.every = 24.f + k * 4.f;
                V.slots.push_back(out);
            }
        }
        for (int k = 0; k < 2; k++) {
            float y = Lerp(y0, y1, 0.3f + k * 0.35f);
            VenueSlot a = mkSlot(vec2(curbX - 2.4f, y), north, VM_FAREWELL, VL_TRAVELER, 6.f, 22.f, 0.8f);
            a.pos2 = vec2(fx + 0.8f, y + 2.f);        // the traveler goes in through the doors
            VenueSlot b = mkSlot(vec2(curbX - 2.4f, y + 0.75f), south, VM_SEEOFF, VL_CIVIL, 6.f, 22.f, 1.f);
            b.follows = true;                          // (the one who came to see them off)
            V.slots.push_back(a);
            V.slots.push_back(b);
        }
        V.slots.push_back(mkSlot(vec2(fx + 6.f, (y0 + y1) * 0.5f), east, VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(vec2(fx + 7.f, y0 + 14.f), east, VM_GUARD, VL_WORKER, 0.f, 24.f, 0.7f));
        for (int k = 0; k < 3; k++) {
            VenueSlot s = mkSlot(vec2(fx + 83.f + k * 6.5f, 1146.f), west, k == 1 ? VM_TALK : VM_LEAN, VL_CIVIL, 0.f, 24.f, 0.9f);
            s.prop = VP_TAXI;
            s.propPos = s.pos;
            s.propYaw = AI::dirYaw(north);
            s.propOff = vec2(-1.f, 0.1f);
            V.slots.push_back(s);
        }
        finish(V);
    }
    // ---------------------------------------------------------------- Sawgrass: anglers along the causeway shoulders
    // either side of the junction, birders round the observation tower at dawn, an airboat operator at the landing
    {
        vec2 marsh(-5000.f, 100.f);
        int node = -1;
        float bd = 400.f;
        for (int n = 0; n < (int)g.roads->nodes.size(); n++) {
            float d = length(g.roads->nodes[n].p - marsh);
            if (d < bd && g.map->regionAt(g.roads->nodes[n].p.x, g.roads->nodes[n].p.y) == World::REG_SAWGRASS) {
                bd = d;
                node = n;
            }
        }
        Venue V;
        V.name = "sawgrass causeway";
        V.c = node >= 0 ? g.roads->nodes[node].p : marsh;
        V.fillR = 230.f;
        V.releaseR = 340.f;
        if (node >= 0) {
            int placed = 0;
            for (int ei : g.roads->nodes[node].edges) {
                const World::RoadEdge& e = g.roads->edges[ei];
                if (e.cls == World::RC_DIRT || e.length < 150.f) continue;
                bool fromN0 = e.n0 == node;
                for (float sAt : {34.f, 61.f, 97.f, 128.f}) {
                    if (placed >= 7) break;
                    float at = fromN0 ? sAt : e.length - sAt;
                    vec3 c3 = e.posAt(at);
                    vec2 t = normalize(e.tangentAt(at).xy() + vec2(1e-5f, 0.f));
                    vec2 r = AI::rightOf(t);
                    int side = 0;   // the side with open water close by
                    for (int sd = -1; sd <= 1 && !side; sd += 2)
                        if (g.map->isWater(c3.x + r.x * sd * 14.f, c3.y + r.y * sd * 14.f)) side = sd;
                    if (!side) continue;
                    // on the bank past the shoulder where it is dry, else at the railing of the deck
                    vec2 bank = c3.xy() + r * ((float)side * (e.halfWidth + 1.4f));
                    vec2 rail = c3.xy() + r * ((float)side * (e.halfWidth - 0.35f));
                    vec2 spot = !g.map->isWater(bank.x, bank.y) ? bank : rail;
                    V.slots.push_back(mkSlot(spot, r * (float)side, placed % 3 == 2 ? VM_SIT : VM_WATCH, placed % 2 ? VL_BEACH : VL_CIVIL, 5.f, 19.5f, 0.8f));
                    placed++;
                }
            }
        }
        if (const World::SiteElem* tower = findElem(World::SK_OBS_TOWER, V.c, 700.f)) {
            for (int k = 0; k < 3; k++) {
                float a = 2.4f + k * 0.9f;
                vec2 off(cosf(a), sinf(a));
                V.slots.push_back(mkSlot(tower->c + off * (tower->hx + 2.5f + k * 1.2f), off, VM_SPOTTER, k == 1 ? VL_CIVIL : VL_BEACH, 5.3f, 10.5f, 0.85f));
            }
        }
        if (const World::SiteElem* dock = findElem(World::SK_DOCK, V.c, 400.f)) {
            vec2 ax = dock->ax, ay = perp(ax);
            vec2 end = dock->c + ax * (dock->hx - 3.f);
            VenueSlot op = mkSlot(end + ay * (dock->hy - 1.5f), ay, VM_STAND, VL_CIVIL, 6.5f, 18.5f, 0.95f);
            op.prop = VP_AIRBOAT;
            op.propPos = end + ay * (dock->hy + 3.2f);
            op.propYaw = AI::dirYaw(ax);
            V.slots.push_back(op);
            for (int k = 0; k < 2; k++)
                V.slots.push_back(mkSlot(dock->c + ax * (k * 1.1f - 2.f) + ay * (0.5f + k * 0.6f), k ? -ay : ay, k ? VM_PHONE : VM_TALK, VL_BEACH, 8.f, 17.5f, 0.6f));
        }
        finish(V);
    }
    LOG("population: venues laid out: %d (%d slots kept, %d dropped on lanes / water / buildings)", (int)gVenues.v.size(), kept, dropped);
}

bool venuePedLive(const GameWorld& g, const VenueSlot& s) {
    if (s.ped < 0 || s.ped >= (int)g.peds.size()) return false;
    const Ped& p = g.peds[s.ped];
    return p.used && p.uid == s.pedUid;
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

void releaseVenueSlot(GameWorld& g, VenueSlot& s, bool despawn) {
    if (venuePedLive(g, s)) {
        Ped& p = g.peds[s.ped];
        PedAI& pa = g.pedAI(s.ped);
        if (despawn) {
            g.despawnPed(s.ped);
        } else if (pa.activity == ACT_VENUE) {
            pa.activity = ACT_WALK;   // an ordinary pedestrian from now on
            pa.navOk = false;
            pa.stance = 0;
            pa.venue = -1;
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
        } else {
            pa.venue = -1;
        }
    }
    s.ped = -1;
    if (s.veh >= 0 && s.veh < (int)g.vehicles.size() && g.vehicles[s.veh].used && g.vehicles[s.veh].uid == s.vehUid) {
        Vehicle& v = g.vehicles[s.veh];
        bool occupied = false;
        for (int k = 0; k < 8; k++) occupied |= v.seats[k] >= 0;
        if (despawn && !occupied) g.despawnVehicle(s.veh, true);
        else v.persistent = false;   // the population recycles it like any parked car
    }
    s.veh = -1;
}

void updateVenues(GameWorld& g, vec3 pp, float dt, bool warm, float tod) {
    if (!gVenues.built) buildVenues(g);
    for (int vi = 0; vi < (int)gVenues.v.size(); vi++) {
        Venue& V = gVenues.v[vi];
        float d = length(V.c - pp.xy());
        if (!V.active && d < V.fillR) {
            V.active = true;
            V.visits++;
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
        int budget = warm ? 40 : 1;
        for (int si = 0; si < (int)V.slots.size(); si++) {
            VenueSlot& s = V.slots[si];
            s.cooldown -= dt;
            if (s.ped >= 0) {
                // still ours? (fled from gunfire, got knocked down, walked off: let the slot go, a new face later)
                bool live = venuePedLive(g, s);
                const PedAI* pa = live ? &g.pedAI(s.ped) : nullptr;
                if (!live || g.peds[s.ped].health <= 0.f || !pa || pa->venue != vi * 64 + si) {
                    releaseVenueSlot(g, s, false);
                    s.cooldown = 50.f + hashToFloat(hash32(V.visits * 131u + si * 7u + (u32)g.time)) * 50.f;
                }
                continue;
            }
            if (s.cooldown > 0.f || budget <= 0 || !venueHours(tod, s.h0, s.h1)) continue;
            if (s.follows && (si == 0 || V.slots[si - 1].ped < 0)) continue;
            u32 h = hash32(V.visits * 2654435761u + (u32)si * 40503u + (s.every > 0.f ? (u32)(g.time / Max(s.every, 1.f)) : 0u));
            if (hashToFloat(h) > s.chance) {
                s.cooldown = s.every > 0.f ? s.every : 1e9f;   // not this visit (streams: not this time round)
                continue;
            }
            // the vehicle that goes with the slot (a rig, a cab, the airboat), parked with nobody in it - out of sight
            if (s.prop != VP_NONE && (s.veh < 0 || s.veh >= (int)g.vehicles.size() || !g.vehicles[s.veh].used || g.vehicles[s.veh].uid != s.vehUid)) {
                Vehicles::VehicleClass cls = s.prop == VP_TRUCK ? Vehicles::VC_TRUCK : (s.prop == VP_TAXI ? Vehicles::VC_TAXI : Vehicles::VC_AIRBOAT);
                int model = g.findVehicleModel(cls, h >> 3);
                s.veh = -1;
                if (model >= 0) {
                    float vz = s.prop == VP_AIRBOAT ? 0.4f : g.groundHeight(s.propPos.x, s.propPos.y, pp.z + 20.f) + 0.4f;
                    bool vSeen = !warm && g.inCameraView(vec3(s.propPos, vz), 4.f) && length(s.propPos - pp.xy()) < 160.f;
                    if (!vSeen) {
                        int vid = g.spawnVehicle(model, dvec3(s.propPos.x, s.propPos.y, vz), s.propYaw, false);
                        if (vid >= 0) {
                            Vehicle& v = g.vehicles[vid];
                            v.parked = true;
                            v.persistent = true;   // (the venue lets it go when the player leaves)
                            v.sim.engineOn = false;
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
            // where they appear: travelers heading out come through a door (fine in view), everyone else out of sight
            bool out = s.mode == VM_TRAVEL_OUT;
            vec2 at = out ? s.pos2 : s.pos;
            float faceYaw = s.yaw;
            if (s.mode == VM_TRAVEL_IN && warm) at = s.pos + (s.pos2 - s.pos) * (hashToFloat(hash32(h + 5u)) * 0.85f);   // mid-way at a fade-in
            if (s.veh >= 0) {
                // beside the vehicle: its driver's side (propOff), leaning back on it or facing along it
                const Vehicle& v = g.vehicles[s.veh];
                vec3 bh = g.vassets[v.model].spec.boxHalf;
                vec2 vf = v.sim.forward().xy();
                vf = length2(vf) > 1e-6f ? normalize(vf) : vec2(0.f, 1.f);
                vec2 vr = AI::rightOf(vf);
                vec2 side = vr * (s.propOff.x >= 0.f ? 1.f : -1.f);
                at = v.sim.body.pos.toVec3().xy() + side * (bh.x + 0.5f) + vf * (s.propOff.y * bh.y);
                faceYaw = AI::dirYaw(s.mode == VM_LEAN ? side : vf);
            }
            float z = g.groundHeight(at.x, at.y, pp.z + 20.f);
            vec3 p3(at.x, at.y, z);
            bool seen = !warm && g.inCameraView(p3 + vec3(0, 0, 1.f), 1.5f) && length(at - pp.xy()) < 120.f;
            if ((seen && !out) || (!warm && length(at - pp.xy()) < 10.f) || !freeStandingSpot(g, p3)) {
                s.cooldown = 2.f;
                continue;
            }
            int ci = venueChar(g, s.look, h >> 5);
            if (ci < 0) continue;
            int id = g.spawnPed(ci, dvec3(p3), out ? AI::dirYaw(normalize(s.pos - s.pos2 + vec2(1e-4f, 0.f))) : faceYaw, FAC_CIVILIAN);
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
            pa.anchor = s.veh >= 0 ? at : s.pos;
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
                case VM_FAREWELL:
                case VM_SEEOFF: pa.stance = 7; break;
                default: pa.stance = 0; break;
            }
            // how long before the next move: pacing legs, the wait at the curb, a goodbye
            pa.actTimer = s.mode == VM_PACE ? 8.f + hashToFloat(hash32(h + 21u)) * 18.f
                                            : (s.mode == VM_TRAVEL_OUT ? 25.f + hashToFloat(hash32(h + 21u)) * 40.f
                                                                       : (s.mode == VM_FAREWELL ? 14.f + hashToFloat(hash32(h + 21u)) * 16.f : 1e5f));
            if (s.follows && si > 0 && venuePedLive(g, V.slots[si - 1])) {
                // the other half of a pair: face them, and part a moment after they go
                const VenueSlot& o = V.slots[si - 1];
                pa.actTimer = g.pedAI(o.ped).actTimer + 1.5f;
                pa.anchorYaw = AI::dirYaw(normalize(o.pos - s.pos + vec2(1e-4f, 0.f)));
                p.yaw = pa.anchorYaw;
            }
            if (s.mode == VM_PACE && (h & 1)) {   // half of them start at the far end
                std::swap(pa.anchor, pa.anchorB);
                std::swap(pa.anchorYaw, pa.anchorBYaw);
            }
            if (!out) p.yaw = faceYaw;
            s.ped = id;
            s.pedUid = p.uid;
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
    (void)dt;
    vec2 pos = p.pos.toVec3().xy();
    bool there = length(pa.anchor - pos) <= 0.35f;
    bool idle = there && pa.clipTimer <= 0.f && p.pendingAction < 0;
    u32 hq = hash32(p.uid * 5u + (u32)(g.time * 2.0));
    // a flashy car rolling by slowly: a point and a word - without leaving the post
    const Ped* pl = g.playerPed();
    if (pl && there && pl->state == PS_INVEHICLE && pl->vehicle >= 0 && p.pendingAction < 0 && pa.barkCooldown <= 0.f && pa.venueMode != VM_SIT) {
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
            if (idle) {
                // anglers hold still for long spells; birders point things out to each other
                p.pendingAction = pa.venueMode == VM_SPOTTER && hq % 3 != 0 ? Anim::CLIP_POINT : Anim::CLIP_IDLE_LOOK;
                pa.clipTimer = (pa.venueMode == VM_WATCH ? 14.f : 7.f) + hashToFloat(hq) * 10.f;
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
                    pa.activity = ACT_WALK;              // and off along the sidewalk
                    pa.navOk = false;
                    pa.venue = -1;
                    pa.stance = 0;
                    p.brain.type = BRAIN_WANDER;
                    p.brain.edge = -1;
                    return true;
                }
            }
            break;
        default:
            break;   // (stances with loops of their own: smoke, phone, talk, lean, sit)
    }
    return false;
}

