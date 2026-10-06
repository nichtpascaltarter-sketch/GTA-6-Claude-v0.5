static void faceLandmarks(const BuildCtx& c, FaceLm& L) {
    const BodyDims& D = *c.D;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        L.eye[sd] = (D.J[B_EYE_L + sd] - D.J[B_HEAD]) / D.headS;
        L.ala[sd] = vec3(sx * 0.0118f * D.noseW + 0.4f * D.asymNose, 0.0935f + 0.003f * (D.noseP - 1.f), 0.0108f - 0.004f * (D.noseL - 1.f));
        L.mouthCorner[sd] = vec3(sx * 0.0252f * D.lipW * D.faceW, 0.0845f, -0.0185f + (sd ? D.asymMouth : 0.f));
        L.ear[sd] = vec3(sx * 0.0695f * D.faceW, -0.01f, 0.036f);
    }
    L.eyeR = 0.0119f * D.eyeSize;
    L.nasion = vec3(0.2f * D.asymNose, 0.0872f + 0.0045f * (D.noseBridge - 1.f), 0.0615f);
    L.noseTip = vec3(D.asymNose, 0.1128f + 0.009f * (D.noseP - 1.f), 0.0195f - 0.011f * (D.noseL - 1.f) + 0.003f * D.noseTipUp);
    L.subnasale = vec3(0.5f * D.asymNose, 0.0955f, 0.0048f - 0.006f * (D.noseL - 1.f));
    L.stomion = vec3(0, 0.0968f, -0.0195f);
    L.chin = vec3(D.asymChin, 0.0935f + 0.006f * (D.chinP - 1.f), -0.051f * D.chinH);     // soft tissue pogonion
    L.menton = vec3(0.8f * D.asymChin, 0.074f + 0.004f * (D.chinP - 1.f), -0.0655f * D.chinH);
}

