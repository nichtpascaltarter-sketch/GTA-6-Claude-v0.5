// Footstep and body foley layers (included by sfx.cpp inside namespace sfxgen, after the synthesis toolkit). The
// mixer (Mixer::startFootstep) assembles each step at runtime: heel strike and roll-off by footwear on hard ground or
// a dull sole on soft ground, the surface's own sound, clothing / gear, puddle splashes, landings and scuffs; body
// impacts and foley bursts for ragdolls, knock-downs, vaults and climbs. Every layer is dry: the mixer adds distance,
// occlusion and the room.

// Heel strike on hard ground (asphalt, concrete, boards, plate), by footwear.
static void st_heel(Buf& b, int fw) {
    float p = b.rnd(0.9f, 1.1f);
    switch (fw) {
        case FOOTWEAR_SNEAKER: {  // rubber sole: a muffled thud and the tread meeting the ground
            noise(b, 0.f, 0.06f, 0.9f, 0.0006f, 0.012f, FLP, 950.f * p, 520.f, 0.02f, 0.7f);
            tone(b, 0.f, 0.05f, 150.f * p, 95.f, 0.012f, 0.3f, 0.0006f, 0.014f);
            noise(b, 0.f, 0.03f, 0.25f, 0.0003f, 0.004f, FBP, 3300.f * p, -1.f, 0.1f, 1.f);
            break;
        }
        case FOOTWEAR_LEATHER: {  // leather sole and a hard heel block: a clear click with a woody knock
            click(b, 0.f, 0.5f, 3);
            noise(b, 0.f, 0.05f, 0.8f, 0.0003f, 0.007f, FBP, 2600.f * p, 1900.f, 0.02f, 1.1f);
            Mode m[3] = {{1250.f * p, 0.012f, 0.6f}, {2300.f * p, 0.008f, 0.4f}, {620.f * p, 0.015f, 0.4f}};
            modes(b, 0.f, 0.5f, m, 3);
            tone(b, 0.f, 0.05f, 130.f * p, 90.f, 0.01f, 0.35f, 0.0005f, 0.012f);
            break;
        }
        case FOOTWEAR_HEEL: {  // a heel tip: small contact, bright "tock" with a short ring
            click(b, 0.f, 0.6f, 2);
            Mode m[4] = {{3100.f * p, 0.02f, 0.8f}, {4700.f * p, 0.012f, 0.5f}, {1900.f * p, 0.015f, 0.4f}, {7200.f * p, 0.006f, 0.25f}};
            modes(b, 0.f, 0.55f, m, 4);
            noise(b, 0.f, 0.03f, 0.4f, 0.0002f, 0.004f, FHP, 3000.f, -1.f, 0.1f, 0.7f);
            tone(b, 0.f, 0.03f, 220.f * p, 160.f, 0.006f, 0.12f, 0.0005f, 0.008f);
            break;
        }
        case FOOTWEAR_BOOT: {  // heavy boot: weighty thump, lug sole knock, grit under the lugs
            tone(b, 0.f, 0.1f, 120.f * p, 70.f, 0.02f, 0.9f, 0.0008f, 0.03f);
            noise(b, 0.f, 0.08f, 0.8f, 0.0005f, 0.014f, FLP, 1300.f * p, 500.f, 0.03f, 0.8f);
            Mode m[3] = {{480.f * p, 0.02f, 0.6f}, {860.f * p, 0.015f, 0.4f}, {1600.f * p, 0.01f, 0.3f}};
            modes(b, 0.f, 0.45f, m, 3);
            click(b, 0.f, 0.35f, 4);
            noise(b, 0.004f, 0.05f, 0.2f, 0.001f, 0.012f, FBP, 3500.f, -1.f, 0.1f, 0.9f);
            break;
        }
        case FOOTWEAR_SANDAL: {  // flip-flop sole: a rubbery slap
            noise(b, 0.f, 0.05f, 0.9f, 0.0003f, 0.008f, FBP, 1500.f * p, 1100.f, 0.02f, 0.9f);
            click(b, 0.f, 0.3f, 4);
            tone(b, 0.f, 0.04f, 160.f * p, 110.f, 0.01f, 0.3f, 0.0005f, 0.01f);
            break;
        }
        default: {  // bare skin on stone: a soft slap
            noise(b, 0.f, 0.05f, 0.8f, 0.0004f, 0.007f, FBP, 1100.f * p, 800.f, 0.02f, 0.8f);
            noise(b, 0.f, 0.03f, 0.3f, 0.0002f, 0.003f, FHP, 2500.f, -1.f, 0.1f, 0.7f);
            tone(b, 0.f, 0.04f, 140.f * p, 100.f, 0.01f, 0.35f, 0.0005f, 0.01f);
            break;
        }
    }
}

