# AI agent: seat claims for door entries (Codex P2 on PR #46, ai.cpp ai_board::begin): a seat somebody is getting into
# through its door (PS_ENTERING, the clip running) counts as taken for every seat choice that leads to ai_board::begin -
# Vehicle::seats only has them at the end of the clip. Applied the same way to HEAD's and the WIP's files.
# Usage: seatfix.py DIR [events]   (DIR holds ai.cpp, pedai.cpp, police.cpp; "events": also DIR/events.cpp - the set 17 WIP)
import sys, os
d = sys.argv[1]
withEvents = len(sys.argv) > 2 and sys.argv[2] == "events"


def patch(name, pairs):
    p = os.path.join(d, name)
    s = open(p, encoding="utf-8").read()
    for old, new, n in pairs:
        c = s.count(old)
        assert c == n, "%s: %d x (want %d): %s" % (name, c, n, old[:90])
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8", newline="").write(s)
    print("ok", p)


# ---- ai.cpp: the claim test and a claim-aware free seat, in namespace ai_board
patch("ai.cpp", [(
    "namespace ai_board {\n\n",
    """namespace ai_board {

// a seat somebody is getting into through its door this moment (PS_ENTERING, the clip running): Vehicle::seats only has
// them once the clip is over (step's warp), so the seat choices that lead to begin() ask this as well - two officers back
// to one car went for the driver's door together and both played the clip into the one seat. The claim ends with the
// clip: in, called off (step), or the claimant down or gone
bool claimed(const GameWorld& g, int veh, int seat, int except) {
    if (veh < 0 || veh >= (int)g.vehicles.size()) return false;
    u32 vu = g.vehicles[veh].uid;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& q = g.peds[i];
        const PedAI& qa = g.ai.ped[i];
        if (i != except && q.used && qa.uid == q.uid && q.state == PS_ENTERING && qa.boardVeh == veh && qa.boardVehUid == vu && qa.boardSeat == seat)
            return true;
    }
    return false;
}

// GameWorld::freeSeat leaving out the seats somebody is getting into: the driver's (0), or a passenger's; -1 if none
int openSeat(const GameWorld& g, int veh, bool driver, int except) {
    if (veh < 0 || veh >= (int)g.vehicles.size() || !g.vehicles[veh].used) return -1;
    const Vehicle& v = g.vehicles[veh];
    int ns = Min((int)g.vassets[v.model].spec.seats.size(), 8);
    if (driver) return ns > 0 && v.seats[0] < 0 && !claimed(g, veh, 0, except) ? 0 : -1;
    for (int s = 1; s < ns; s++)
        if (v.seats[s] < 0 && !claimed(g, veh, s, except)) return s;
    return -1;
}

""", 1)])

# ---- pedai.cpp: every seat choice before ai_board::begin
patch("pedai.cpp", [
    ("""namespace ai_board {
bool begin(GameWorld& g, int id, int veh, int seat);
void boarded(GameWorld& g, int id, int veh, int seat);
}  // namespace ai_board""",
     """namespace ai_board {
bool begin(GameWorld& g, int id, int veh, int seat);
void boarded(GameWorld& g, int id, int veh, int seat);
bool claimed(const GameWorld& g, int veh, int seat, int except);       // (somebody getting into that seat through its door)
int openSeat(const GameWorld& g, int veh, bool driver, int except);    // (freeSeat without those)
}  // namespace ai_board""", 1),
    # a ride along: a passenger seat nobody is in, on the way to, or getting into
    ("                                if (vehicles[rc].seats[s2] >= 0) continue;",
     "                                if (vehicles[rc].seats[s2] >= 0 || ai_board::claimed(*this, rc, s2, id)) continue;", 1),
    # a taxi fare: the kerb-side back seat, else any
    ("                                if (v.seats[k] < 0 && !ts.seats[k].exitLeft) seat = k;\n"
     "                            if (seat < 0) seat = freeSeat(tv, false);",
     "                                if (v.seats[k] < 0 && !ts.seats[k].exitLeft && !ai_board::claimed(*this, tv, k, id)) seat = k;\n"
     "                            if (seat < 0) seat = ai_board::openSeat(*this, tv, false, id);", 1),
    # an emergency crew back to its vehicle
    ("                                int seat = freeSeat(hv, false);",
     "                                int seat = ai_board::openSeat(*this, hv, false, id);   // (two medics: one seat each)", 1),
    # somebody under arrest into the back
    ("                            if (seat < 0 && k < n && cv.seats[k] < 0) seat = k;",
     "                            if (seat < 0 && k < n && cv.seats[k] < 0 && !ai_board::claimed(*this, car, k, id)) seat = k;", 1),
    # the end of a coffee break: the seat they got out of, else another
    ("                    int seat = pa.breakSeat < ns && hvv.seats[pa.breakSeat] < 0 ? pa.breakSeat : -1;\n"
     "                    for (int k = 0; k < ns && seat < 0; k++)\n"
     "                        if (hvv.seats[k] < 0) seat = k;",
     "                    int seat = pa.breakSeat < ns && hvv.seats[pa.breakSeat] < 0 && !ai_board::claimed(*this, hv, pa.breakSeat, id) ? pa.breakSeat : -1;\n"
     "                    for (int k = 0; k < ns && seat < 0; k++)\n"
     "                        if (hvv.seats[k] < 0 && !ai_board::claimed(*this, hv, k, id)) seat = k;", 1),
    # road rage, an errand: back to the own car's wheel
    ("vehicles[hv].used && !vehicles[hv].exploded && vehicles[hv].seats[0] < 0;",
     "vehicles[hv].used && !vehicles[hv].exploded && vehicles[hv].seats[0] < 0 &&\n"
     "                                 !ai_board::claimed(*this, hv, 0, id);", 2),
    # an owner driving off
    ("                                 vehicles[hv].seats[0] < 0 && vehicles[hv].parked && !vehicles[hv].playerUsed;",
     "                                 vehicles[hv].seats[0] < 0 && !ai_board::claimed(*this, hv, 0, id) && vehicles[hv].parked && !vehicles[hv].playerUsed;", 1),
])

