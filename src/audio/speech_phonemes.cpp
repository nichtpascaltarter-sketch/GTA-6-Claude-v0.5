// Phoneme inventory: articulatory/acoustic targets for ~46 American English phonemes.
// Formant targets are for an adult male reference tract (scaled by VoiceParams::formantScale).
// Vowel values follow Peterson & Barney (1952) / Hillenbrand et al. (1995) averages, slightly
// exaggerated toward the periphery (helps intelligibility of synthetic speech). Durations follow
// Klatt's (MITalk) inherent/minimum duration table. Consonant coarticulation uses a Holmes-style
// rank + locus scheme: boundary = fix + prop * neighbour target (locus equations).
#include "speech_internal.h"

namespace Speech {
namespace detail {

struct PhTable {
    PhInfo p[PH_COUNT];
};

enum Place { PL_NONE, PL_LABIAL, PL_DENTAL, PL_ALVEOLAR, PL_POSTALV, PL_VELAR, PL_GLOTTAL };

static void phClear(PhInfo& p, const char* name, u32 flags) {
    memset(&p, 0, sizeof(p));
    p.name = name;
    p.flags = flags;
    p.fricByp = -99.f;
    for (int i = 0; i < 3; i++) p.fricA[i] = -99.f, p.fricF[i] = 1000.f, p.fricB[i] = 1000.f;
}

static void phVowel(PhInfo& p, const char* name, u32 flags, float inh, float mn, float f1, float f2, float f3, float b1,
                    float b2, float b3, float e1 = 0, float e2 = 0, float e3 = 0) {
    phClear(p, name, flags | PF_VOWEL | PF_VOICED | PF_SONOR | (e1 > 0 ? PF_DIPH : 0));
    p.inh = inh, p.mn = mn;
    p.f[0] = f1, p.f[1] = f2, p.f[2] = f3;
    p.fe[0] = e1 > 0 ? e1 : f1, p.fe[1] = e2 > 0 ? e2 : f2, p.fe[2] = e3 > 0 ? e3 : f3;
    p.bw[0] = b1, p.bw[1] = b2, p.bw[2] = b3;
    p.av = 60.f;
    p.rank = 2;
    p.tInt = 50.f;
}

// Locus / coarticulation per place of articulation (F1 depends on manner).
static void phPlace(PhInfo& p, Place pl, float f1fix, float f1prop) {
    p.fix[0] = f1fix, p.prop[0] = f1prop;
    switch (pl) {
        case PL_LABIAL: p.fix[1] = 150.f, p.prop[1] = 0.80f, p.fix[2] = 300.f, p.prop[2] = 0.82f; p.flags |= PF_LABIAL; break;
        case PL_DENTAL: p.fix[1] = 900.f, p.prop[1] = 0.50f, p.fix[2] = 1300.f, p.prop[2] = 0.50f; p.flags |= PF_DENTAL; break;
        case PL_ALVEOLAR: p.fix[1] = 1000.f, p.prop[1] = 0.45f, p.fix[2] = 1400.f, p.prop[2] = 0.50f; p.flags |= PF_ALVEOLAR; break;
        case PL_POSTALV: p.fix[1] = 1400.f, p.prop[1] = 0.35f, p.fix[2] = 1500.f, p.prop[2] = 0.45f; p.flags |= PF_POSTALV; break;
        case PL_VELAR: p.fix[1] = 0.f, p.prop[1] = 1.f, p.fix[2] = 0.f, p.prop[2] = 1.f; p.flags |= PF_VELAR; break;  // computed dynamically
        case PL_GLOTTAL: p.flags |= PF_GLOTTAL; break;
        default: break;
    }
}

static void phTargets(PhInfo& p, float f1, float f2, float f3, float b1, float b2, float b3) {
    p.f[0] = p.fe[0] = f1, p.f[1] = p.fe[1] = f2, p.f[2] = p.fe[2] = f3;
    p.bw[0] = b1, p.bw[1] = b2, p.bw[2] = b3;
}

static void phFric(PhInfo& p, float f1, float b1, float a1, float f2, float b2, float a2, float f3, float b3, float a3,
                   float byp) {
    p.fricF[0] = f1, p.fricB[0] = b1, p.fricA[0] = a1;
    p.fricF[1] = f2, p.fricB[1] = b2, p.fricA[1] = a2;
    p.fricF[2] = f3, p.fricB[2] = b3, p.fricA[2] = a3;
    p.fricByp = byp;
}

static void phStop(PhInfo& p, const char* name, Place pl, bool voiced, float inh, float mn, float burstMs, float burstAf,
                   float vot, float extMs) {
    phClear(p, name, PF_CONS | PF_STOP | PF_OBSTRUENT | (voiced ? PF_VOICED : 0));
    p.inh = inh, p.mn = mn;
    phPlace(p, pl, 150.f, 0.22f);
    p.rank = 25;
    p.tInt = 10.f, p.tExt = extMs, p.tExtF1 = 25.f;
    p.burstMs = burstMs, p.burstAf = burstAf, p.vot = vot;
    p.av = voiced ? 42.f : 0.f;  // voice bar (only used intervocalically)
}

static void phFricative(PhInfo& p, const char* name, Place pl, bool voiced, float inh, float mn, float af, float av,
                        float extMs) {
    phClear(p, name, PF_CONS | PF_FRIC | PF_OBSTRUENT | (voiced ? PF_VOICED : 0));
    p.inh = inh, p.mn = mn;
    phPlace(p, pl, 170.f, 0.3f);
    p.rank = 18;
    p.tInt = 15.f, p.tExt = extMs, p.tExtF1 = 25.f;
    p.af = af, p.av = av;
}

static void phNasal(PhInfo& p, const char* name, Place pl, float inh, float mn, float f2, float f3, float zero) {
    phClear(p, name, PF_CONS | PF_NASAL | PF_VOICED | PF_SONOR);
    p.inh = inh, p.mn = mn;
    phPlace(p, pl, 200.f, 0.2f);
    phTargets(p, 270.f, f2, f3, 70.f, 280.f, 320.f);
    p.rank = 12;
    p.tInt = 12.f, p.tExt = 40.f, p.tExtF1 = 20.f;
    p.av = 53.f;
    p.nasalZero = zero;
}

static void phApprox(PhInfo& p, const char* name, u32 flags, float inh, float mn, float f1, float f2, float f3, float b1,
                     float b2, float b3, float fix1, float prop1, float fix2, float prop2, float fix3, float prop3,
                     float tInt, float tExt) {
    phClear(p, name, PF_CONS | PF_VOICED | PF_SONOR | flags);
    p.inh = inh, p.mn = mn;
    phTargets(p, f1, f2, f3, b1, b2, b3);
    p.fix[0] = fix1, p.prop[0] = prop1, p.fix[1] = fix2, p.prop[1] = prop2, p.fix[2] = fix3, p.prop[2] = prop3;
    p.rank = 10;
    p.tInt = tInt, p.tExt = tExt, p.tExtF1 = tExt * 0.6f;
    p.av = 56.f;
}

static PhTable buildPhTable() {
    PhTable t;
    PhInfo* p = t.p;
    phClear(p[PH_SIL], "SIL", PF_SIL);
    p[PH_SIL].inh = p[PH_SIL].mn = 100.f;
    phTargets(p[PH_SIL], 500, 1500, 2500, 100, 150, 200);

    const u32 FR = PF_FRONT, RO = PF_ROUND, HI = PF_HIGH, LO = PF_LOW, TE = PF_TENSE;
    //                 name  flags            inh  min   F1    F2    F3    B1   B2   B3    (end targets)
    phVowel(p[PH_IY], "IY", FR | HI | TE,    155,  55,  290, 2280, 3000,  50, 110, 180,  270, 2350, 3050);
    phVowel(p[PH_IH], "IH", FR | HI,         135,  40,  420, 1960, 2600,  60, 100, 180);
    phVowel(p[PH_EY], "EY", FR | TE,         190, 100,  470, 2020, 2650,  60, 100, 180,  320, 2260, 2900);
    phVowel(p[PH_EH], "EH", FR,              150,  70,  570, 1800, 2550,  70, 100, 180);
    phVowel(p[PH_AE], "AE", FR | LO,         230,  80,  680, 1720, 2450,  80, 100, 200,  650, 1760, 2450);
    phVowel(p[PH_AA], "AA", LO,              240, 100,  740, 1130, 2450,  90, 100, 200);
    phVowel(p[PH_AO], "AO", LO | RO,         240, 100,  600,  900, 2450,  90,  90, 200,  580,  920, 2450);
    phVowel(p[PH_OW], "OW", RO | TE,         220,  80,  520,  980, 2400,  70,  80, 200,  390,  820, 2350);
    phVowel(p[PH_UH], "UH", RO | HI,         160,  60,  450, 1100, 2350,  60,  90, 200);
    phVowel(p[PH_UW], "UW", RO | HI | TE,    210,  70,  330, 1000, 2300,  60,  90, 200,  300,  900, 2250);
    phVowel(p[PH_AH], "AH", 0,               140,  60,  620, 1220, 2550,  80,  90, 200);
    phVowel(p[PH_AX], "AX", PF_REDUCED,      120,  60,  520, 1450, 2500,  80, 100, 200);
    phVowel(p[PH_IX], "IX", PF_REDUCED | HI, 110,  40,  420, 1780, 2550,  70, 100, 200);
    phVowel(p[PH_ER], "ER", PF_RHOTIC,       180,  80,  470, 1350, 1700,  80, 100, 110);
    phVowel(p[PH_AXR], "AXR", PF_RHOTIC | PF_REDUCED, 150, 60, 480, 1400, 1750, 80, 100, 120);
    phVowel(p[PH_AY], "AY", TE,              250, 150,  720, 1250, 2500,  80, 100, 200,  400, 1950, 2600);
    phVowel(p[PH_AW], "AW", TE,              260, 100,  720, 1300, 2500,  80,  90, 200,  450, 1000, 2400);
    phVowel(p[PH_OY], "OY", TE | RO,         280, 150,  550,  920, 2450,  80,  90, 200,  400, 1850, 2600);

    // Stops:        name  place         voiced inh min burst  burstAf VOT extMs
    phStop(p[PH_P], "P", PL_LABIAL, false, 90, 50, 7, 56, 55, 40);
    phStop(p[PH_B], "B", PL_LABIAL, true, 85, 60, 6, 51, 12, 40);
    phStop(p[PH_T], "T", PL_ALVEOLAR, false, 75, 50, 11, 63, 65, 50);
    phStop(p[PH_D], "D", PL_ALVEOLAR, true, 75, 50, 9, 58, 15, 50);
    phStop(p[PH_K], "K", PL_VELAR, false, 80, 60, 18, 65, 70, 55);
    phStop(p[PH_G], "G", PL_VELAR, true, 80, 60, 15, 60, 18, 55);
    phTargets(p[PH_P], 250, 900, 2200, 80, 120, 200);
    phTargets(p[PH_B], 200, 900, 2200, 80, 120, 200);
    phTargets(p[PH_T], 250, 1700, 2650, 80, 120, 200);
    phTargets(p[PH_D], 200, 1700, 2650, 80, 120, 200);
    phTargets(p[PH_K], 250, 1900, 2600, 80, 120, 200);
    phTargets(p[PH_G], 200, 1900, 2600, 80, 120, 200);
    // Burst spectra: labial diffuse/flat-falling, alveolar high diffuse-rising, velar compact mid (F2/F3 of vowel).
    phFric(p[PH_P], 800, 1200, -4, 2500, 2500, -10, 5000, 4000, -16, -4);
    phFric(p[PH_B], 800, 1200, -4, 2500, 2500, -10, 5000, 4000, -16, -4);
    phFric(p[PH_T], 3900, 1500, 0, 6000, 2500, -3, 2600, 800, -14, -20);
    phFric(p[PH_D], 3900, 1500, 0, 6000, 2500, -3, 2600, 800, -14, -20);
    phFric(p[PH_K], 2200, 350, 0, 3600, 1500, -14, 1500, 600, -14, -28);
    phFric(p[PH_G], 2200, 350, 0, 3600, 1500, -14, 1500, 600, -14, -28);

    // Flap and glottal stop.
    phClear(p[PH_DX], "DX", PF_CONS | PF_VOICED | PF_FLAP | PF_ALVEOLAR);
    p[PH_DX].inh = 22, p[PH_DX].mn = 18;
    phPlace(p[PH_DX], PL_ALVEOLAR, 200.f, 0.35f);
    phTargets(p[PH_DX], 300, 1650, 2650, 90, 120, 200);
    p[PH_DX].rank = 14, p[PH_DX].tInt = 6, p[PH_DX].tExt = 25, p[PH_DX].tExtF1 = 18;
    p[PH_DX].av = 50.f;
    phClear(p[PH_Q], "Q", PF_CONS | PF_GLOTTAL);
    p[PH_Q].inh = 45, p[PH_Q].mn = 25;
    p[PH_Q].rank = 1;
    phTargets(p[PH_Q], 500, 1500, 2500, 100, 150, 200);

    // Affricates: closure then postalveolar frication.
    phClear(p[PH_CH], "CH", PF_CONS | PF_AFFR | PF_OBSTRUENT | PF_SIBILANT);
    p[PH_CH].inh = 110, p[PH_CH].mn = 70;
    phPlace(p[PH_CH], PL_POSTALV, 150.f, 0.25f);
    phTargets(p[PH_CH], 300, 1850, 2700, 80, 120, 200);
    p[PH_CH].rank = 25, p[PH_CH].tInt = 15, p[PH_CH].tExt = 50, p[PH_CH].tExtF1 = 25;
    p[PH_CH].af = 62.f;
    p[PH_CH].burstMs = 4, p[PH_CH].burstAf = 56;
    phFric(p[PH_CH], 2700, 700, 0, 4000, 1500, -5, 6000, 2500, -14, -40);
    phClear(p[PH_JH], "JH", PF_CONS | PF_AFFR | PF_OBSTRUENT | PF_SIBILANT | PF_VOICED);
    p[PH_JH].inh = 90, p[PH_JH].mn = 55;
    phPlace(p[PH_JH], PL_POSTALV, 150.f, 0.25f);
    phTargets(p[PH_JH], 260, 1850, 2700, 80, 120, 200);
    p[PH_JH].rank = 25, p[PH_JH].tInt = 15, p[PH_JH].tExt = 50, p[PH_JH].tExtF1 = 25;
    p[PH_JH].af = 57.f, p[PH_JH].av = 46.f;
    p[PH_JH].burstMs = 4, p[PH_JH].burstAf = 50;
    phFric(p[PH_JH], 2700, 700, 0, 4000, 1500, -5, 6000, 2500, -14, -40);

    // Fricatives:        name  place         voiced inh  min  AF  AV   ext
    phFricative(p[PH_F], "F", PL_LABIAL, false, 100, 80, 47, 0, 35);
    phFricative(p[PH_V], "V", PL_LABIAL, true, 60, 40, 39, 50, 35);
    phFricative(p[PH_TH], "TH", PL_DENTAL, false, 90, 60, 45, 0, 40);
    phFricative(p[PH_DH], "DH", PL_DENTAL, true, 50, 30, 35, 49, 40);
    phFricative(p[PH_S], "S", PL_ALVEOLAR, false, 105, 60, 56, 0, 45);
    phFricative(p[PH_Z], "Z", PL_ALVEOLAR, true, 75, 40, 51, 50, 45);
    phFricative(p[PH_SH], "SH", PL_POSTALV, false, 105, 80, 63, 0, 50);
    phFricative(p[PH_ZH], "ZH", PL_POSTALV, true, 70, 40, 55, 50, 50);
    p[PH_S].flags |= PF_SIBILANT, p[PH_Z].flags |= PF_SIBILANT, p[PH_SH].flags |= PF_SIBILANT, p[PH_ZH].flags |= PF_SIBILANT;
    phTargets(p[PH_F], 340, 1100, 2100, 100, 150, 200);
    phTargets(p[PH_V], 260, 1100, 2100, 70, 150, 200);
    phTargets(p[PH_TH], 320, 1300, 2550, 100, 150, 200);
    phTargets(p[PH_DH], 270, 1300, 2550, 70, 150, 200);
    phTargets(p[PH_S], 320, 1450, 2600, 100, 150, 200);
    phTargets(p[PH_Z], 250, 1450, 2600, 70, 150, 200);
    phTargets(p[PH_SH], 300, 1850, 2700, 100, 150, 200);
    phTargets(p[PH_ZH], 260, 1850, 2700, 70, 150, 200);
    phFric(p[PH_F], 2200, 3000, -8, 5500, 5000, -4, 8500, 5000, -9, -16);
    phFric(p[PH_V], 2200, 3000, -8, 5500, 5000, -4, 8500, 5000, -9, -16);
    phFric(p[PH_TH], 3500, 2000, -10, 6500, 3000, -2, 8500, 3000, -6, -16);
    phFric(p[PH_DH], 3500, 2000, -10, 6500, 3000, -2, 8500, 3000, -6, -16);
    phFric(p[PH_S], 4800, 1400, 0, 7500, 2500, -4, 3200, 700, -22, -28);
    phFric(p[PH_Z], 4800, 1400, 0, 7500, 2500, -4, 3200, 700, -22, -28);
    phFric(p[PH_SH], 2600, 700, 0, 3900, 1400, -5, 6000, 2500, -14, -40);
    phFric(p[PH_ZH], 2600, 700, 0, 3900, 1400, -5, 6000, 2500, -14, -40);

    // /h/: aspiration through the vocal tract shaped like the neighbouring vowel (transparent rank).
    phClear(p[PH_HH], "HH", PF_CONS | PF_GLOTTAL);
    p[PH_HH].inh = 80, p[PH_HH].mn = 20;
    phTargets(p[PH_HH], 500, 1500, 2500, 300, 150, 250);
    p[PH_HH].rank = 1;
    p[PH_HH].ah = 51.f;

    // Nasals:     name  place        inh min  F2    F3   anti-resonance
    phNasal(p[PH_M], "M", PL_LABIAL, 70, 50, 1000, 2200, 1000);
    phNasal(p[PH_N], "N", PL_ALVEOLAR, 60, 30, 1500, 2550, 1700);
    phNasal(p[PH_NG], "NG", PL_VELAR, 95, 60, 2000, 2700, 3200);

    // Liquids and glides: slow characteristic trajectories.
    //                                    inh min  F1   F2    F3    B1  B2   B3   fix1 p1   fix2 p2    fix3  p3   tInt tExt
    phApprox(p[PH_L], "L", PF_LIQUID | PF_ALVEOLAR, 80, 40, 330, 1050, 2800, 60, 120, 250, 150, 0.5f, 500, 0.5f, 1400, 0.5f, 30, 50);
    phApprox(p[PH_LX], "LX", PF_LIQUID | PF_ALVEOLAR, 80, 40, 380, 880, 2650, 70, 130, 300, 200, 0.5f, 380, 0.45f, 1300, 0.5f, 35, 55);
    phApprox(p[PH_R], "R", PF_LIQUID | PF_RHOTIC | PF_POSTALV, 80, 30, 320, 1100, 1450, 70, 100, 120, 200, 0.4f, 550, 0.5f, 800, 0.4f, 40, 65);
    phApprox(p[PH_W], "W", PF_GLIDE | PF_LABIAL | PF_ROUND, 80, 60, 290, 650, 2200, 50, 80, 150, 150, 0.5f, 350, 0.5f, 1100, 0.5f, 40, 60);
    phApprox(p[PH_Y], "Y", PF_GLIDE | PF_FRONT, 80, 40, 260, 2150, 3000, 45, 150, 250, 150, 0.4f, 1100, 0.5f, 1500, 0.5f, 40, 60);
    return t;
}

static const PhTable& phTable() {
    static const PhTable table = buildPhTable();  // thread-safe one-time init
    return table;
}

const PhInfo& phInfo(int ph) {
    const PhTable& t = phTable();
    if (ph < 0 || ph >= PH_COUNT) return t.p[PH_SIL];
    return t.p[ph];
}

int phFromName(const char* s, int len) {
    if (len <= 0 || len > 3) return -1;
    char buf[4] = {0, 0, 0, 0};
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        buf[i] = c;
    }
    const PhTable& t = phTable();
    for (int i = 0; i < PH_COUNT; i++)
        if (strcmp(t.p[i].name, buf) == 0) return i;
    // Aliases used by some ARPAbet variants.
    if (strcmp(buf, "EL") == 0) return PH_LX;
    if (strcmp(buf, "EM") == 0) return PH_M;
    if (strcmp(buf, "EN") == 0) return PH_N;
    if (strcmp(buf, "NX") == 0) return PH_NG;
    if (strcmp(buf, "UX") == 0) return PH_UW;
    if (strcmp(buf, "H") == 0) return PH_HH;
    if (strcmp(buf, "PAU") == 0) return PH_SIL;
    return -1;
}

bool parsePhonemes(const char* s, Pron& out) {
    bool ok = true;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '-' || *s == '~') s++;
        if (!*s) break;
        const char* b = s;
        while (*s && ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))) s++;
        int len = (int)(s - b);
        int stress = -1;
        if (*s >= '0' && *s <= '2') stress = *s++ - '0';
        if (len == 0) {  // skip unknown character
            if (*s) s++;
            ok = false;
            continue;
        }
        int ph = phFromName(b, len);
        if (ph < 0) {
            ok = false;
            continue;
        }
        PhS x;
        x.ph = (u8)ph;
        x.stress = (u8)(isVowel(ph) ? (stress < 0 ? 1 : stress) : 0);
        out.push_back(x);
    }
    return ok;
}

}  // namespace detail
}  // namespace Speech