// Roll-off and toe, by footwear (for sandals the sole flapping back against the heel).
static void st_toe(Buf& b, int fw) {
    float p = b.rnd(0.9f, 1.1f);
    switch (fw) {
        case FOOTWEAR_SNEAKER:  // rubber rolling off: a soft scuff, now and then the tread squeaks
            noise(b, 0.f, 0.09f, 0.6f, 0.006f, 0.02f, FBP, 1800.f * p, 2600.f * p, 0.04f, 0.8f);
            noise(b, 0.f, 0.04f, 0.4f, 0.0008f, 0.008f, FLP, 700.f * p, -1.f, 0.1f, 0.7f);
            if (b.chance(0.2f)) fmNote(b, 0.012f, 0.05f, b.rnd(1800.f, 2800.f), 1.f, 0.8f, 0.02f, 0.12f, 0.004f, 0.015f);
            break;
        case FOOTWEAR_LEATHER:  // the sole's front taps down and scuffs
            click(b, 0.f, 0.3f, 3);
            noise(b, 0.f, 0.05f, 0.5f, 0.0004f, 0.006f, FBP, 2900.f * p, -1.f, 0.1f, 1.f);
            noise(b, 0.004f, 0.08f, 0.25f, 0.004f, 0.02f, FBP, 4200.f * p, 3000.f, 0.03f, 0.8f);
            break;
        case FOOTWEAR_HEEL:  // the front sole of a heeled shoe: a lighter tap
            noise(b, 0.f, 0.04f, 0.5f, 0.0004f, 0.006f, FBP, 2200.f * p, -1.f, 0.1f, 1.f);
            click(b, 0.f, 0.15f, 3);
            break;
        case FOOTWEAR_BOOT:  // lugs thumping and scraping off
            noise(b, 0.f, 0.08f, 0.6f, 0.001f, 0.015f, FLP, 900.f * p, 600.f, 0.03f, 0.8f);
            noise(b, 0.005f, 0.09f, 0.3f, 0.005f, 0.02f, FBP, 3000.f * p, 2200.f, 0.04f, 0.8f);
            break;
        case FOOTWEAR_SANDAL:  // the flap: the sandal slaps back against the heel as the foot lifts
            noise(b, 0.f, 0.04f, 1.f, 0.0002f, 0.005f, FBP, 1800.f * p, 1500.f, 0.02f, 1.1f);
            click(b, 0.f, 0.4f, 3);
            tone(b, 0.f, 0.02f, 300.f * p, 220.f, 0.005f, 0.2f, 0.0003f, 0.006f);
            break;
        default:  // skin peeling off the stone
            noise(b, 0.f, 0.05f, 0.5f, 0.003f, 0.012f, FBP, 2600.f * p, 3600.f, 0.02f, 0.9f);
            noise(b, 0.f, 0.02f, 0.3f, 0.0002f, 0.003f, FBP, 1400.f * p, -1.f, 0.1f, 0.8f);
            break;
    }
}

// Any sole on soft ground: a dull, damped thump (the surface layer carries the character).
static void st_soft(Buf& b) {
    float p = b.rnd(0.9f, 1.1f);
    noise(b, 0.f, 0.09f, 1.f, 0.002f, 0.02f, FLP, 420.f * p, 250.f, 0.04f, 0.7f, 1);
    tone(b, 0.f, 0.07f, 95.f * p, 65.f, 0.02f, 0.5f, 0.002f, 0.02f);
}