# ---- police.cpp: officers back to their car (the case Codex traced), the car brought round, the chase jump-in
patch("police.cpp", [
    ("""namespace ai_board {   // ai.cpp: getting into a car through its door (GameWorld::startCarEntry; PS_ENTERING while the clip runs)
bool begin(GameWorld& g, int id, int veh, int seat);
void boarded(GameWorld& g, int id, int veh, int seat);
}  // namespace ai_board""",
     """namespace ai_board {   // ai.cpp: getting into a car through its door (GameWorld::startCarEntry; PS_ENTERING while the clip runs)
bool begin(GameWorld& g, int id, int veh, int seat);
void boarded(GameWorld& g, int id, int veh, int seat);
bool claimed(const GameWorld& g, int veh, int seat, int except);       // (somebody getting into that seat through its door)
int openSeat(const GameWorld& g, int veh, bool driver, int except);    // (freeSeat without those)
}  // namespace ai_board""", 1),
    ("                if (vehicles[hv].seats[0] < 0 && vehAI(hv).task != PT_TRANSPORT && !vehicles[hv].playerUsed && !isAircraft(hv) && !isBoat(hv))",
     "                if (vehicles[hv].seats[0] < 0 && !ai_board::claimed(*this, hv, 0, id) && vehAI(hv).task != PT_TRANSPORT && !vehicles[hv].playerUsed &&\n"
     "                    !isAircraft(hv) && !isBoat(hv))", 1),
    ("                bool driverFree = vehicles[hv].seats[0] < 0;\n"
     "                int seat = driverFree ? 0 : freeSeat(hv, false);",
     "                // (the wheel if nobody is at it or getting in there - two back at once: one each, not both through the\n"
     "                //  driver's door into the one seat)\n"
     "                int seat = ai_board::openSeat(*this, hv, true, id);\n"
     "                if (seat < 0) seat = ai_board::openSeat(*this, hv, false, id);", 1),
    ("            int seat = hv.seats[0] < 0 ? 0 : freeSeat(pa.homeVeh, false);",
     "            int seat = ai_board::openSeat(*this, pa.homeVeh, true, id);\n"
     "            if (seat < 0) seat = ai_board::openSeat(*this, pa.homeVeh, false, id);", 1),
])

if withEvents:
    # ---- events.cpp (set 17): the boxed-in crash driver's way in by the passenger seat
    patch("events.cpp", [
        ("""namespace ai_board {
bool beginThen(GameWorld& g, int id, int veh, int seat, u8 then);
}  // namespace ai_board""",
         """namespace ai_board {
bool beginThen(GameWorld& g, int id, int veh, int seat, u8 then);
bool claimed(const GameWorld& g, int veh, int seat, int except);   // (somebody getting into that seat through its door)
}  // namespace ai_board""", 1),
        ("                            if (ps > 0 && v.seats[ps] < 0 && dot(bp, AI::rightOf(bf)) > spec.boxHalf.x * 0.5f && !aiCarEndFree(*this, car, 1.f) &&",
         "                            if (ps > 0 && v.seats[ps] < 0 && !ai_board::claimed(*this, car, ps, me) && dot(bp, AI::rightOf(bf)) > spec.boxHalf.x * 0.5f &&\n"
         "                                !aiCarEndFree(*this, car, 1.f) &&", 1),
    ])
