    // ---- --autoplay locotour (locomotion test build only): tour stop 2 (downtown at 14:00) for a minute. Every
    // pedestrian within 25 m of the camera has its feet measured each frame: the slide of a planted foot per step as the
    // locomotion harness counts it (heel, ball and toe tip within 8 mm of the ground under them; per frame the smallest
    // move of a point on the ground), the pivot per step, scuffs (contacts under 2 frames); close passes between people
    // (one walking, closing at 0.6 m/s or more) for the arms' clearance; bursts of shots from a fixed camera beside
    // someone walking (feet, whole body) and beside two people about to pass each other.
    struct LocoFoot {
        bool in = false, skip = false;
        vec3 prev[3];
        float prevYaw = 0.f, slide = 0.f, pivot = 0.f, spd = 0.f, turn = 0.f;
        int n = 0, planted = 0;
    };
    struct LocoPed {
        u32 uid = 0;
        bool seen = false, passOn = false;
        float yawPrev = 0.f;
        LocoFoot f[2];
    };
    struct LocoBucket {
        std::vector<float> slide, pivot;
        int scuffs = 0;
    };
    struct LocoPair {
        u32 a = 0, b = 0;
        float gap = 1e9f, center = 1e9f;
        float rv = 0.f, passW = 0.f;   // at the smallest gap: the speed between them, the larger passing weight of the two
        bool group = false;            // one's leader is the other, or they share one (pedai.cpp groups)
    };
    std::vector<LocoPed> locoPeds;
    std::vector<LocoPair> locoPairs;
    LocoBucket locoB[4];
    int locoFootFrames = 0, locoFloatFrames = 0, locoSamples = 0, locoBig = 0;
    float locoLowest = 1e9f, locoProfPeds = 0.f;
    int locoPassEvents = 0, locoPassFrames = 0;
    int locoTogether = 0, locoTogetherLean = 0, locoTogetherLog = 0;   // walking together: pair-frames, ped-frames with the passing lean on
    float locoTogetherTwist = 0.f, locoTogetherShift = 0.f;
    int locoBigStance[32] = {}, locoBigAct[64] = {};
    int locoBurst = -1, locoBursts = 0, locoShot = 0, locoKind = 0, locoRenderEvery = 1;
    float locoBurstT = 0.f, locoShotAt = 0.f, locoNextBurst = 14.f;
    int locoSubj = -1, locoSubj2 = -1;

    bool locoEligible(const Ped& p) const {
        return p.used && !p.isPlayer && p.state == PS_ONFOOT && !p.ragdoll && p.health > 0.f && p.grounded && p.vehicle < 0 &&
               p.doorVehicle < 0 && p.moveMode == 0 && p.charIndex >= 0 && p.charIndex < (int)game.chars.size();
    }

    // heel, ball and toe tip of a foot in the world (the harness's and anim_test's foot points)
    void locoFootPoints(const Ped& p, int s, vec3* out) const {
        const Anim::Skeleton& sk = game.chars[p.charIndex].skel;
        const mat4* m = p.bones;
        int fb = s ? Anim::B_FOOT_R : Anim::B_FOOT_L, tb = s ? Anim::B_TOE_R : Anim::B_TOE_L;
        float ball = sk.bindLocalPos[tb].y, heel = ball * (0.21f / 0.52f), toe = ball * (0.79f / 0.52f);
        float ankH = sk.bindLocalPos[Anim::B_ROOT].z + sk.bindLocalPos[Anim::B_PELVIS].z + sk.bindLocalPos[Anim::B_THIGH_L].z +
                     sk.bindLocalPos[Anim::B_CALF_L].z + sk.bindLocalPos[Anim::B_FOOT_L].z;
        float toeZ = ankH + sk.bindLocalPos[Anim::B_TOE_L].z;
        vec3 hm = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
        vec3 bm = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
        vec3 tm = m[tb].c[3].xyz() + transformDir(m[tb], vec3(0.f, toe - ball, -toeZ));
        quat q = quatAxisAngle(vec3(0, 0, 1), p.yaw);
        vec3 base = p.pos.toVec3();
        out[0] = base + rotate(q, hm);
        out[1] = base + rotate(q, bm);
        out[2] = base + rotate(q, tm);
    }

    static float locoPct(std::vector<float> v, float q) {
        if (v.empty()) return 0.f;
        std::sort(v.begin(), v.end());
        return v[Min((size_t)(v.size() * q), v.size() - 1)];
    }

    void locoClose(int i, const LocoPed& L, const LocoFoot& F, int s, float dt) {
        float spd = F.spd / Max(F.n, 1), turn = F.turn / Max(F.n * dt, 1e-3f);
        int b = spd < 0.25f ? 0 : (spd >= 2.2f ? 3 : (turn >= 0.8f ? 2 : 1));
        if (F.n - 1 < 2) {   // (2 frames at 30 Hz: the harness's 4 at 60 Hz)
            locoB[b].scuffs++;
            return;
        }
        locoB[b].slide.push_back(F.slide * 100.f);
        locoB[b].pivot.push_back(F.pivot * 57.2958f);
        if (F.slide > 0.2f) {
            // what the ped is doing: its stance and action, the walk's weight, the AI's activity
            const Ped& p = game.peds[i];
            PedAI& pa = game.pedAI(i);
            locoBigStance[Clamp(p.animIn.stance, 0, 31)]++;
            locoBigAct[Min((int)pa.activity, 63)]++;
            if (locoBig < 150) {
                locoBig++;
                vec2 v(p.vel.x, p.vel.y), fw(-sinf(p.yaw), cosf(p.yaw)), rt(cosf(p.yaw), sinf(p.yaw));
                LOG("locotour big slide: ped %u foot %d %.1f cm over %d frames (planted %d), pivot %.0f deg, speed %.2f m/s (fwd %.2f side %.2f), "
                    "turning %.2f rad/s | stance %d action %d t%.2f moveW %.2f state %d brain %d activity %d walkStance %d leader %d moveMode %d",
                    L.uid, s, F.slide * 100.f, F.n, F.planted, F.pivot * 57.2958f, spd, dot(v, fw), dot(v, rt), turn, p.animIn.stance, p.anim.action,
                    p.anim.actionTime, p.anim.moveW, (int)p.state, (int)p.brain.type, (int)pa.activity, (int)pa.walkStance, pa.leader, p.moveMode);
            }
        }
    }

    void locoMeasure(float dt) {
        const float kDown = 0.008f;
        if (locoPeds.size() < game.peds.size()) locoPeds.resize(game.peds.size());
        locoSamples++;
        locoProfPeds += game.profPeds;
        std::vector<int> act;
        for (int i = 0; i < (int)game.peds.size(); i++) {
            const Ped& p = game.peds[i];
            LocoPed& L = locoPeds[i];
            if (!locoEligible(p) || p.visibleDist >= 25.f) {
                L = LocoPed();   // (out of view or busy: open contacts are dropped, not counted)
                continue;
            }
            if (L.uid != p.uid) {
                L = LocoPed();
                L.uid = p.uid;
            }
            act.push_back(i);
            const bool first = !L.seen;
            float spd = length(vec2(p.vel.x, p.vel.y)), dyaw = first ? 0.f : wrapAngle(p.yaw - L.yawPrev);
            L.yawPrev = p.yaw;
            L.seen = true;
            for (int s = 0; s < 2; s++) {
                vec3 P[3];
                locoFootPoints(p, s, P);
                bool down[3];
                float low = 1e9f;
                for (int k = 0; k < 3; k++) {
                    float gz = game.groundHeight(P[k].x, P[k].y, P[k].z - 0.25f);
                    float h = gz > -1e8f ? P[k].z - gz : 1.f;
                    down[k] = h < kDown;
                    low = Min(low, h);
                }
                locoFootFrames++;
                locoLowest = Min(locoLowest, low);
                if (p.anim.planted[s] && low > 0.015f) locoFloatFrames++;
                vec3 dd = P[1] - P[0];
                float fyaw = atan2f(-dd.x, dd.y);
                LocoFoot& F = L.f[s];
                bool contact = down[0] || down[1] || down[2];
                if (first) F.skip = contact;   // (a contact already under way when the ped came into view: not counted)
                if (F.skip) {
                    if (!contact) F.skip = false;
                } else if (contact) {
                    if (F.in) {
                        float best = 1e9f;
                        for (int k = 0; k < 3; k++)
                            if (down[k]) best = Min(best, length(vec2(P[k].x - F.prev[k].x, P[k].y - F.prev[k].y)));
                        F.slide += best;
                        F.pivot += fabsf(wrapAngle(fyaw - F.prevYaw));
                    } else {
                        F = LocoFoot();
                        F.in = true;
                    }
                    F.spd += spd;
                    F.turn += fabsf(dyaw);
                    F.n++;
                    F.planted += p.anim.planted[s] ? 1 : 0;
                } else if (F.in) {
                    locoClose(i, L, F, s, dt);
                    F.in = false;
                }
                for (int k = 0; k < 3; k++) F.prev[k] = P[k];
                F.prevYaw = fyaw;
            }
#ifdef LOCO_AFTER
            // the passing body language (shoulder turn, lean, side-step) at work
            if (p.anim.passW > 0.3f) {
                locoPassFrames++;
                if (!L.passOn) locoPassEvents++;
                L.passOn = true;
            } else if (p.anim.passW < 0.15f) {
                L.passOn = false;
            }
#endif
        }
        // close passes: the arms' clearance (shoulders, elbows, wrists as spheres), the smallest over each pair's pass
        static const int kB[6] = {Anim::B_UPPERARM_L, Anim::B_UPPERARM_R, Anim::B_FOREARM_L, Anim::B_FOREARM_R, Anim::B_HAND_L, Anim::B_HAND_R};
        static const float kR[6] = {0.07f, 0.07f, 0.05f, 0.05f, 0.04f, 0.04f};
        for (size_t a = 0; a < act.size(); a++)
            for (size_t b = a + 1; b < act.size(); b++) {
                const Ped& A = game.peds[act[a]];
                const Ped& B = game.peds[act[b]];
                vec3 pa = A.pos.toVec3(), pb = B.pos.toVec3();
                float cd = length(vec2(pb.x - pa.x, pb.y - pa.y));
                if (cd > 1.2f || fabsf(pb.z - pa.z) > 0.5f) continue;
                vec2 va(A.vel.x, A.vel.y), vb(B.vel.x, B.vel.y);
#ifdef LOCO_AFTER
                // walking together (both walking, under 0.3 m/s between them, within 1.2 m): no passing lean wanted
                if (Min(length(va), length(vb)) > 0.5f && length(vb - va) < 0.3f) {
                    locoTogether++;
                    for (int w = 0; w < 2; w++) {
                        const Ped* q = w ? &B : &A;
                        const Ped* o = w ? &A : &B;
                        if (q->anim.passW > 0.3f) {
                            locoTogetherLean++;
                            if (locoTogetherLog < 16) {
                                // whom it leans from: the passer the game gave it (AnimInput::passBy, its chest) against the companion
                                locoTogetherLog++;
                                vec3 pb = q->pos.toVec3() + rotate(quatAxisAngle(vec3(0, 0, 1), q->yaw), q->animIn.passBy);
                                float fromComp = length(vec2(pb.x - (float)o->pos.x, pb.y - (float)o->pos.y));
                                LOG("locotour together lean: ped %u (passW %.2f, twist %.1f deg) with %u, %.2f m apart, %.2f m/s between, group %d | its passer's "
                                    "chest %.2f m from the companion",
                                    q->uid, q->anim.passW, q->anim.passYaw * 57.3f, o->uid, cd, length(vb - va), (int)locoGroup(w ? act[b] : act[a], w ? act[a] : act[b]),
                                    fromComp);
                            }
                        }
                        locoTogetherTwist = Max(locoTogetherTwist, fabsf(q->anim.passYaw) * 57.3f);
                        locoTogetherShift = Max(locoTogetherShift, fabsf(q->anim.passShift) * 100.f);
                    }
                }
#endif
                if (Max(length(va), length(vb)) < 0.5f || length(vb - va) < 0.6f) continue;
                quat qa = quatAxisAngle(vec3(0, 0, 1), A.yaw), qb = quatAxisAngle(vec3(0, 0, 1), B.yaw);
                float gap = 1e9f;
                for (int i = 0; i < 6; i++) {
                    vec3 wa = pa + rotate(qa, A.bones[kB[i]].c[3].xyz());
                    for (int j = 0; j < 6; j++) gap = Min(gap, length(wa - (pb + rotate(qb, B.bones[kB[j]].c[3].xyz()))) - kR[i] - kR[j]);
                }
                u32 ua = Min(A.uid, B.uid), ub = Max(A.uid, B.uid);
                LocoPair* pr = nullptr;
                for (LocoPair& q : locoPairs)
                    if (q.a == ua && q.b == ub) pr = &q;
                if (!pr) {
                    locoPairs.push_back(LocoPair());
                    pr = &locoPairs.back();
                    pr->a = ua;
                    pr->b = ub;
                }
                if (gap < pr->gap) {
                    pr->rv = length(vb - va);
#ifdef LOCO_AFTER
                    pr->passW = Max(A.anim.passW, B.anim.passW);
#endif
                    pr->group = locoGroup(act[a], act[b]);
                }
                pr->gap = Min(pr->gap, gap);
                pr->center = Min(pr->center, cd);
            }
    }

    // two peds of one walking group (pedai.cpp): one's leader is the other, or they share a leader
    bool locoGroup(int i, int j) {
        if (i < 0 || j < 0 || i >= (int)game.peds.size() || j >= (int)game.peds.size()) return false;
        const PedAI& qi = game.pedAI(i);
        const PedAI& qj = game.pedAI(j);
        const Ped &pi = game.peds[i], &pj = game.peds[j];
        return (qi.leader == j && qi.leaderUid == pj.uid) || (qj.leader == i && qj.leaderUid == pi.uid) ||
               (qi.leader >= 0 && qi.leader == qj.leader && qi.leaderUid == qj.leaderUid);
    }

    // a camera spot dist metres beside the point m (across the direction dir), with a clear view of m
    bool locoCamSpot(vec3 m, vec2 dir, float dist, float h, int ignorePed, vec3& cam) const {
        vec2 n(dir.y, -dir.x);
        for (int k = 0; k < 2; k++) {
            float sd = k ? -1.f : 1.f;
            vec3 c(m.x + n.x * dist * sd, m.y + n.y * dist * sd, m.z + h);
            if (game.lineOfSight(dvec3(c.x, c.y, c.z), dvec3(m.x, m.y, m.z + 0.5f), ignorePed, -1) &&
                game.lineOfSight(dvec3(c.x, c.y, c.z), dvec3(m.x, m.y, m.z + 1.5f), ignorePed, -1)) {
                cam = c;
                return true;
            }
        }
        return false;
    }

    void locoShots(float dt) {
        static const char* kKind[3] = {"feet", "body", "pass"};
        Ped* pl = game.playerPed();
        if (locoBurst < 0) {
            if (tourT < locoNextBurst || locoBursts >= 9 || !pl) return;
            int kind = locoBursts % 3;   // feet, whole body, two people passing
            if (kind == 2 && tourT > locoNextBurst + 5.f) kind = 0;   // (nobody about to pass: feet instead)
            vec3 plp = pl->pos.toVec3();
            vec3 cam(0.f), tgt(0.f);
            float fov = 30.f, shotAt = 0.5f;
            int s1 = -1, s2 = -1;
            float bestD = 1e9f;
            for (int i = 0; i < (int)game.peds.size(); i++) {
                const Ped& p = game.peds[i];
                if (!locoEligible(p)) continue;
                vec3 pp = p.pos.toVec3();
                vec2 v(p.vel.x, p.vel.y);
                float s = length(v), d = length(vec2(pp.x - plp.x, pp.y - plp.y));
                if (d < 2.f || d > 22.f || d >= bestD) continue;
                if (kind < 2) {
                    // someone walking straight ahead: the camera beside where they will be half way through the burst
                    if (s < 0.9f || s > 2.f || dot(v / s, vec2(-sinf(p.yaw), cosf(p.yaw))) < 0.95f) continue;
                    vec2 mid = vec2(pp.x, pp.y) + v * 1.05f;
                    vec3 m(mid.x, mid.y, game.groundHeight(mid.x, mid.y, pp.z + 0.5f));
                    if (m.z < -1e8f || fabsf(m.z - pp.z) > 0.3f) continue;
                    vec3 c;
                    if (!locoCamSpot(m, v / s, kind == 0 ? 2.6f : 4.2f, kind == 0 ? 0.3f : 1.f, i, c)) continue;
                    cam = c;
                    tgt = m + vec3(0.f, 0.f, kind == 0 ? 0.25f : 0.9f);
                    fov = kind == 0 ? 30.f : 36.f;
                    shotAt = 0.5f;
                    s1 = i;
                    bestD = d;
                } else {
                    // two people closing on each other, to pass within 0.9 m in 1.3-3 s
                    if (s < 0.6f) continue;
                    for (int j = 0; j < (int)game.peds.size(); j++) {
                        const Ped& o = game.peds[j];
                        if (j == i || !locoEligible(o)) continue;
                        vec3 op = o.pos.toVec3();
                        vec2 ov(o.vel.x, o.vel.y);
                        if (length(ov) < 0.6f || fabsf(op.z - pp.z) > 0.4f) continue;
                        vec2 dd(op.x - pp.x, op.y - pp.y), rv = ov - v;
                        float dist = length(dd), rv2 = length2(rv);
                        if (dist < 2.f || dist > 7.f || rv2 < 1.f) continue;
                        float tca = -dot(dd, rv) / rv2;
                        if (tca < 1.3f || tca > 3.f || length(dd + rv * tca) > 0.9f) continue;
                        vec2 mid = (vec2(pp.x, pp.y) + v * tca + vec2(op.x, op.y) + ov * tca) * 0.5f;
                        vec3 m(mid.x, mid.y, game.groundHeight(mid.x, mid.y, pp.z + 0.5f));
                        if (m.z < -1e8f || fabsf(m.z - pp.z) > 0.3f) continue;
                        vec3 c;
                        if (!locoCamSpot(m, rv / sqrtf(rv2), 5.f, 1.3f, i, c)) continue;
                        cam = c;
                        tgt = m + vec3(0.f, 0.f, 0.9f);
                        fov = 40.f;
                        shotAt = tca - 0.8f;
                        s1 = i;
                        s2 = j;
                        bestD = d;
                        break;
                    }
                }
            }
            if (s1 < 0) return;
            locoBurst = locoBursts;
            locoKind = kind;
            locoBurstT = 0.f;
            locoShot = 0;
            locoShotAt = shotAt;
            locoSubj = s1;
            locoSubj2 = s2;
            game.rig.scriptActive = true;
            game.rig.scriptPos = dvec3(cam.x, cam.y, cam.z);
            game.rig.scriptTarget = dvec3(tgt.x, tgt.y, tgt.z);
            game.rig.scriptFov = fov;
            game.rig.cut = true;
            renderer.cameraCut = true;
            renderEvery = 1;
            LOG("locotour burst %d (%s) at t=%.1f: ped %u%s, camera %.1f %.1f %.1f looking at %.1f %.1f %.1f", locoBurst, kKind[kind], tourT,
                game.peds[s1].uid, s2 >= 0 ? StrFormat(" and ped %u", game.peds[s2].uid).c_str() : "", cam.x, cam.y, cam.z, tgt.x, tgt.y, tgt.z);
            return;
        }
        locoBurstT += dt;
        const int nShots = locoKind == 2 ? 16 : 12;
        if (locoShot < nShots && locoBurstT >= locoShotAt + locoShot * 0.1f - 1e-3f && game.requestScreenshot.empty()) {
            game.requestScreenshot = shotPath(StrFormat("loco_b%02d_%s_%02d", locoBurst, kKind[locoKind], locoShot));
            const Ped& p = game.peds[locoSubj];
            float pw = 0.f, pw2 = 0.f, cd = 0.f;
#ifdef LOCO_AFTER
            pw = p.anim.passW;
            if (locoSubj2 >= 0) pw2 = game.peds[locoSubj2].anim.passW;
#endif
            if (locoSubj2 >= 0) {
                vec3 a = p.pos.toVec3(), b = game.peds[locoSubj2].pos.toVec3();
                cd = length(vec2(b.x - a.x, b.y - a.y));
            }
            LOG("locotour shot %d.%02d: speed %.2f planted %d%d phase %.2f | apart %.2f m, passing weight %.2f / %.2f", locoBurst, locoShot,
                length(vec2(p.vel.x, p.vel.y)), (int)p.anim.planted[0], (int)p.anim.planted[1], p.anim.phase, cd, pw, pw2);
            locoShot++;
        }
        if (locoShot >= nShots && game.requestScreenshot.empty()) {
            locoBurst = -1;
            locoBursts++;
            locoNextBurst = tourT + 5.f;
            game.rig.scriptActive = false;
            game.rig.cut = true;
            renderEvery = locoRenderEvery;
        }
    }

    void locoReport() {
        static const char* kName[4] = {"standing", "walking straight", "walking, turning", "jogging/running"};
        LocoBucket all;
        for (int b = 0; b < 4; b++) {
            const LocoBucket& B = locoB[b];
            double s = 0.0, pv = 0.0;
            for (float x : B.slide) s += x;
            for (float x : B.pivot) pv += x;
            int n = (int)B.slide.size();
            LOG("locotour steps %-17s n %4d | slide cm/step mean %.2f p90 %.2f max %.2f | pivot deg/step mean %.1f | scuffs %d", kName[b], n,
                n ? s / n : 0.0, locoPct(B.slide, 0.9f), locoPct(B.slide, 1.f), n ? pv / n : 0.0, B.scuffs);
            all.slide.insert(all.slide.end(), B.slide.begin(), B.slide.end());
            all.pivot.insert(all.pivot.end(), B.pivot.begin(), B.pivot.end());
            all.scuffs += B.scuffs;
        }
        double s = 0.0;
        for (float x : all.slide) s += x;
        int n = (int)all.slide.size();
        LOG("locotour steps %-17s n %4d | slide cm/step mean %.2f p90 %.2f max %.2f | scuffs %d", "all", n, n ? s / n : 0.0, locoPct(all.slide, 0.9f),
            locoPct(all.slide, 1.f), all.scuffs);
        LOG("locotour feet: %d foot-frames, planted but more than 15 mm off the ground %d, lowest sole point %.1f mm", locoFootFrames,
            locoFloatFrames, locoLowest * 1000.f);
        int np = 0, neg = 0;
        double gs = 0.0;
        float gmin = 1e9f;
        for (const LocoPair& q : locoPairs) {
            if (q.center > 0.9f) continue;   // (passes within 0.9 m, centre to centre)
            np++;
            gs += q.gap;
            gmin = Min(gmin, q.gap);
            neg += q.gap < 0.f;
        }
        LOG("locotour close passes (within 0.9 m): %d | arms' clearance cm: smallest %.1f, mean of each pass's smallest %.1f | arms through each other %d",
            np, np ? gmin * 100.f : 0.f, np ? gs / np * 100.0 : 0.0, neg);
        {
            // the same split by walking group (a companion catching up on its slot is no passer), and each pass
            int ng[2] = {0, 0}, negg[2] = {0, 0};
            double gsg[2] = {0.0, 0.0};
            float gming[2] = {1e9f, 1e9f};
            std::vector<LocoPair> ps;
            for (const LocoPair& q : locoPairs)
                if (q.center <= 0.9f) ps.push_back(q);
            std::sort(ps.begin(), ps.end(), [](const LocoPair& x, const LocoPair& y) { return x.a != y.a ? x.a < y.a : x.b < y.b; });
            for (const LocoPair& q : ps) {
                int g = q.group ? 1 : 0;
                ng[g]++;
                gsg[g] += q.gap;
                gming[g] = Min(gming[g], q.gap);
                negg[g] += q.gap < 0.f;
                LOG("locotour pass %u-%u: arms' clearance %.1f cm, centres %.2f m, %.2f m/s between, passing weight %.2f, %s", q.a, q.b, q.gap * 100.f,
                    q.center, q.rv, q.passW, q.group ? "one walking group" : "strangers");
            }
            for (int g = 0; g < 2; g++)
                LOG("locotour close passes, %s: %d | arms' clearance cm: smallest %.1f, mean %.1f | arms through each other %d", g ? "within a walking group" : "between strangers",
                    ng[g], ng[g] ? gming[g] * 100.f : 0.f, ng[g] ? gsg[g] / ng[g] * 100.0 : 0.0, negg[g]);
        }
        LOG("locotour passing body language: %d events, %d ped-frames", locoPassEvents, locoPassFrames);
        LOG("locotour walking together (within 1.2 m, under 0.3 m/s apart): %d pair-frames | ped-frames with the passing lean %d | twist max %.1f deg | side-step max %.1f cm",
            locoTogether, locoTogetherLean, locoTogetherTwist, locoTogetherShift);
        std::string bs, ba;
        for (int k = 0; k < 32; k++)
            if (locoBigStance[k]) bs += StrFormat(" %d:%d", k, locoBigStance[k]);
        for (int k = 0; k < 64; k++)
            if (locoBigAct[k]) ba += StrFormat(" %d:%d", k, locoBigAct[k]);
        LOG("locotour slides over 20 cm by stance:%s | by AI activity:%s", bs.c_str(), ba.c_str());
        LOG("locotour peds cpu %.2f ms per frame (mean over %d frames)", locoSamples ? locoProfPeds / locoSamples : 0.f, locoSamples);
    }

    void updateLocoTour(Controls& c, float dt) {
        mu::computePlaces(game);
        const mu::Place& pl0 = mu::gPlaces.policeHq;   // tour stop 2, "downtown_afternoon"
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl) return;
        if (tourStop < 0) {
            tourStop = 2;
            tourT = 0.f;
            if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
            vec3 pos = pl0.pos;
            pl->pos = dvec3(pos.x, pos.y, game.groundHeight(pos.x, pos.y, pos.z + 2.f));
            pl->vel = vec3(0.f);
            pl->yaw = atan2f(-pl0.streetDir.x, pl0.streetDir.y);
            pl->invincible = true;
            vec2 sd = pl0.streetDir, left(-sd.y, sd.x);
            float side = dot(left, pl0.outward) >= 0.f ? 1.f : -1.f;
            game.rig.yaw = pl->yaw + 0.5f * side;
            game.rig.pitch = -0.1f;
            game.rig.cut = true;
            game.hudVisible = false;
            env.timeOfDay = 14.f;
            weather.setImmediate(WX_FAIR);
            game.populationWarmup = 2.5f;
            game.pinfo.wanted = 0;
            locoRenderEvery = renderEvery;
            LOG("locotour: stop 2 downtown_afternoon at %.0f %.0f", pos.x, pos.y);
        }
        tourT += dt;
        env.timeOfDay = 14.f;   // (held: comparable runs)
        game.pinfo.wanted = 0;
        c = Controls();
        if (tourT > 8.f) locoMeasure(dt);
        locoShots(dt);
        if (tourT > 72.f && locoBurst < 0) {
            locoReport();
            tourDone = true;
            game.rig.scriptActive = false;
            renderEvery = locoRenderEvery;
        }
    }