// The surface's own sound under a foot.
static void st_tex(Buf& b, int surf) {
    float p = b.rnd(0.9f, 1.1f);
    switch (surf) {
        case FOOT_ASPHALT:  // coarse grit crunching under the sole
            grains(b, 0.f, 0.07f, 420.f, 0.3f, 2200.f, 8000.f, 0.0008f, 0.003f, 0.04f);
            noise(b, 0.f, 0.08f, 0.35f, 0.003f, 0.018f, FBP, 3600.f * p, 2400.f, 0.04f, 0.8f);
            break;
        case FOOT_CONCRETE:  // fine dust on smooth concrete
            grains(b, 0.f, 0.05f, 300.f, 0.2f, 3000.f, 9500.f, 0.0006f, 0.002f, 0.03f);
            noise(b, 0.f, 0.06f, 0.3f, 0.002f, 0.012f, FBP, 4500.f * p, 3200.f, 0.03f, 0.8f);
            break;
        case FOOT_GRASS:  // blades brushing and bending, a soft crush underneath
            noise(b, 0.f, 0.18f, 0.7f, 0.01f, 0.05f, FBP, 4200.f * p, 2800.f, 0.08f, 0.6f);
            grains(b, 0.005f, 0.14f, 320.f, 0.3f, 2600.f, 8000.f, 0.001f, 0.005f, 0.07f);
            noise(b, 0.f, 0.1f, 0.4f, 0.004f, 0.025f, FLP, 600.f * p, -1.f, 0.1f, 0.7f);
            break;
        case FOOT_DIRT:  // packed earth and pebbles
            grains(b, 0.f, 0.12f, 800.f, 0.35f, 900.f, 5200.f, 0.001f, 0.005f, 0.06f);
            noise(b, 0.f, 0.1f, 0.5f, 0.002f, 0.025f, FBP, 1600.f * p, 1100.f, 0.05f, 0.7f);
            noise(b, 0.f, 0.06f, 0.4f, 0.002f, 0.015f, FLP, 350.f, -1.f, 0.1f, 0.7f, 2);
            break;
        case FOOT_SAND:  // dry sand: a soft, sliding crunch with grains squeaking
            noise(b, 0.f, 0.22f, 0.8f, 0.02f, 0.06f, FLP, 1800.f * p, 900.f, 0.1f, 0.6f, 1);
            noise(b, 0.01f, 0.16f, 0.35f, 0.012f, 0.04f, FBP, 5200.f * p, -1.f, 0.1f, 0.7f);
            grains(b, 0.01f, 0.12f, 180.f, 0.15f, 2500.f, 9000.f, 0.0008f, 0.003f);
            break;
        case FOOT_WATER: {  // ankle-deep water: a splash, the slosh around the shin and bubbles
            noise(b, 0.f, 0.25f, 0.6f, 0.004f, 0.05f, FBP, 2600.f * p, 1600.f, 0.1f, 0.7f);
            noise(b, 0.f, 0.2f, 0.6f, 0.006f, 0.05f, FLP, 420.f * p, -1.f, 0.1f, 0.7f, 1);
            noise(b, 0.05f, 0.25f, 0.25f, 0.04f, 0.07f, FBP, 900.f * p, 600.f, 0.1f, 0.9f);
            int nb = b.irnd(3, 6);
            for (int j = 0; j < nb; j++) {
                float f0 = b.rnd(500.f, 1300.f);
                tone(b, b.rnd(0.01f, 0.16f), 0.05f, f0, f0 * b.rnd(1.6f, 2.4f), 0.015f, 0.18f, 0.001f, 0.012f);
            }
            break;
        }
        case FOOT_WOOD: {  // boardwalk: the plank flexing over its joists (hollow), a knock, now and then a creak or a
                           // loose board ticking
            Mode m[5] = {{b.rnd(95.f, 130.f), 0.06f, 1.f}, {b.rnd(210.f, 280.f), 0.045f, 0.8f}, {b.rnd(430.f, 560.f), 0.03f, 0.5f},
                         {b.rnd(900.f, 1200.f), 0.018f, 0.3f}, {b.rnd(1900.f, 2400.f), 0.01f, 0.15f}};
            modes(b, 0.f, 0.8f, m, 5);
            noise(b, 0.f, 0.05f, 0.35f, 0.001f, 0.01f, FBP, 1400.f * p, -1.f, 0.1f, 0.9f);
            if (b.chance(0.25f)) fmNote(b, b.rnd(0.02f, 0.05f), 0.2f, b.rnd(380.f, 720.f), 1.004f, 2.5f, 0.08f, 0.06f, 0.03f, 0.05f);
            if (b.chance(0.3f)) grains(b, 0.012f, 0.05f, 140.f, 0.12f, 2800.f, 6000.f, 0.001f, 0.003f);
            break;
        }
        case FOOT_METAL: {  // steel plate / grating: a ringing panel and the rattle in its frame
            Mode m[7] = {{b.rnd(380.f, 520.f), 0.12f, 0.6f},  {b.rnd(900.f, 1200.f), 0.1f, 0.5f},  {b.rnd(1700.f, 2100.f), 0.08f, 0.45f},
                         {b.rnd(2600.f, 3100.f), 0.06f, 0.35f}, {b.rnd(3900.f, 4600.f), 0.04f, 0.3f}, {b.rnd(5600.f, 6500.f), 0.03f, 0.2f},
                         {b.rnd(160.f, 220.f), 0.05f, 0.5f}};
            modes(b, 0.f, 0.6f, m, 7);
            grains(b, 0.004f, 0.08f, 90.f, 0.15f, 1200.f, 4500.f, 0.004f, 0.015f, 0.04f);
            noise(b, 0.f, 0.04f, 0.3f, 0.0003f, 0.006f, FLP, 1000.f, -1.f, 0.1f, 0.7f);
            break;
        }
        default: {  // mud: a squelch under the sole and the suction as it lifts
            noise(b, 0.f, 0.14f, 0.8f, 0.004f, 0.04f, FBP, 500.f * p, 900.f * p, 0.05f, 2.2f);
            noise(b, 0.f, 0.1f, 0.5f, 0.003f, 0.03f, FLP, 300.f, -1.f, 0.1f, 0.7f, 2);
            tone(b, 0.07f, 0.06f, 260.f * p, 700.f * p, 0.02f, 0.25f, 0.004f, 0.02f);
            grains(b, 0.02f, 0.1f, 120.f, 0.1f, 800.f, 2500.f, 0.004f, 0.012f);
            break;
        }
    }
}

