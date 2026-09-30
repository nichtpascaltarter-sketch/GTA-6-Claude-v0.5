// Melee combat: timed attacks (wind-up -> contact -> recovery) with light-attack combos, heavy attacks that break
// guards, blocking (a block raised just before the contact is a perfect block: the attacker staggers and the
// defender gets a counter-attack window), dodges with brief invulnerability, lunges that close the last metre to the
// opponent, hit reactions, knock-downs and knock-outs. Shared by the player (player.cpp: lock-on, buttons) and every
// NPC that attacks through fireWeapon with fists, a knife or a bat; NPCs in a fight also block and dodge.
#include "gameworld.h"

namespace Game {

namespace melee_detail {

enum MeleeMoveId { MM_JAB_L = 0, MM_JAB_R, MM_FINISHER, MM_HEAVY, MM_SLASH, MM_STAB, MM_SWING, MM_SMASH, MM_COUNTER, MM_COUNT };

struct MeleeMove {
    int clip;           // Anim::Clip played by the attacker
    float contact;      // s from the start to the contact frame
    float duration;     // s until the move ends (recovery included)
    float chainAt;      // s after which a queued attack cancels the recovery
    float reach;        // m from the attacker's chest
    float arcCos;       // cos of the half-angle of the hit cone
    float damageMul;    // x weapon damage
    float knockChance;  // chance to knock the target down on a clean hit
    float stagger;      // s the target cannot act after a clean hit
    float lunge;        // m the attacker may step in toward the target during the wind-up
    bool heavy;         // breaks a (non-perfect) guard
    bool kick;          // kick sound / low contact
};

const MeleeMove kMoves[MM_COUNT] = {
    //  clip                 contact duration chain  reach  arc    dmg    knock  stagger lunge heavy  kick
    {Anim::CLIP_PUNCH_L, 0.15f, 0.42f, 0.24f, 1.25f, 0.5f, 1.0f, 0.0f, 0.3f, 0.7f, false, false},     // jab
    {Anim::CLIP_PUNCH_R, 0.16f, 0.46f, 0.26f, 1.3f, 0.5f, 1.15f, 0.0f, 0.35f, 0.7f, false, false},    // cross
    {Anim::CLIP_KICK, 0.26f, 0.72f, 0.5f, 1.45f, 0.45f, 1.9f, 0.45f, 0.9f, 0.9f, false, true},       // combo finisher
    {Anim::CLIP_KICK, 0.36f, 0.85f, 0.6f, 1.5f, 0.45f, 2.4f, 0.7f, 1.1f, 1.1f, true, true},          // heavy
    {Anim::CLIP_PUNCH_R, 0.14f, 0.4f, 0.22f, 1.35f, 0.4f, 1.0f, 0.0f, 0.35f, 0.7f, false, false},     // knife slash
    {Anim::CLIP_PUNCH_R, 0.3f, 0.75f, 0.5f, 1.4f, 0.35f, 1.9f, 0.25f, 0.9f, 0.9f, true, false},       // knife stab
    {Anim::CLIP_PUNCH_R, 0.24f, 0.62f, 0.4f, 1.7f, 0.35f, 1.0f, 0.35f, 0.6f, 0.6f, false, false},     // bat swing
    {Anim::CLIP_PUNCH_R, 0.4f, 0.95f, 0.7f, 1.75f, 0.35f, 1.8f, 0.75f, 1.2f, 0.8f, true, false},      // bat smash
    {Anim::CLIP_PUNCH_R, 0.12f, 0.5f, 0.3f, 1.4f, 0.3f, 1.6f, 0.5f, 1.0f, 0.5f, true, false},         // counter
};

float meleeWrap(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

bool isMeleeWeapon(WeaponType w) { return weaponInfo(w).clipSize == 0; }

}  // namespace melee_detail

using namespace melee_detail;

// Best opponent in front of the ped (fwd = facing when zero), preferring whoever is fighting it.
int GameWorld::meleeAutoTarget(const Ped& p, float maxDist, float minCos, vec2 fwd) const {
    if (length(fwd) < 1e-3f) fwd = vec2(-sinf(p.yaw), cosf(p.yaw));
    int self = (int)(&p - peds.data());
    std::vector<int> list;
    pedsNear(p.pos.toVec3().xy(), maxDist + 0.5f, list);
    int best = -1;
    float bestScore = -1e9f;
    for (int o : list) {
        if (o == self) continue;
        const Ped& t = peds[o];
        if (!t.used || t.health <= 0.f || t.state != PS_ONFOOT || t.ragdoll) continue;
        vec3 d = rel(t.pos, p.pos);
        if (fabsf(d.z) > 1.6f) continue;
        vec2 dh(d.x, d.y);
        float dist = length(dh);
        if (dist > maxDist || dist < 0.05f) continue;
        float c = dot(dh / dist, fwd);
        if (c < minCos) continue;
        bool hostile = t.meleeTarget == self || (t.brain.type == BRAIN_COMBAT && t.brain.target == self) ||
                       (p.isPlayer && (t.faction == FAC_ENEMY || t.faction == FAC_POLICE));
        float score = c * 1.5f - dist * 0.35f + (hostile ? 0.8f : 0.f);
        if (score > bestScore) {
            bestScore = score;
            best = o;
        }
    }
    return best;
}

bool GameWorld::meleeStart(int pid, bool heavy) {
    if (pid < 0 || pid >= (int)peds.size() || !peds[pid].used) return false;
    Ped& p = peds[pid];
    if (p.state != PS_ONFOOT || p.health <= 0.f || p.ragdoll || p.meleeStagger > 0.f || p.dodgeT >= 0.f) return false;
    if (!isMeleeWeapon(p.weapon)) return false;
    if (p.meleeMove >= 0) {
        // mid-move: chain the next attack when the input comes after the first half of the wind-up
        if (p.meleeT >= kMoves[p.meleeMove].contact * 0.5f) p.meleeQueued = heavy ? 2 : 1;
        return false;
    }
    int m;
    if (time < p.counterUntil) {
        m = MM_COUNTER;
        p.counterUntil = -100.0;
        p.meleeCombo = 0;
    } else if (p.weapon == WPN_FISTS) {
        if (heavy) {
            m = MM_HEAVY;
            p.meleeCombo = 0;
        } else {
            int step = time - p.meleeLastEnd < 0.45 ? p.meleeCombo + 1 : 0;
            if (step > 2) step = 0;
            m = step == 0 ? MM_JAB_L : (step == 1 ? MM_JAB_R : MM_FINISHER);
            p.meleeCombo = step;
        }
    } else if (p.weapon == WPN_KNIFE) {
        m = heavy ? MM_STAB : MM_SLASH;
    } else {
        m = heavy ? MM_SMASH : MM_SWING;
    }
    p.meleeMove = m;
    p.meleeT = 0.f;
    p.meleeHitDone = false;
    p.meleeQueued = 0;
    p.meleeSerial++;
    p.blocking = false;
    p.pendingAction = kMoves[m].clip;
    // opponent: the current one while still close, else the best ped in front
    int t = p.meleeTarget;
    if (t >= 0 && (t >= (int)peds.size() || !peds[t].used || peds[t].health <= 0.f || peds[t].state != PS_ONFOOT || peds[t].ragdoll ||
                   length(rel(peds[t].pos, p.pos)) > 3.5f))
        t = -1;
    if (t < 0) t = meleeAutoTarget(p, 2.8f, 0.3f);
    p.meleeTarget = t;
    if (t >= 0) {
        Ped& o = peds[t];
        vec3 d = rel(o.pos, p.pos);
        if (length(vec2(d.x, d.y)) > 0.05f) p.yaw = atan2f(-d.x, d.y);
        // an NPC in a fight defends itself: block or dodge after a short reaction time (once per attack)
        bool fighting = o.brain.type == BRAIN_COMBAT || o.faction == FAC_POLICE || o.faction == FAC_ENEMY || o.meleeTarget == pid;
        if (!o.isPlayer && fighting && o.meleeMove < 0 && o.meleeStagger <= 0.f && o.dodgeT < 0.f) {
            u32 h = hash32(o.uid * 31u + p.meleeSerial * 7u + (u32)pid * 131u);
            float skill = Saturate(o.brain.accuracy);
            float pBlock = 0.16f + 0.3f * skill, pDodge = 0.06f + 0.1f * skill;
            if (kMoves[m].heavy) {
                pBlock *= 0.5f;
                pDodge *= 1.8f;
            }
            float r = hashToFloat(h);
            o.meleeReact = r < pBlock ? 1 : (r < pBlock + pDodge ? 2 : 0);
            o.meleeReactAt = time + 0.07 + 0.2 * hashToFloat(hash32(h + 17u));
            o.meleeTarget = pid;
        }
    }
    return true;
}

void GameWorld::meleeBlock(int pid, bool on) {
    if (pid < 0 || pid >= (int)peds.size()) return;
    Ped& p = peds[pid];
    if (!on) {
        p.blocking = false;
        return;
    }
    if (p.blocking || p.meleeMove >= 0 || p.meleeStagger > 0.f || p.dodgeT >= 0.f || p.state != PS_ONFOOT || p.ragdoll) return;
    p.blocking = true;
    p.blockStart = time;
    p.pendingAction = Anim::CLIP_BLOCK;
}

bool GameWorld::meleeDodge(int pid, vec2 worldDir) {
    if (pid < 0 || pid >= (int)peds.size()) return false;
    Ped& p = peds[pid];
    if (p.dodgeT >= 0.f || p.meleeStagger > 0.f || p.state != PS_ONFOOT || !p.grounded || p.ragdoll) return false;
    if (p.meleeMove >= 0 && !p.meleeHitDone) return false;   // committed to the swing until its contact
    p.meleeMove = -1;
    p.meleeQueued = 0;
    p.blocking = false;
    vec2 back(sinf(p.yaw), -cosf(p.yaw));
    p.dodgeDir = length(worldDir) > 0.1f ? normalize(worldDir) : back;
    p.dodgeT = 0.f;
#ifdef HAVE_AUDIO
    Audio::play(Audio::SFX_STEP_CONCRETE, p.pos.toVec3(), 0.6f, 0.8f);
#endif
    return true;
}

void GameWorld::updateMelee(int pid, float dt) {
    Ped& p = peds[pid];
    p.meleeStagger = Max(0.f, p.meleeStagger - dt);
    if (p.state != PS_ONFOOT || p.health <= 0.f || p.ragdoll) {
        p.meleeMove = -1;
        p.meleeQueued = 0;
        p.blocking = false;
        p.dodgeT = -1.f;
        p.meleeReact = 0;
        if (!p.isPlayer && p.animIn.stance == 19) p.animIn.stance = 0;
        return;
    }
    // scheduled AI defence
    if (p.meleeReact && time >= p.meleeReactAt) {
        int react = p.meleeReact;
        p.meleeReact = 0;
        int a = p.meleeTarget;
        if (a >= 0 && a < (int)peds.size() && peds[a].used && peds[a].meleeMove >= 0 && !peds[a].meleeHitDone) {
            if (react == 1) {
                meleeBlock(pid, true);
                p.blockUntil = time + 0.55;
            } else {
                vec3 d = rel(peds[a].pos, p.pos);
                vec2 away = length(vec2(d.x, d.y)) > 0.05f ? -normalize(vec2(d.x, d.y)) : vec2(sinf(p.yaw), -cosf(p.yaw));
                vec2 side(-away.y, away.x);
                if (hash32(p.uid + peds[a].meleeSerial) & 1) side = -side;
                meleeDodge(pid, normalize(side * 0.8f + away * 0.6f));
            }
        }
    }
    if (!p.isPlayer && p.blocking && time > p.blockUntil) meleeBlock(pid, false);
    // dodge: fast step with an ease-out, briefly untouchable (see meleeContact)
    if (p.dodgeT >= 0.f) {
        p.dodgeT += dt;
        float k = 1.f - Saturate(p.dodgeT / 0.42f);
        p.forcedVel = p.dodgeDir * (5.8f * k * k + 0.3f);
        p.forcedT = 0.05f;
        if (p.dodgeT > 0.45f) p.dodgeT = -1.f;
    }
    // NPC fighting guard while squared up with an opponent
    if (!p.isPlayer) {
        bool squared = false;
        if (p.meleeTarget >= 0 && p.meleeTarget < (int)peds.size() && isMeleeWeapon(p.weapon)) {
            const Ped& o = peds[p.meleeTarget];
            squared = o.used && o.health > 0.f && o.state == PS_ONFOOT && length(rel(o.pos, p.pos)) < 4.f &&
                      (p.brain.type == BRAIN_COMBAT || p.faction == FAC_POLICE || p.faction == FAC_ENEMY);
        }
        if (squared && p.animIn.stance == 0) p.animIn.stance = 19;
        else if (!squared && p.animIn.stance == 19) p.animIn.stance = 0;
    }
    if (p.meleeMove < 0) return;
    const MeleeMove& mv = kMoves[p.meleeMove];
    p.meleeT += dt;
    // wind-up: turn toward the opponent and close the distance
    int t = p.meleeTarget;
    if (!p.meleeHitDone && t >= 0 && t < (int)peds.size() && peds[t].used && peds[t].state == PS_ONFOOT) {
        vec3 d = rel(peds[t].pos, p.pos);
        vec2 dh(d.x, d.y);
        float dist = length(dh);
        if (dist > 0.05f) {
            float ty = atan2f(-dh.x, dh.y);
            p.yaw = meleeWrap(p.yaw + meleeWrap(ty - p.yaw) * Saturate(dt * 16.f));
            float want = mv.reach * 0.72f;
            float left = mv.contact - p.meleeT;
            if (dist > want && dist < want + mv.lunge + 0.6f && left > 0.02f) {
                p.forcedVel = dh / dist * Min((dist - want) / Max(left, 0.06f), 5.5f);
                p.forcedT = 0.05f;
            }
        }
    }
    if (!p.meleeHitDone && p.meleeT >= mv.contact) {
        p.meleeHitDone = true;
        meleeContact(pid);
        if (!peds[pid].used || p.meleeMove < 0) return;   // interrupted by a perfect block
    }
    if (p.meleeQueued && p.meleeHitDone && p.meleeT >= mv.chainAt) {
        int q = p.meleeQueued;
        p.meleeMove = -1;
        p.meleeQueued = 0;
        p.meleeLastEnd = time;
        meleeStart(pid, q == 2);
    } else if (p.meleeT >= mv.duration) {
        p.meleeMove = -1;
        p.meleeLastEnd = time;
    }
}

void GameWorld::meleeContact(int pid) {
    Ped& p = peds[pid];
    const MeleeMove& mv = kMoves[p.meleeMove];
    vec2 fwd(-sinf(p.yaw), cosf(p.yaw));
    vec3 chest = pedChestPos(p);
    std::vector<int> list;
    pedsNear(chest.xy(), mv.reach + 1.f, list);
    int best = -1;
    float bestScore = -1e9f;
    for (int o : list) {
        if (o == pid) continue;
        const Ped& t = peds[o];
        if (!t.used || t.health <= 0.f || t.state != PS_ONFOOT || t.ragdoll) continue;
        if (t.dodgeT > 0.04f && t.dodgeT < 0.36f) continue;   // mid-dodge: the blow whiffs
        vec3 d = pedChestPos(t) - chest;
        if (fabsf(d.z) > 1.f) continue;
        vec2 dh(d.x, d.y);
        float dist = length(dh);
        if (dist > mv.reach + kPedRadius) continue;
        float c = dist > 0.05f ? dot(dh / dist, fwd) : 1.f;
        if (c < mv.arcCos) continue;
        float score = c - dist * 0.3f + (o == p.meleeTarget ? 1.f : 0.f);
        if (score > bestScore) {
            bestScore = score;
            best = o;
        }
    }
    if (best >= 0) {
        meleeHit(pid, best, p.meleeMove);
        return;
    }
#ifdef HAVE_AUDIO
    Audio::play(Audio::SFX_WHOOSH, chest + vec3(fwd.x, fwd.y, 0.f) * 0.6f, mv.heavy ? 0.7f : 0.45f, mv.heavy ? 0.8f : 1.1f);
#endif
}

void GameWorld::meleeHit(int ai, int ti, int move) {
    Ped& a = peds[ai];
    Ped& t = peds[ti];
    const MeleeMove& mv = kMoves[move];
    const WeaponInfo& wi = weaponInfo(a.weapon);
    vec3 d = rel(t.pos, a.pos);
    d.z = 0.f;
    float dl = length(d);
    vec3 dir = dl > 1e-3f ? d / dl : vec3(-sinf(a.yaw), cosf(a.yaw), 0.f);
    vec3 tf(-sinf(t.yaw), cosf(t.yaw), 0.f);
    bool facing = dot(tf, -dir) > 0.25f;
    bool bat = a.weapon == WPN_BAT, knife = a.weapon == WPN_KNIFE;
    bool face = move == MM_JAB_L || move == MM_JAB_R || move == MM_COUNTER;
    vec3 hitPos = pedChestPos(t) - dir * 0.15f + vec3(0.f, 0.f, face ? 0.35f : (mv.kick ? -0.25f : 0.f));
    bool playerInvolved = a.isPlayer || t.isPlayer;
    // ---- guard
    if (t.blocking && facing && t.meleeStagger <= 0.f) {
        bool perfect = time - t.blockStart < 0.3;
        if (mv.heavy && !perfect) {
            // guard broken
            t.blocking = false;
            t.meleeStagger = 0.7f;
            t.pendingAction = Anim::CLIP_STAGGER;
            t.forcedVel = vec2(dir.x, dir.y) * 2.2f;
            t.forcedT = 0.2f;
            damagePed(ti, wi.damage * mv.damageMul * 0.3f, DMG_MELEE, ai, dir);
#ifdef HAVE_AUDIO
            Audio::play(bat ? Audio::SFX_IMPACT_WOOD : Audio::SFX_PUNCH, hitPos, 0.9f, 0.8f);
#endif
            if (playerInvolved) rig.shake = Max(rig.shake, 0.3f);
            return;
        }
        if (perfect) {
            // perfect block: the attacker reels, the defender may counter
            a.meleeMove = -1;
            a.meleeQueued = 0;
            a.meleeLastEnd = time;
            a.meleeStagger = 0.85f;
            a.pendingAction = Anim::CLIP_STAGGER;
            a.forcedVel = -vec2(dir.x, dir.y) * 2.4f;
            a.forcedT = 0.22f;
            t.counterUntil = time + 0.9;
            if (!t.isPlayer) t.meleeReact = 0;
        } else {
            damagePed(ti, wi.damage * mv.damageMul * 0.12f, DMG_MELEE, ai, dir);
            a.meleeStagger = 0.25f;
            t.forcedVel = vec2(dir.x, dir.y) * 1.2f;
            t.forcedT = 0.12f;
        }
#ifdef HAVE_AUDIO
        Audio::play(bat ? Audio::SFX_IMPACT_WOOD : Audio::SFX_BODY_FALL, hitPos, perfect ? 0.8f : 0.55f, 1.3f);
#endif
        if (playerInvolved) rig.shake = Max(rig.shake, perfect ? 0.25f : 0.12f);
        return;
    }
    // ---- clean hit
    float dmg = wi.damage * mv.damageMul * (0.9f + 0.2f * hashToFloat(hash32(a.meleeSerial * 131u + (u32)ti)));
    if (!facing) dmg *= 1.3f;   // from behind or the side
    if (knife) {
        addWound(ti, dvec3(hitPos), Anim::B_CHEST, 0.05f);
        spawnFx(FX_BLOOD, dvec3(hitPos), dir, 5, 0.9f);
    } else if (bat) {
        spawnFx(FX_BLOOD, dvec3(hitPos), dir, 2, 0.6f);
    }
#ifdef HAVE_AUDIO
    Audio::play(knife ? Audio::SFX_IMPACT_FLESH : (mv.kick ? Audio::SFX_KICK : (bat ? Audio::SFX_IMPACT_WOOD : Audio::SFX_PUNCH)), hitPos,
                mv.heavy ? 1.f : 0.85f, 0.92f + 0.16f * hashToFloat(hash32(a.meleeSerial + 5u)));
#endif
    if (a.isPlayer) {
        pinfo.hitMarker = 1.f;
        rig.shake = Max(rig.shake, mv.heavy ? 0.22f : 0.1f);
    }
    if (t.isPlayer) rig.shake = Max(rig.shake, mv.heavy ? 0.4f : 0.25f);
    damagePed(ti, dmg, DMG_MELEE, ai, dir);
    if (!peds[ti].used || peds[ti].health <= 0.f || peds[ti].ragdoll) return;   // killed / already down
    // knock-down / knock-out: likelier when hurt, from a bat or a heavy blow
    float frac = t.health / Max(t.maxHealth, 1.f);
    float knock = mv.knockChance + (frac < 0.35f ? 0.35f : 0.f) + (bat ? 0.15f : 0.f);
    if (t.isPlayer) knock *= 0.45f;
    if (hashToFloat(hash32(a.meleeSerial * 977u + (u32)ti * 13u)) < knock) {
        knockDown(ti, dir * ((bat ? 380.f : 260.f) * (mv.heavy ? 1.3f : 1.f)) + vec3(0.f, 0.f, 70.f));
        return;
    }
    t.pendingAction = facing ? Anim::CLIP_HIT_FRONT : Anim::CLIP_HIT_BACK;
    t.meleeStagger = Max(t.meleeStagger, mv.stagger);
    if (t.meleeMove >= 0) {   // interrupted
        t.meleeMove = -1;
        t.meleeQueued = 0;
        t.meleeLastEnd = time;
    }
    t.blocking = false;
    t.forcedVel = vec2(dir.x, dir.y) * (mv.heavy ? 2.6f : 1.6f);
    t.forcedT = 0.16f;
    t.hitReactTimer = 0.5f;
}

}  // namespace Game
