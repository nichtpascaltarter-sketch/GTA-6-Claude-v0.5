// The game side of the handset (src/ui/phone.cpp): contacts (the story cast, whose calls start story jobs), text
// messages (mission texts with "Mark on map" and action buttons, missed calls, business income), incoming story calls,
// and the game's list apps: Wheels.ps (vehicles), Dynasty Realty (safehouses and businesses), Jobs (side activities),
// Replay (finished story missions), Switch (the other protagonist) and the vehicle jobs (taxi / vigilante / paramedic).
// The app owns the UI::PhoneState and calls phoneRefresh() every gameplay frame before UI::Phone::update() and
// phoneHandle() with the returned action.
#include "missions.h"

namespace Game {
namespace mu {

enum PhoneApp : int { APP_SWITCH = 1, APP_WHEELS, APP_REALTY, APP_JOBS, APP_REPLAY, APP_VEHICLE_JOB };
enum MessageAction : int { MSGACT_NONE = 0, MSGACT_SWITCH, MSGACT_WAYPOINT_JOB };
constexpr int kContactOther = 50;     // the other protagonist

struct PhoneGameState {
    int wiredFrame = -100;            // updateFrames value of the last phoneRefresh (the handset is wired when recent)
    double lastTime = -1.0;
    u32 builtSig = 0;
    bool haveBuilt = false;
    // messages (newest last); the handset keeps its own copy, refreshed when ours changes
    std::vector<UI::PhoneMessage> messages;
    std::vector<int> messageAction;   // MessageAction per message (parallel to messages)
    std::vector<int> messageMission;  // MissionDef index the message is about (-1 none)
    int nextMessageId = 1;
    u32 messagesVersion = 1;
    // calls
    int pendingIncoming = -1;         // kCalls index waiting to ring
    int ringingCall = -1;             // kCalls index ringing now
    float ringTime = 0.f;
    int outgoingContact = -1;         // contact being dialed
    float dialTime = 0.f;
    bool inCall = false;              // connected, lines playing
    float callTime = 0.f;
    int callContact = -1;
};
PhoneGameState gPhone;

bool phoneWired() { return gMissions.updateFrames - gPhone.wiredFrame < 30; }

void phoneGameReset() {
    gPhone = PhoneGameState();
}

int castOfContact(int id) {
    if (id < 0 || id >= (int)ARRAY_COUNT(kContacts)) return -1;
    return kContacts[id].cast;
}

int contactOfCast(int cast) {
    for (int i = 0; i < (int)ARRAY_COUNT(kContacts); i++)
        if (kContacts[i].cast == cast) return i;
    return -1;
}

const char* contactRole(int id) {
    static const char* const kRoles[] = {"Brother", "Lucha's Diner", "Rook's Salvage", "Pulse FM", "Pike's Airboat Tours"};
    return id >= 0 && id < (int)ARRAY_COUNT(kRoles) ? kRoles[id] : "";
}

void addMessage(GameWorld& g, const std::string& from, const std::string& text, int contactId, bool mission, int missionDef, vec2* location,
                int action, const char* actionLabel) {
    UI::PhoneMessage m;
    m.id = gPhone.nextMessageId++;
    m.contactId = contactId;
    m.from = from;
    m.text = text;
    int hh = (int)g.env->timeOfDay, mm = (int)((g.env->timeOfDay - hh) * 60.f);
    m.time = StrFormat("%02d:%02d", hh, mm);
    m.unread = true;
    m.mission = mission;
    if (location) {
        m.hasLocation = true;
        m.location = *location;
    }
    if (actionLabel) m.actionLabel = actionLabel;
    gPhone.messages.push_back(m);
    gPhone.messageAction.push_back(action);
    gPhone.messageMission.push_back(missionDef);
    while (gPhone.messages.size() > 40) {
        gPhone.messages.erase(gPhone.messages.begin());
        gPhone.messageAction.erase(gPhone.messageAction.begin());
        gPhone.messageMission.erase(gPhone.messageMission.begin());
    }
    gPhone.messagesVersion++;
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_PHONE_MSG, 0.7f);
#endif
}

int findMessage(int id) {
    for (size_t i = 0; i < gPhone.messages.size(); i++)
        if (gPhone.messages[i].id == id) return (int)i;
    return -1;
}

const StoryCall* callForMission(const char* id) {
    for (const StoryCall& c : kCalls)
        if (strcmp(c.mission, id) == 0) return &c;
    return nullptr;
}

// Text for a newly available story mission (used for texts, missed calls and job reminders)
void missionText(GameWorld& g, int di, const StoryCall* c, bool missed) {
    const MissionDef& d = gMissions.defs[di];
    vec2 loc = d.startPos;
    std::string from = d.contact;
    std::string body;
    const char* colon = c ? strchr(c->text, ':') : nullptr;
    std::string gist = colon ? std::string(colon + 2) : std::string(d.title);
    if (missed) body = StrFormat("Missed call. %s", gist.c_str());
    else body = gist;
    bool other = d.protagonist >= 0 && d.protagonist != g.protagonistIndex;
    if (other) body += StrFormat(" (a job for %s)", d.protagonist == 0 ? "Mari" : "Dex");
    int cast = c ? c->cast : -1;
    int contact = cast >= 0 ? contactOfCast(cast) : -1;
    if (other) addMessage(g, from, body, contact, true, di, &loc, MSGACT_SWITCH, d.protagonist == 0 ? "Switch to Mari" : "Switch to Dex");
    else addMessage(g, from, body, contact, true, di, &loc, MSGACT_NONE, nullptr);
}

// ------------------------------------------------------------------------------------------------------------------
// Story calls routed through the handset (called by updateStoryCalls when a mission becomes available)
void phoneIncomingStoryCall(GameWorld& g, int callIndex, int missionDef) {
    const StoryCall& c = kCalls[callIndex];
    if (c.cast < 0) {
        missionText(g, missionDef, &c, false);
        const char* colon = strchr(c.text, ':');
        g.notify(colon ? std::string(c.text, colon - c.text) : std::string("MESSAGE"), colon ? std::string(colon + 2) : std::string(c.text));
        return;
    }
    gPhone.pendingIncoming = callIndex;
}

// the call's lines, then a waypoint to the job
void playStoryCallLines(GameWorld& g, const StoryCall& c) {
    for (const char* l : c.lines) {
        if (!l) break;
        if (l[0] == '>') sayMe(g, l + 1);
        else phoneLine(g, c.cast, l);
    }
    int di = gMissions.findDef(c.mission);
    if (di >= 0) setWaypoint(g, gMissions.defs[di].startPos);
}

// outgoing call connected: an undelivered story call for this contact, a job reminder, or small talk
void connectOutgoing(GameWorld& g, int contact) {
    if (contact == kContactOther) {
        callContact(g, kContactOther);
        return;
    }
    int cast = castOfContact(contact);
    const MissionDef* d = nextMissionOf(g, kContacts[contact].who);
    if (d) {
        const StoryCall* c = callForMission(d->id);
        if (c && c->cast == cast) {
            playStoryCallLines(g, *c);
            markCall(g, d->storyIndex);
            return;
        }
    }
    callContact(g, contact);
    if (d) setWaypoint(g, d->startPos);
}

// ------------------------------------------------------------------------------------------------------------------
std::vector<UI::PhoneListItem> toPhoneItems(const std::vector<MenuItem>& in) {
    std::vector<UI::PhoneListItem> out;
    for (const MenuItem& m : in) {
        UI::PhoneListItem it;
        it.id = m.id;
        it.label = m.label;
        it.detail = m.detail;
        it.right = m.right.empty() && m.checked ? std::string("OWNED") : m.right;
        it.price = m.checked ? -1 : m.price;
        it.enabled = m.enabled;
        out.push_back(it);
    }
    return out;
}

const char* vehicleJobHere(GameWorld& g) {
    int pv = g.playerVehicle();
    if (pv < 0 || gMissions.active || g.peds[g.player].seat != 0) return nullptr;
    Vehicles::VehicleClass c = g.vassets[g.vehicles[pv].model].spec.cls;
    return c == Vehicles::VC_TAXI ? "taxi" : (c == Vehicles::VC_POLICE ? "vigilante" : (c == Vehicles::VC_AMBULANCE ? "paramedic" : nullptr));
}

void buildPhone(GameWorld& g, UI::PhoneState& ph) {
    // ---- contacts
    ph.contacts.clear();
    for (int i = 0; i < (int)ARRAY_COUNT(kContacts); i++) {
        const ContactInfo& c = kContacts[i];
        if (c.unlock >= 0 && !storyDone(g, c.unlock)) continue;
        UI::PhoneContact pc;
        pc.id = i;
        pc.name = c.who;
        const MissionDef* d = nextMissionOf(g, c.who);
        pc.mission = d != nullptr;
        pc.subtitle = d ? StrFormat("New job: %s", d->title) : std::string(contactRole(i));
        pc.enabled = !gMissions.active;
        pc.color = kCast[c.cast].color;
        ph.contacts.push_back(pc);
    }
    if (switchUnlocked(g) && storyDone(g, SF_DRY_DOCK)) {
        UI::PhoneContact pc;
        pc.id = kContactOther;
        pc.name = g.protagonistIndex == 0 ? "Dex" : "Mari";
        pc.subtitle = g.protagonistIndex == 0 ? "Repo man" : "Ortega Boatyard";
        pc.enabled = !gMissions.active;
        pc.color = g.protagonistIndex == 0 ? kColDex : kColMari;
        ph.contacts.push_back(pc);
    }
    // ---- apps
    ph.apps.clear();
    if (switchUnlocked(g)) {
        UI::PhoneListApp a;
        a.id = APP_SWITCH;
        a.name = g.protagonistIndex == 0 ? "Switch to Dex" : "Switch to Mari";
        a.glyph = UI::PG_SWITCH;
        a.action = true;
        a.enabled = !gMissions.active || gMissions.allowSwitch;
        ph.apps.push_back(a);
    }
    if (const char* job = vehicleJobHere(g)) {
        UI::PhoneListApp a;
        a.id = APP_VEHICLE_JOB;
        a.name = strcmp(job, "taxi") == 0 ? "Start Taxi Fares" : (strcmp(job, "vigilante") == 0 ? "Start Vigilante" : "Start Paramedic");
        a.glyph = strcmp(job, "taxi") == 0 ? UI::PG_CAR : UI::PG_BRIEFCASE;
        a.action = true;
        a.enabled = g.pinfo.wanted == 0;
        ph.apps.push_back(a);
    }
    {
        UI::PhoneListApp a;
        a.id = APP_WHEELS;
        a.name = "Wheels.ps";
        a.subtitle = gMissions.active ? std::string("Not during a mission") : StrFormat("Cash $%lld", g.pinfo.money);
        a.glyph = UI::PG_CAR;
        a.enabled = !gMissions.active;
        a.items = toPhoneItems(dealershipItems(g));
        ph.apps.push_back(a);
    }
    {
        UI::PhoneListApp a;
        a.id = APP_REALTY;
        a.name = "Dynasty Realty";
        a.subtitle = "Safehouses and businesses. Select one to mark it.";
        a.glyph = UI::PG_HOUSE;
        a.items = toPhoneItems(realtyItems(g));
        ph.apps.push_back(a);
    }
    {
        UI::PhoneListApp a;
        a.id = APP_JOBS;
        a.name = "Jobs";
        a.subtitle = "Races, courier runs, the range, flight school. Select one to mark it.";
        a.glyph = UI::PG_TROPHY;
        a.items = toPhoneItems(jobItems(g));
        ph.apps.push_back(a);
    }
    {
        UI::PhoneListApp a;
        a.id = APP_REPLAY;
        a.name = "Replay";
        a.subtitle = "Play a finished story mission again (no rewards).";
        a.glyph = UI::PG_REPLAY;
        a.enabled = !gMissions.active;
        a.items = toPhoneItems(replayItems(g));
        ph.apps.push_back(a);
    }
}

u32 phoneSignature(GameWorld& g) {
    u32 h = hash32((u32)g.protagonistIndex * 977u + (gMissions.active ? 1u : 0u) + (gMissions.allowSwitch ? 2u : 0u));
    h = hash32(h ^ (u32)(g.pinfo.money & 0xffffffffll));
    h = hash32(h ^ (u32)g.ownedVehicleModels.size() * 131u ^ (u32)g.gameDay * 7u ^ (u32)g.pinfo.wanted * 3u);
    for (size_t i = 0; i < g.storyFlags.size(); i++)
        if (g.storyFlags[i]) h = hash32(h ^ ((u32)i * 2654435761u + (u32)g.storyFlags[i]));
    const char* job = vehicleJobHere(g);
    h = hash32(h ^ (job ? (u32)job[0] : 0u));
    return h;
}

// Every frame: detect a new game/load, drive the call state machine, rebuild the lists when something changed.
void phoneRefreshImpl(GameWorld& g, UI::PhoneState& ph) {
    double now = g.time;
    float dt = gPhone.lastTime < 0.0 ? 0.f : (float)Clamp(now - gPhone.lastTime, 0.0, 0.25);
    gPhone.lastTime = now;
    gPhone.wiredFrame = gMissions.updateFrames;
    bool busy = gMissions.active || g.mInCutscene() || !g.playerControl;
    // ---- outgoing call: ring, then connect and play the lines
    if (gPhone.outgoingContact >= 0) {
        gPhone.dialTime += dt;
        ph.call = UI::CALL_OUTGOING;
        if (gPhone.dialTime > (gMissions.test.active ? 0.3f : 2.2f)) {
            int c = gPhone.outgoingContact;
            gPhone.outgoingContact = -1;
            gMissions.lines.clear();
            connectOutgoing(g, c);
            gPhone.inCall = true;
            gPhone.callTime = 0.f;
            gPhone.callContact = c;
            ph.call = UI::CALL_ACTIVE;
        }
    }
    // ---- incoming story call: ring when the player is free, missed after a while
    if (gPhone.pendingIncoming >= 0 && gPhone.ringingCall < 0 && !gPhone.inCall && gPhone.outgoingContact < 0 && !busy && !g.mTalking()) {
        gPhone.ringingCall = gPhone.pendingIncoming;
        gPhone.pendingIncoming = -1;
        gPhone.ringTime = 0.f;
        const StoryCall& c = kCalls[gPhone.ringingCall];
        ph.call = UI::CALL_INCOMING;
        ph.callName = kCast[c.cast].name;
        ph.callContactId = contactOfCast(c.cast);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PHONE_RING, 0.8f);
#endif
    }
    if (gPhone.ringingCall >= 0) {
        gPhone.ringTime += dt;
        if (busy || gPhone.ringTime > 16.f) {
            // missed: leave a text with the gist and a call-back button
            const StoryCall& c = kCalls[gPhone.ringingCall];
            int di = gMissions.findDef(c.mission);
            if (di >= 0) missionText(g, di, &c, true);
            gPhone.ringingCall = -1;
            ph.call = UI::CALL_NONE;
        } else {
            ph.call = UI::CALL_INCOMING;
        }
    }
    // ---- connected call: ends when its lines are done
    if (gPhone.inCall) {
        gPhone.callTime += dt;
        ph.call = UI::CALL_ACTIVE;
        ph.callSeconds = gPhone.callTime;
        if (!g.mTalking() && gMissions.lines.empty()) {
            gPhone.inCall = false;
            gPhone.callContact = -1;
            ph.call = UI::CALL_NONE;
        }
    }
    // ---- lists
    u32 sig = phoneSignature(g) ^ hash32(gPhone.messagesVersion);
    if (!gPhone.haveBuilt || sig != gPhone.builtSig) {
        gPhone.haveBuilt = true;
        gPhone.builtSig = sig;
        buildPhone(g, ph);
        // keep the handset's read state, add new messages
        std::vector<UI::PhoneMessage> merged;
        for (const UI::PhoneMessage& m : gPhone.messages) {
            UI::PhoneMessage cp = m;
            for (const UI::PhoneMessage& o : ph.messages)
                if (o.id == m.id) cp.unread = o.unread;
            merged.push_back(cp);
        }
        ph.messages = merged;
    }
}