// A foot coming down in a puddle on hard ground: spray and droplets.
static void st_puddle(Buf& b) {
    float p = b.rnd(0.9f, 1.1f);
    noise(b, 0.f, 0.14f, 0.8f, 0.0015f, 0.03f, FHP, 2200.f * p, -1.f, 0.1f, 0.7f);
    noise(b, 0.f, 0.1f, 0.5f, 0.001f, 0.02f, FBP, 900.f * p, -1.f, 0.1f, 0.8f);
    int nd = b.irnd(3, 7);
    for (int k = 0; k < nd; k++) {
        float f0 = b.rnd(900.f, 2400.f);
        tone(b, b.rnd(0.01f, 0.15f), 0.04f, f0, f0 * b.rnd(1.5f, 2.2f), 0.01f, 0.12f, 0.0005f, 0.008f);
    }
}

// Clothing per step: fabric rubbing as the thighs and sleeves pass (a denser, brighter swish at a run).
static void st_cloth(Buf& b, bool run) {
    int swishes = run ? 2 : 1;
    for (int k = 0; k < swishes; k++) {
        float t0 = (float)k * b.rnd(0.06f, 0.1f);
        float f = b.rnd(1800.f, 3200.f) * (run ? 1.3f : 1.f);
        noise(b, t0, run ? 0.2f : 0.25f, 0.7f, run ? 0.03f : 0.06f, run ? 0.05f : 0.07f, FBP, f, f * 1.4f, 0.1f, 0.5f);
        grains(b, t0 + 0.01f, run ? 0.12f : 0.16f, run ? 500.f : 300.f, 0.12f, 2500.f, 7000.f, 0.0005f, 0.002f);
    }
}

// Keys, coins and equipment jingling in a pocket or on a belt.
static void st_gear(Buf& b) {
    int n = b.irnd(3, 6);
    for (int k = 0; k < n; k++) {
        float t = b.rnd(0.f, 0.08f);
        Mode m[3] = {{b.rnd(2800.f, 4200.f), b.rnd(0.03f, 0.08f), 1.f}, {b.rnd(5200.f, 7800.f), b.rnd(0.02f, 0.05f), 0.6f},
                     {b.rnd(9000.f, 11000.f), 0.015f, 0.3f}};
        modes(b, t, b.rnd(0.3f, 1.f), m, 3);
    }
    noise(b, 0.f, 0.1f, 0.15f, 0.005f, 0.02f, FBP, 1500.f, -1.f, 0.1f, 0.7f);
}

