#!/usr/bin/env python3
# Game-side integration of Audio::playFootstep / playBodyImpact / playFoley (item 6). Usage: patch_game_foot.py <src/game dir>
import sys, os

gdir = sys.argv[1]


def patch(path, pairs):
    s = open(path).read()
    for old, new in pairs:
        if s.count(old) != 1:
            raise SystemExit("anchor not found (%d) in %s:\n%s" % (s.count(old), path, old[:200]))
        s = s.replace(old, new)
    open(path, "w").write(s)


peds = os.path.join(gdir, "peds.cpp")
patch(peds, [
    ("""quat yawQuat(float yaw) { return quatAxisAngle(vec3(0, 0, 1), yaw); }

}  // namespace ped_detail""",
     """quat yawQuat(float yaw) { return quatAxisAngle(vec3(0, 0, 1), yaw); }

#ifdef HAVE_AUDIO
static_assert((int)Audio::FOOT_ASPHALT == (int)Phys::SURF_ASPHALT && (int)Audio::FOOT_WATER == (int)Phys::SURF_WATER &&
                  (int)Audio::FOOT_WOOD == (int)Phys::SURF_WOOD && (int)Audio::FOOT_MUD == (int)Phys::SURF_MUD,
              "Audio::FootSurface follows Phys::SurfaceType");
// Footwear of a ped for its footsteps: dress shoes are heels on women, leather soles on men; flats click like leather.
u8 footwearOf(const GameWorld& g, const Ped& p) {
    if (p.charIndex < 0 || p.charIndex >= (int)g.chars.size()) return Audio::FOOTWEAR_SNEAKER;
    const Anim::CharacterDesc& d = g.chars[(size_t)p.charIndex].desc;
    switch (d.shoes) {
        case Anim::detail::SHOE_DRESS: return d.gender == Anim::FEMALE ? Audio::FOOTWEAR_HEEL : Audio::FOOTWEAR_LEATHER;
        case Anim::detail::SHOE_FLATS: return Audio::FOOTWEAR_LEATHER;
        case Anim::detail::SHOE_BOOT: return Audio::FOOTWEAR_BOOT;
        case Anim::detail::SHOE_SANDAL: return Audio::FOOTWEAR_SANDAL;
        case Anim::detail::SHOE_BARE: return Audio::FOOTWEAR_BARE;
        default: return Audio::FOOTWEAR_SNEAKER;
    }
}
// Body weight relative to an average adult (footstep force and pitch).
float bodyWeightOf(const GameWorld& g, const Ped& p) {
    if (p.charIndex < 0 || p.charIndex >= (int)g.chars.size()) return 1.f;
    const Anim::CharacterDesc& d = g.chars[(size_t)p.charIndex].desc;
    float h = d.height / 1.75f;
    return Clamp(h * h * (0.8f + 0.4f * d.weight + 0.15f * d.muscle) * (d.gender == Anim::FEMALE ? 0.85f : 1.f), 0.5f, 1.8f);
}
// The ground under a foot as an Audio::FootSurface (standing water counts as wading).
u8 footSurfaceOf(const Phys::GroundHit& gh) {
    return gh.water ? (u8)Audio::FOOT_WATER : (u8)Min((int)gh.surface, (int)Audio::FOOT_SURFACE_COUNT - 1);
}
#endif

}  // namespace ped_detail"""),
    # landing after a jump or a drop
    ("""                } else if (impact > 4.f) {
                    p.pendingAction = Anim::CLIP_LAND;
                }""",
     """                } else if (impact > 4.f) {
                    p.pendingAction = Anim::CLIP_LAND;
                }
#ifdef HAVE_AUDIO
                if (impact > 2.5f && (p.isPlayer || p.visibleDist < 40.f)) {
                    Audio::Footstep f;
                    f.pos = np;
                    f.event = Audio::FOOT_LAND;
                    f.impact = impact;
                    f.speed = length(vec2(p.vel.x, p.vel.y));
                    f.surface = footSurfaceOf(g);
                    f.footwear = footwearOf(*this, p);
                    f.weight = bodyWeightOf(*this, p);
                    f.wetness = env ? env->wetness : 0.f;
                    f.player = p.isPlayer;
                    Audio::playFootstep(f);
                }
#endif"""),
    # vault / climb: a hand grabbing the obstacle and the clothing burst
    ("""    p.traverseT = 0.f;
    p.yaw = atan2f(-dir.x, dir.y);
    p.vel = vec3(0);
    p.aiming = false;
    return true;
}""",
     """    p.traverseT = 0.f;
    p.yaw = atan2f(-dir.x, dir.y);
    p.vel = vec3(0);
    p.aiming = false;
#ifdef HAVE_AUDIO
    if (p.isPlayer || p.visibleDist < 30.f) {
        Audio::playFoley(vec3(hit.x, hit.y, top), Audio::FOLEY_GRAB, p.moveMode == 3 ? 1.f : 0.8f);
        Audio::playFoley(start + vec3(0.f, 0.f, 1.f), Audio::FOLEY_CLOTH, p.moveMode == 3 ? 1.f : 0.8f);
    }
#endif
    return true;
}"""),
    ("""    if (t >= 1.f) {
        p.moveMode = 0;
        p.pos = dvec3(p.traverseTo);
        p.groundZ = p.traverseTo.z;
        p.airTime = 0.f;
    }
}""",
     """    if (t >= 1.f) {
#ifdef HAVE_AUDIO
        if (p.isPlayer || p.visibleDist < 30.f) {  // feet coming down on the far side / on top
            Audio::Footstep f;
            f.pos = p.traverseTo;
            f.event = Audio::FOOT_LAND;
            f.impact = p.moveMode == 2 ? 3.2f : 2.2f;
            f.surface = footSurfaceOf(Phys::gCollision->ground(p.traverseTo.x, p.traverseTo.y, p.traverseTo.z + 0.2f));
            f.footwear = footwearOf(*this, p);
            f.weight = bodyWeightOf(*this, p);
            f.wetness = env ? env->wetness : 0.f;
            f.player = p.isPlayer;
            Audio::playFootstep(f);
        }
#endif
        p.moveMode = 0;
        p.pos = dvec3(p.traverseTo);
        p.groundZ = p.traverseTo.z;
        p.airTime = 0.f;
    }
}"""),
    # footsteps: layered steps by surface, footwear, gait, weight and wet ground
    ("""    // footsteps (nearby)
    if (p.state == PS_ONFOOT && p.grounded && p.visibleDist < 25.f) {
        float spd = length(vec2(p.vel.x, p.vel.y));
        if (spd > 0.6f) {
            float stride = spd < 2.f ? 0.75f : (spd < 5.f ? 1.2f : 1.7f);
            p.stepPhase += spd * dt / stride;
            if (p.stepPhase >= 1.f) {
                p.stepPhase -= 1.f;
#ifdef HAVE_AUDIO
                Phys::GroundHit g = Phys::gCollision->ground((float)p.pos.x, (float)p.pos.y, (float)p.pos.z + 0.2f);
                Audio::Sfx s = Audio::SFX_STEP_CONCRETE;
                switch (g.surface) {
                    case Phys::SURF_GRASS: s = Audio::SFX_STEP_GRASS; break;
                    case Phys::SURF_DIRT: case Phys::SURF_MUD: s = Audio::SFX_STEP_GRAVEL; break;
                    case Phys::SURF_SAND: s = Audio::SFX_STEP_SAND; break;
                    case Phys::SURF_WOOD: s = Audio::SFX_STEP_WOOD; break;
                    case Phys::SURF_METAL: s = Audio::SFX_STEP_METAL; break;
                    default: break;
                }
                if (g.water) s = Audio::SFX_STEP_WATER;
                Audio::play(s, p.pos.toVec3(), (p.isPlayer ? 0.55f : 0.35f) * Saturate(spd / 3.f + 0.4f));
#endif
            }
        }
    }""",
     """    // footsteps (nearby): each foot contact goes to the mixer with the surface, footwear, gait, body weight and wet
    // ground; it builds the layered step (Audio::playFootstep)
    if (p.state == PS_ONFOOT && p.grounded && (p.isPlayer || p.visibleDist < 30.f)) {
        float spd = length(vec2(p.vel.x, p.vel.y));
        if (spd > 0.6f) {
            float stride = spd < 2.f ? 0.75f : (spd < 5.f ? 1.2f : 1.7f);
            p.stepPhase += spd * dt / stride;
            if (p.stepPhase >= 1.f) {
                p.stepPhase -= 1.f;
#ifdef HAVE_AUDIO
                Phys::GroundHit g = Phys::gCollision->ground((float)p.pos.x, (float)p.pos.y, (float)p.pos.z + 0.2f);
                Audio::Footstep f;
                f.pos = p.pos.toVec3();
                f.speed = spd;
                f.surface = footSurfaceOf(g);
                f.footwear = footwearOf(*this, p);
                f.weight = bodyWeightOf(*this, p);
                f.wetness = env ? env->wetness : 0.f;
                f.player = p.isPlayer;
                Audio::playFootstep(f);
#endif
            }
        }
    }"""),
])