bool phoneHandleImpl(GameWorld& g, UI::PhoneState& ph, const UI::PhoneAction& a) {
    switch (a.type) {
        case UI::PA_CALL: {
            bool ok = a.id == kContactOther || (a.id >= 0 && a.id < (int)ARRAY_COUNT(kContacts));
            if (!ok || gMissions.active || gPhone.inCall) {
                ph.call = UI::CALL_NONE;
                ph.toast = gMissions.active ? "Not during a mission" : "Busy";
                return true;
            }
            gPhone.outgoingContact = a.id;
            gPhone.dialTime = 0.f;
            ph.call = UI::CALL_OUTGOING;
            ph.callContactId = a.id;
            ph.callName = a.id == kContactOther ? (g.protagonistIndex == 0 ? "Dex" : "Mari") : kContacts[a.id].who;
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_PHONE_RING, 0.5f);
#endif
            return true;
        }
        case UI::PA_ANSWER: {
            if (gPhone.ringingCall < 0) {
                ph.call = UI::CALL_NONE;
                return true;
            }
            const StoryCall& c = kCalls[gPhone.ringingCall];
            gPhone.ringingCall = -1;
            gMissions.lines.clear();
            playStoryCallLines(g, c);
            gPhone.inCall = true;
            gPhone.callTime = 0.f;
            gPhone.callContact = contactOfCast(c.cast);
            ph.call = UI::CALL_ACTIVE;
            return true;
        }
        case UI::PA_DECLINE: {
            if (gPhone.ringingCall >= 0) {
                const StoryCall& c = kCalls[gPhone.ringingCall];
                int di = gMissions.findDef(c.mission);
                if (di >= 0) missionText(g, di, &c, true);
                gPhone.ringingCall = -1;
            }
            ph.call = UI::CALL_NONE;
            return true;
        }
        case UI::PA_HANG_UP:
            gPhone.outgoingContact = -1;
            if (gPhone.inCall && !gMissions.active) {
                gMissions.lines.clear();
                if (gMissions.lineSound != 0 && gMissions.lineSound != 0xffffffffu) Audio::stop(gMissions.lineSound);
                gMissions.lineSound = 0;
                gMissions.lineTimer = 0.f;
            }
            gPhone.inCall = false;
            gPhone.callContact = -1;
            ph.call = UI::CALL_NONE;
            return true;
        case UI::PA_READ_MESSAGE: {
            int i = findMessage(a.id);
            if (i >= 0) gPhone.messages[i].unread = false;
            return true;
        }
        case UI::PA_MESSAGE_ACTION: {
            int i = findMessage(a.id);
            if (i < 0) return true;
            int act = gPhone.messageAction[i];
            if (act == MSGACT_SWITCH) {
                int di = gPhone.messageMission[i];
                int who = di >= 0 ? gMissions.defs[di].protagonist : (g.protagonistIndex == 0 ? 1 : 0);
                if (who >= 0 && who != g.protagonistIndex && switchUnlocked(g) && !gMissions.active) {
                    ph.open = false;
                    switchProtagonist(g, who, false);
                } else if (who == g.protagonistIndex) {
                    ph.toast = "Already here";
                } else {
                    ph.toast = "Not available right now";
                }
            }
            return true;
        }
        case UI::PA_APP_ACTION:
            if (a.app == APP_SWITCH) {
                if (!switchUnlocked(g) || (gMissions.active && !gMissions.allowSwitch)) {
                    ph.toast = "Not available right now";
                    return true;
                }
                ph.open = false;
                switchProtagonist(g, g.protagonistIndex == 0 ? 1 : 0, false);
                return true;
            }
            if (a.app == APP_VEHICLE_JOB) {
                const char* job = vehicleJobHere(g);
                int di = job ? gMissions.findDef(job) : -1;
                if (di >= 0 && g.pinfo.wanted == 0) {
                    ph.open = false;
                    gMissions.startCheckpoint = 0;
                    g.startMission(di);
                } else {
                    ph.toast = g.pinfo.wanted > 0 ? "Lose the police first" : "Not available right now";
                }
                return true;
            }
            return false;
        case UI::PA_APP_ITEM:
            switch (a.app) {
                case APP_WHEELS: {
                    if (gMissions.active || a.id < 0 || a.id >= (int)g.vassets.size()) return true;
                    const Vehicles::VehicleModel& m = g.vassets[a.id].spec;
                    bool owned = std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), a.id) != g.ownedVehicleModels.end();
                    if (owned) {
                        ph.toast = "Already in your garages";
                        return true;
                    }
                    if (g.pinfo.money < m.price) {
                        ph.toast = "Not enough money";
                        return true;
                    }
                    money(g, -m.price);
                    g.ownedVehicleModels.push_back(a.id);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_PURCHASE, 0.8f);