// Both feet after a jump or a drop: knees absorbing a heavy thump, the soles slapping, grit and clothing.
static void st_land(Buf& b, bool hard) {
    float p = b.rnd(0.9f, 1.1f);
    tone(b, 0.f, 0.14f, 110.f * p, 55.f, 0.03f, 1.f, 0.001f, 0.045f);
    noise(b, 0.f, 0.1f, 0.9f, 0.0008f, 0.02f, FLP, (hard ? 800.f : 450.f) * p, hard ? 350.f : 220.f, 0.04f, 0.8f);
    float t2 = b.rnd(0.008f, 0.03f);
    if (hard) {
        noise(b, t2, 0.07f, 0.6f, 0.0005f, 0.012f, FBP, 1600.f * p, -1.f, 0.1f, 0.9f);
        click(b, 0.f, 0.4f, 5);
        grains(b, 0.005f, 0.1f, 500.f, 0.2f, 2000.f, 8000.f, 0.001f, 0.004f, 0.05f);
    } else {
        noise(b, t2, 0.16f, 0.5f, 0.01f, 0.04f, FBP, 2800.f * p, 1800.f, 0.08f, 0.6f);
        grains(b, 0.005f, 0.14f, 400.f, 0.2f, 1200.f, 6000.f, 0.001f, 0.005f, 0.07f);
    }
    noise(b, 0.02f, 0.2f, 0.25f, 0.02f, 0.06f, FBP, 2600.f, 3400.f, 0.1f, 0.6f);
}

// A sole twisting on the ground (sharp stop or turn): a grit scrape and, often, the rubber chirping.
static void st_scuff(Buf& b) {
    float p = b.rnd(0.9f, 1.1f);
    noise(b, 0.f, 0.16f, 0.7f, 0.01f, 0.04f, FBP, 2800.f * p, 4200.f, 0.06f, 0.9f);
    grains(b, 0.f, 0.12f, 600.f, 0.25f, 2500.f, 9000.f, 0.0006f, 0.002f, 0.08f);
    if (b.chance(0.6f)) fmNote(b, 0.02f, 0.09f, b.rnd(1600.f, 2800.f), 1.f, 1.2f, 0.03f, 0.35f, 0.006f, 0.04f);
}

// A body landing on the ground: the chest cavity's deep thud, the flesh slap of the contact patch, clothing, the
// surface (grit, grass, hollow boards, ringing plate) and a smaller second impact as the limbs and head follow.
static void st_bodyThud(Buf& b, int kind) {
    float p = b.rnd(0.9f, 1.1f);
    tone(b, 0.f, 0.2f, 95.f * p, 55.f, 0.04f, 1.f, 0.002f, 0.06f);
    noise(b, 0.f, 0.14f, 0.9f, 0.001f, 0.035f, FLP, 700.f * p, 300.f, 0.05f, 0.7f, 1);
    if (kind != 1) noise(b, 0.f, 0.05f, 0.6f, 0.0005f, 0.01f, FBP, 1200.f * p, -1.f, 0.1f, 0.8f);
    noise(b, 0.01f, 0.3f, 0.3f, 0.03f, 0.08f, FBP, 2400.f, 3200.f, 0.1f, 0.5f);
    switch (kind) {
        case 0: grains(b, 0.005f, 0.12f, 400.f, 0.15f, 2000.f, 8000.f, 0.001f, 0.004f, 0.06f); break;
        case 1:
            noise(b, 0.f, 0.2f, 0.5f, 0.005f, 0.05f, FBP, 3000.f * p, 2000.f, 0.1f, 0.6f);
            grains(b, 0.005f, 0.15f, 350.f, 0.2f, 1200.f, 6000.f, 0.001f, 0.005f, 0.07f);
            break;
        case 2: {
            Mode m[4] = {{b.rnd(100.f, 130.f), 0.09f, 1.f}, {b.rnd(220.f, 280.f), 0.07f, 0.8f}, {b.rnd(460.f, 560.f), 0.04f, 0.5f},
                         {b.rnd(950.f, 1150.f), 0.02f, 0.3f}};
            modes(b, 0.f, 0.9f, m, 4);
            break;
        }
        default: {
            Mode m[6] = {{b.rnd(300.f, 420.f), 0.35f, 0.7f}, {b.rnd(800.f, 1000.f), 0.28f, 0.5f}, {b.rnd(1500.f, 1800.f), 0.2f, 0.45f},
                         {b.rnd(2400.f, 2900.f), 0.14f, 0.35f}, {b.rnd(3700.f, 4300.f), 0.09f, 0.25f}, {b.rnd(140.f, 190.f), 0.12f, 0.6f}};
            modes(b, 0.f, 0.5f, m, 6);
            grains(b, 0.01f, 0.12f, 80.f, 0.12f, 1000.f, 4000.f, 0.004f, 0.015f, 0.05f);
            break;
        }
    }
    float t2 = b.rnd(0.06f, 0.16f);
    tone(b, t2, 0.1f, 120.f * p, 80.f, 0.02f, 0.35f, 0.001f, 0.03f);
    noise(b, t2, 0.05f, 0.35f, 0.0005f, 0.01f, FBP, 1000.f * p, -1.f, 0.1f, 0.8f);
}

