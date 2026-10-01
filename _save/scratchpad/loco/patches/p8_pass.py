import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''        rotateLocal(outp, B_NECK, qy(-leanS * 0.5f));
    }

    // ---------------------------------------------------------------- start / stop lean, head leading into turns,''', '''        rotateLocal(outp, B_NECK, qy(-leanS * 0.5f));
    }

    // ---------------------------------------------------------------- someone passing close (AnimInput::passBy): the
    //                                                                  near shoulder turns back, the trunk leans away
    //                                                                  and the steps go a little aside
    {
        float wT = 0.f, yawT = 0.f, shiftT = 0.f;
        const float side = in.passBy.x >= 0.f ? 1.f : -1.f;   // the passer on the right (+) or the left (-)
        if (upright && !cheap && !in.aiming && in.passWeight > 0.f && crouchBlend < 0.5f && !stanceIsGuard(stance) && stance != 25 &&
            !stanceLocksLegs(stance)) {
            // the closer across they come, the more (two people square on need some 0.9 m between them); most while abreast
            const float tight = sstep(1.05f, 0.5f, fabsf(in.passBy.x)), abreast = 1.f - sstep(0.5f, 1.8f, fabsf(in.passBy.y));
            wT = Saturate(in.passWeight) * tight * (0.3f + 0.7f * abreast);
            yawT = -side * 0.5f * wT;
            shiftT = -side * 0.11f * wT;
        }
        const float kp = 1.f - expf(-dt * 7.f);
        passW += (wT - passW) * kp;
        passYaw += (yawT - passYaw) * kp;
        passShift += (shiftT - passShift) * (1.f - expf(-dt * 4.f));
        if (fabsf(passYaw) > 1e-4f) {
            // the twist spread up the spine (the lower back least), most of it taken back by the neck and head (the eyes
            // stay on the way ahead), the trunk leaning a little away from them
            static const u8 kChain[5] = {B_SPINE1, B_SPINE2, B_CHEST, B_NECK, B_HEAD};
            static const float kShare[5] = {0.25f, 0.35f, 0.4f, -0.35f, -0.35f};
            for (int k = 0; k < 5; k++) {
                quat qp;
                vec3 pp;
                boneModel(sk, outp, sk.parent[kChain[k]], qp, pp);
                outp.rot[kChain[k]] = normalize(conj(qp) * qz(passYaw * kShare[k]) * qp * outp.rot[kChain[k]]);
            }
            rotateLocal(outp, B_SPINE2, qy(-0.12f * passYaw));
        }
        if (fabsf(passShift) > 1e-4f) outp.rootOffset.x += passShift;
    }

    // ---------------------------------------------------------------- start / stop lean, head leading into turns,''')
open(p, 'w').write(s)
print("ok")