#endif
                    ph.toast = StrFormat("%s delivered to your garages", m.name.c_str());
                    addMessage(g, "Wheels.ps", StrFormat("Your %s %s is ready in every safehouse garage. Thanks for shopping online!", m.maker.c_str(),
                                                         m.name.c_str()),
                               -1, false, -1, nullptr, MSGACT_NONE, nullptr);
                    g.socialReport(UI::TE_PURCHASE, dvec3(playerPos(g)), m.name.c_str(), (float)m.price);
                    return true;
                }
                case APP_REALTY:
                    if (a.id >= 100 && a.id - 100 < (int)gShops.businesses.size()) setWaypoint(g, gShops.businesses[a.id - 100].marker.xy());
                    else if (a.id >= 0 && a.id < (int)gShops.safehouses.size()) setWaypoint(g, gShops.safehouses[a.id].save.xy());
                    ph.toast = "Marked on your map";
                    return true;
                case APP_JOBS:
                    if (a.id >= 0 && a.id < (int)gMissions.defs.size()) {
                        setWaypoint(g, gMissions.defs[a.id].startPos);
                        ph.toast = "Marked on your map";
                    }
                    return true;
                case APP_REPLAY:
                    if (a.id >= 0 && a.id < (int)gMissions.defs.size() && !gMissions.active) {
                        const MissionDef& d = gMissions.defs[a.id];
                        ph.open = false;
                        if (d.protagonist >= 0 && d.protagonist != g.protagonistIndex) switchProtagonist(g, d.protagonist, true);
                        gMissions.replay = true;
                        gMissions.startCheckpoint = 0;
                        g.fadeIn(1.5f);
                        g.startMission(a.id);
                    }
                    return true;
                default: return false;
            }
        default: return false;
    }
}

}  // namespace mu

void phoneRefresh(GameWorld& g, UI::PhoneState& ph) { mu::phoneRefreshImpl(g, ph); }

bool phoneHandle(GameWorld& g, UI::PhoneState& ph, const UI::PhoneAction& a) { return mu::phoneHandleImpl(g, ph, a); }

}  // namespace Game