// An arm or leg slapping down.
static void st_bodySlap(Buf& b) {
    float p = b.rnd(0.9f, 1.1f);
    noise(b, 0.f, 0.05f, 0.9f, 0.0004f, 0.009f, FBP, 1300.f * p, 1000.f, 0.02f, 0.9f);
    tone(b, 0.f, 0.05f, 150.f * p, 100.f, 0.01f, 0.4f, 0.0006f, 0.014f);
    noise(b, 0.f, 0.02f, 0.25f, 0.0002f, 0.003f, FHP, 3000.f, -1.f, 0.1f, 0.7f);
    noise(b, 0.01f, 0.12f, 0.15f, 0.01f, 0.03f, FBP, 2600.f, -1.f, 0.1f, 0.6f);
}

// Clothing burst of a vault, climb or dive: two or three rubs and a heavier jacket swish.
static void st_clothBurst(Buf& b) {
    int n = b.irnd(2, 3);
    float t = 0.f;
    for (int k = 0; k < n; k++) {
        float f = b.rnd(1600.f, 3400.f);
        noise(b, t, 0.3f, b.rnd(0.5f, 1.f), b.rnd(0.02f, 0.06f), b.rnd(0.05f, 0.09f), FBP, f, f * b.rnd(0.7f, 1.5f), 0.1f, 0.5f);
        grains(b, t + 0.01f, 0.18f, 400.f, 0.12f, 2500.f, 7500.f, 0.0005f, 0.002f);
        t += b.rnd(0.07f, 0.15f);
    }
    noise(b, 0.f, 0.35f, 0.35f, 0.08f, 0.1f, FLP, 900.f, -1.f, 0.1f, 0.7f, 1);
}

// A palm slapping onto a ledge and gripping it.
static void st_grab(Buf& b) {
    float p = b.rnd(0.9f, 1.1f);
    noise(b, 0.f, 0.04f, 0.9f, 0.0003f, 0.006f, FBP, 1500.f * p, 1100.f, 0.02f, 1.f);
    click(b, 0.f, 0.3f, 3);
    tone(b, 0.f, 0.04f, 220.f * p, 150.f, 0.008f, 0.3f, 0.0005f, 0.01f);
    noise(b, 0.015f, 0.12f, 0.25f, 0.01f, 0.03f, FBP, 3200.f * p, 2400.f, 0.05f, 0.7f);
}

static void s_footLayer(Buf& b, int id) {
    if (id >= STEP_HEEL && id < STEP_TOE) st_heel(b, id - STEP_HEEL);
    else if (id >= STEP_TOE && id < STEP_SOFT) st_toe(b, id - STEP_TOE);
    else if (id == STEP_SOFT) st_soft(b);
    else if (id >= STEP_TEX && id < STEP_PUDDLE) st_tex(b, id - STEP_TEX);
    else if (id == STEP_PUDDLE) st_puddle(b);
    else if (id == STEP_CLOTH_WALK || id == STEP_CLOTH_RUN) st_cloth(b, id == STEP_CLOTH_RUN);
    else if (id == STEP_GEAR) st_gear(b);
    else if (id == STEP_LAND_HARD || id == STEP_LAND_SOFT) st_land(b, id == STEP_LAND_HARD);
    else if (id == STEP_SCUFF) st_scuff(b);
    else if (id >= BODY_THUD_HARD && id <= BODY_THUD_METAL) st_bodyThud(b, id - BODY_THUD_HARD);
    else if (id == BODY_SLAP) st_bodySlap(b);
    else if (id == FOLEY_CLOTH_BURST) st_clothBurst(b);
    else if (id == FOLEY_GRAB_HAND) st_grab(b);
}