rag = os.path.join(gdir, "ragdoll.cpp")
patch(rag, [
    ("""    float settle = 0.f;
    float time = 0.f;
    bool frozen = false;
    int contactVehicle = -1;
};""",
     """    float settle = 0.f;
    float time = 0.f;
    bool frozen = false;
    int contactVehicle = -1;
    // ground impacts this frame for the body-fall sounds: [0] trunk and head, [1] limbs
    float hitSpeed[2] = {0.f, 0.f};
    vec3 hitPos[2];
    u8 hitSurf[2] = {0, 0};
    float thudCool = 0.f, slapCool = 0.f;
};"""),
    ("""                    if (q.z < gh.z + rad) {
                        float pen = gh.z + rad - q.z;""",
     """                    if (q.z < gh.z + rad) {
                        if (it == 0) {  // touchdown speed for the impact sounds
                            int cls = (i <= RP_HEAD) ? 0 : 1;
                            float vz = (r.prev[i].z - q.z) / h;
                            if (vz > r.hitSpeed[cls]) {
                                r.hitSpeed[cls] = vz;
                                r.hitPos[cls] = q;
                                r.hitSurf[cls] = gh.water ? (u8)Phys::SURF_WATER : gh.surface;
                            }
                        }
                        float pen = gh.z + rad - q.z;"""),
    ("""        if (p.state == PS_DEAD && r.settle > 2.f) r.frozen = true;
    }""",
     """        if (p.state == PS_DEAD && r.settle > 2.f) r.frozen = true;
#ifdef HAVE_AUDIO
        // the body hitting the ground: a thud for the trunk, slaps for arms and legs (rate-limited per body)
        r.thudCool -= dt;
        r.slapCool -= dt;
        if (p.visibleDist < 60.f || p.isPlayer) {
            if (r.hitSpeed[0] > 1.6f && r.thudCool <= 0.f) {
                Audio::playBodyImpact(r.hitPos[0], r.hitSpeed[0], r.hitSurf[0], true);
                r.thudCool = 0.35f;
            }
            if (r.hitSpeed[1] > 2.2f && r.slapCool <= 0.f) {
                Audio::playBodyImpact(r.hitPos[1], r.hitSpeed[1], r.hitSurf[1], false, 0.8f);
                r.slapCool = 0.18f;
            }
        }
        r.hitSpeed[0] = r.hitSpeed[1] = 0.f;
#endif
    }"""),
])
print("patched", peds, rag)