// The face is sculpted from overlapping smooth primitives, back to front: skull, brow ridge, orbits, maxilla and
// cheekbones, mandible (angles, body, chin), cheek soft tissue (malar fat, the pad lateral to the nasolabial fold,
// jowls), the mouth (muzzle, philtrum columns, upper lip with its tubercle, two-lobed lower lip), the nose (dorsum
// with an optional hump, domed tip, columella, alae) and the lids. Blend radii set how soft each junction is: the
// nasolabial fold, the alar crease and the lid crease come from tight blends.
void addHeadPrims(BuildCtx& c) {
    const BodyDims& D = *c.D;
    Sdf& S = c.sdf;
    FaceLm L;
    faceLandmarks(c, L);
    const float hs = D.headS;
    auto P = [&](float x, float y, float z) { return headToModel(c, vec3(x, y, z)); };
    auto Pv = [&](vec3 v) { return headToModel(c, v); };
    auto R = [&](float r) { return r * hs; };
    const u32 HM = MK_HEAD;
    const float fem = D.fem, wc = D.weight - 0.5f, age = Saturate(D.age), mus = Saturate(D.muscle);
    const float lean = Saturate(-wc * 2.2f), full = Saturate(wc * 2.f), youth = 1.f - age;
    const float fw = D.faceW, jw = D.jawW;
    // ---- skull: cranium and forehead
    S.ellipsoid(P(0, -0.013f, 0.074f), vec3(0.0752f * fw, 0.098f * D.headLen, 0.1f) * hs, HM, R(0.01f));
    float fs = D.foreheadSlope;
    S.ellipsoid(P(0, 0.034f - 0.004f * fs, 0.088f), vec3(0.061f, 0.055f, 0.062f) * hs, HM, R(0.03f));
    // temples: a slight hollow behind the lateral orbital rim on lean / older faces
    float temple = Saturate(0.6f * lean + 0.4f * age - 0.15f);
    if (temple > 0.02f)
        for (int sd = 0; sd < 2; sd++) {
            Prim& q = S.prims[S.ellipsoid(P((sd ? 1.f : -1.f) * 0.0735f * fw, 0.042f, 0.06f), vec3(0.006f, 0.013f, 0.014f) * (hs * (0.5f + 0.5f * temple)), HM, R(0.014f))];
            q.op = OP_SUB;
        }
    // ---- brow ridge (glabella + supraorbital arches wrapping round to the temples)
    float br = (0.0085f + 0.004f * D.browRidge);
    S.ellipsoid(P(0, 0.0775f + 0.002f * D.browRidge, 0.0765f), vec3(0.015f, br * 1.0f, br * 1.05f) * hs, HM, R(0.018f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f, bz = sd ? D.asymBrow : 0.f;
        S.cone(P(sx * 0.011f, 0.0775f + 0.002f * D.browRidge, 0.0775f + bz), P(sx * 0.046f, 0.064f, 0.0785f + bz), R(br), R(br * 0.78f), HM, R(0.018f));
    }
    // ---- mid face (maxilla + cheeks) and cheekbones
    S.ellipsoid(P(0, 0.028f, 0.012f), vec3(0.0615f * fw, 0.062f, 0.062f) * hs, HM, R(0.02f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        // malar eminence below and lateral to the orbit
        S.ellipsoid(P(sx * 0.0488f * fw, 0.0555f, 0.034f), vec3(0.021f, 0.0165f, 0.0125f) * (hs * (0.85f + 0.2f * D.cheekB)), HM, R(0.022f));
        // zygomatic arch towards the ear
        S.cone(P(sx * 0.052f * fw, 0.045f, 0.036f), P(sx * 0.064f * fw, 0.0f, 0.03f), R(0.008f), R(0.006f), HM, R(0.014f));
    }
    // ---- mandible: base mass, angles, body along the jawline, chin
    S.ellipsoid(P(0, 0.029f, -0.027f * D.chinH), vec3(0.0525f * jw, 0.058f, 0.041f * D.chinH) * hs, HM, R(0.024f));
    const float gx = 0.0495f * jw * (0.96f + 0.08f * D.jawFlare);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 gon(sx * gx, 0.004f, -0.031f + 0.004f * (1.f - D.jawFlare));
        S.ellipsoid(Pv(gon + vec3(0, 0, 0.001f)), vec3(0.012f, 0.019f, 0.016f) * (hs * Lerp(1.f, 0.72f, fem) * (0.9f + 0.2f * D.jawFlare)), HM, R(0.02f));
        // jawline: from the angle forward to the corner of the chin
        vec3 cc(L.chin.x + sx * 0.0125f * (1.f + 0.45f * D.chinSquare), L.chin.y - 0.014f, L.chin.z - 0.0035f);
        S.cone(Pv(gon + vec3(-sx * 0.004f, 0.012f, -0.002f)), Pv(cc), R(0.0102f * Lerp(1.f, 0.85f, fem)), R(0.0108f * (0.9f + 0.2f * D.chinSquare)), HM, R(0.014f));
        // masseter
        S.ellipsoid(P(sx * 0.046f * jw, 0.018f, -0.008f), vec3(0.0105f, 0.02f, 0.025f) * (hs * (0.8f + 0.35f * mus) * Lerp(1.f, 0.85f, fem)), HM, R(0.02f));
    }
    // chin (mental protuberance): rounder / narrower for women, broad and square for some men
    {
        float cw = 0.0162f * (1.f + 0.4f * D.chinSquare) * (1.f + 0.25f * (jw - 1.f));
        S.ellipsoid(Pv(L.chin + vec3(0, -0.0125f, -0.0015f)), vec3(cw, 0.0125f, 0.0155f) * hs, HM, R(0.012f));
        if (D.chinCleft > 0.f) {
            Prim& q = S.prims[S.cone(Pv(L.chin + vec3(0, 0.002f, 0.006f)), Pv(L.chin + vec3(0, 0.0015f, -0.008f)), R(0.0021f * D.chinCleft), R(0.0019f * D.chinCleft), HM, R(0.003f))];
            q.op = OP_SUB;
        }
    }
    // under the chin into the neck (+ submental fullness on heavier faces)
    S.ellipsoid(P(0, 0.028f, -0.057f * D.chinH), vec3(0.036f, 0.045f, 0.022f) * hs, HM | MK_NECK, R(0.022f));
    if (full > 0.05f) S.ellipsoid(P(0, 0.046f, -0.062f * D.chinH), vec3(0.028f, 0.026f, 0.013f) * (hs * (0.4f + 0.6f * full)), HM | MK_NECK, R(0.02f));
    // ---- cheek soft tissue
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        // malar fat ("apple" of the cheek): fuller on young, female and heavier faces
        float mf = Saturate(0.35f + 0.35f * youth + 0.35f * full + 0.2f * fem - 0.25f * lean);
        S.ellipsoid(P(sx * 0.0345f, 0.0625f, 0.019f), vec3(0.019f, 0.0125f, 0.017f) * (hs * (0.55f + 0.5f * mf)), HM, R(0.022f));
        // pad lateral to the nasolabial fold (ala -> past the mouth corner); its tight medial blend is the fold
        float nl = 0.55f + 0.45f * age + 0.25f * full;
        vec3 a(sx * 0.0215f, 0.0835f, 0.0095f), b(sx * 0.0315f, 0.0735f, -0.0235f);
        S.cone(Pv(a), Pv(b), R(0.0072f * nl), R(0.0086f * nl), HM, R(0.0065f));
        // jowls (age / weight) and the buccal hollow under the cheekbone (lean)
        float jl = Saturate(0.9f * age + 0.6f * full - 0.35f);
        if (jl > 0.02f) S.ellipsoid(P(sx * 0.0385f * jw, 0.061f, -0.037f), vec3(0.012f, 0.012f, 0.012f) * (hs * (0.6f + 0.5f * jl)), HM, R(0.016f));
        float bh = Saturate(1.1f * lean + 0.3f * age * (1.f - full) - 0.1f);
        if (bh > 0.02f) {
            Prim& q = S.prims[S.ellipsoid(P(sx * 0.052f * fw, 0.047f, 0.004f), vec3(0.008f, 0.013f, 0.013f) * (hs * (0.5f + 0.5f * bh)), HM, R(0.018f))];
            q.op = OP_SUB;
        }
    }
    // ---- mouth: muzzle over the dental arch and the skin of the upper lip
    S.ellipsoid(P(0, 0.066f, -0.012f), vec3(0.034f * D.lipW, 0.0305f, 0.029f) * hs, HM, R(0.02f));
    S.ellipsoid(Pv(L.subnasale + vec3(0, -0.0105f, -0.0105f)), vec3(0.0185f * D.lipW, 0.0105f, 0.0115f) * hs, HM, R(0.01f));
    // lips: upper (tubercle + cupid's bow) and lower (two lobes), curved chains towards the corners
    float lf = D.lipFull;
    float ulR = 0.0053f * lf, llR = 0.0068f * lf;
    vec3 ulC(0, L.stomion.y - 0.0004f, L.stomion.z + 0.0056f), llC(0, L.stomion.y - 0.003f, L.stomion.z - 0.0068f);
    S.ellipsoid(Pv(ulC + vec3(0, 0.0005f, -0.0016f)), vec3(0.0062f * D.lipW, 0.0045f * lf, 0.0042f * lf) * hs, HM, R(0.003f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 mc = L.mouthCorner[sd];
        // philtrum column: nose base down to the peak of the cupid's bow
        vec3 pk(sx * 0.0054f * D.lipW, ulC.y + 0.0004f, ulC.z + ulR * 0.92f + 0.0007f * D.lipBow);
        S.cone(Pv(L.subnasale + vec3(sx * 0.0033f, -0.0016f, -0.0016f)), Pv(pk), R(0.0017f), R(0.0021f), HM, R(0.0034f));
        vec3 ulM(sx * 0.0115f * D.lipW, ulC.y - 0.0014f, ulC.z + 0.0003f), llM(sx * 0.0115f * D.lipW, llC.y - 0.0012f, llC.z - 0.0002f);
        S.cone(Pv(ulC), Pv(ulM), R(ulR), R(ulR * 0.95f), HM, R(0.003f));
        S.cone(Pv(ulM), Pv(mc + vec3(-sx * 0.002f, 0.0f, 0.0015f)), R(ulR * 0.95f), R(0.0022f), HM, R(0.003f));
        S.ellipsoid(Pv(vec3(sx * 0.0062f * D.lipW, llC.y + 0.0002f, llC.z)), vec3(0.0092f * D.lipW, llR * 0.96f, llR * 0.9f) * hs, HM, R(0.003f));
        S.cone(Pv(llM), Pv(mc + vec3(-sx * 0.002f, 0.0f, -0.0015f)), R(llR * 0.85f), R(0.0022f), HM, R(0.003f));
    }
    // ---- nose: dorsum (bony bridge + cartilage, optional hump), domed tip, columella, alae
    {
        float nw = D.noseW;
        vec3 N = L.nasion, T = L.noseTip, Sn = L.subnasale;
        vec3 sp = T + vec3(0, -0.0068f, 0.0068f);   // supratip
        S.cone(Pv(N + vec3(0, -0.0035f, 0.0015f)), Pv(sp), R(0.0052f * (0.85f + 0.15f * nw)), R(0.0066f * nw), HM, R(0.01f), 0.92f, 1.f, vec3(1, 0, 0));
        if (D.noseHump > 0.05f)
            S.ellipsoid(Pv(lerp(N, sp, 0.42f) + vec3(0, 0.0012f, 0)), vec3(0.0044f, 0.0034f, 0.0065f) * (hs * D.noseHump), HM, R(0.004f));
        S.ellipsoid(Pv(T + vec3(0, -0.0068f, -0.0008f)), vec3(0.0088f * nw, 0.0068f, 0.0082f) * hs, HM, R(0.006f));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            S.ellipsoid(Pv(T + vec3(sx * 0.0036f, -0.0047f, 0.f)), vec3(0.0052f, 0.0048f, 0.0055f) * hs, HM, R(0.003f));
        }
        S.cone(Pv(T + vec3(0, -0.0062f, -0.0068f)), Pv(Sn + vec3(0, 0.0012f, 0.0022f)), R(0.0033f), R(0.0036f), HM, R(0.003f), 0.8f, 1.f, vec3(1, 0, 0));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            vec3 A = L.ala[sd];
            // lateral crus: ala to tip
            S.cone(Pv(A + vec3(-sx * 0.0028f, 0.0042f, 0.0012f)), Pv(T + vec3(sx * 0.0042f, -0.0062f, -0.001f)), R(0.0044f), R(0.004f), HM, R(0.004f));
            S.ellipsoid(Pv(A + vec3(0, 0, -0.0005f)), vec3(0.0058f, 0.0076f, 0.0064f) * hs, HM, R(0.0032f));
        }
    }
    // ---- eyes: sockets (carved), then lids wrapped around the eyeballs, bags under older eyes
    for (int sd = 0; sd < 2; sd++) {
        vec3 e = L.eye[sd];
        Prim& q = S.prims[S.ellipsoid(Pv(e + vec3(0, 0.0055f, 0.0018f)), vec3(0.0195f, 0.0145f, 0.0135f) * hs, HM, R(0.01f))];
        q.op = OP_SUB;
    }
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 e = L.eye[sd];
        float er = L.eyeR;
        S.ellipsoid(Pv(e), vec3(er + 0.0009f) * hs, HM, R(0.003f));
        // upper lid (hooded / monolid for high lidFold), thick margin
        float up = 0.0012f + 0.0016f * D.lidFold;
        S.ellipsoid(Pv(e + vec3(0, -0.0006f, 0.0032f)), vec3(er + 0.0019f, er + up + 0.0003f, er * 0.8f) * hs, HM, R(0.0035f));
        S.ellipsoid(Pv(e + vec3(0, -0.001f, -0.0035f)), vec3(er + 0.0013f, er + 0.0011f, er * 0.62f) * hs, HM, R(0.003f));
        float bag = Saturate(1.2f * age - 0.35f + 0.3f * full);
        if (bag > 0.02f) S.ellipsoid(Pv(e + vec3(sx * 0.002f, 0.0052f, -0.0128f)), vec3(0.0105f, 0.0042f, 0.0042f) * (hs * (0.6f + 0.4f * bag)), HM, R(0.005f));
    }
    // ear roots (the ears themselves are separate meshes)
    for (int sd = 0; sd < 2; sd++) S.ellipsoid(Pv(L.ear[sd] + vec3(0, 0.002f, 0)), vec3(0.008f, 0.016f, 0.021f) * hs, HM, R(0.008f));
}

// ------------------------------------------------------------------------------------------------
