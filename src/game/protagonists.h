// The two protagonists' character descriptions: one source of truth for the roster (assets.cpp) and the character
// viewer (--viewer characters --protagonists). The face, hair and skin come from the seeded civilian draw with the
// roster's deterministic gender re-roll; build, colours and the outfit are fixed.
#pragma once
#ifdef HAVE_CHARACTERS
namespace Game {

inline Anim::CharacterDesc protagonistDesc(int who) {
    using namespace Anim::detail;
    u32 seed = who == 0 ? 0xA11CEu : 0xDE7u;
    const int gender = who == 0 ? 1 : 0;
    Anim::CharacterDesc d = Anim::randomCharacter(seed, 0);
    // re-roll the seed deterministically until the requested gender is produced (as the roster does for everyone)
    for (int k = 0; k < 24 && (int)d.gender != gender; k++) {
        seed = hash32(seed + 0x9e37u);
        d = Anim::randomCharacter(seed, 0);
    }
    if (who == 0) {   // Mari: late 20s, athletic, dark wavy hair, casual street style
        d.gender = Anim::FEMALE;
        d.height = 1.68f;
        d.weight = 0.35f;
        d.muscle = 0.5f;
        d.age = 0.15f;
        d.skinTone = vec3(0.62f, 0.44f, 0.33f);
        d.hairColor = vec3(0.06f, 0.04f, 0.03f);
        d.topColor = vec3(0.85f, 0.2f, 0.45f);
        d.bottomColor = vec3(0.12f, 0.14f, 0.2f);
        d.shoeColor = vec3(0.9f, 0.9f, 0.88f);
        // her look is fixed, not drawn by the civilian generator: tank top, jeans, flats, a watch and earrings
        d.top = TOP_TANK;
        d.bottom = BOT_JEANS;
        d.shoes = SHOE_FLATS;
        d.outer = -1;
        d.bag = -1;
        d.extras = ACC_EXPLICIT | ACC_WATCH | ACC_EARRINGS;
    } else {          // Dex: early 30s, broad, short hair, stubble, work jacket
        d.gender = Anim::MALE;
        d.height = 1.84f;
        d.weight = 0.55f;
        d.muscle = 0.7f;
        d.age = 0.25f;
        d.skinTone = vec3(0.72f, 0.56f, 0.45f);
        d.hairColor = vec3(0.2f, 0.13f, 0.08f);
        d.bottomColor = vec3(0.16f, 0.18f, 0.24f);
        d.shoeColor = vec3(0.25f, 0.17f, 0.1f);
        // his look is fixed: a grey tee under an open olive work jacket, jeans, brown work boots, a watch
        d.top = TOP_TSHIRT;
        d.topColor = vec3(0.3f, 0.3f, 0.32f);
        d.outer = OUT_JACKET;
        d.outerColor = vec3(0.24f, 0.3f, 0.22f);
        d.bottom = BOT_JEANS;
        d.shoes = SHOE_BOOT;
        d.bag = -1;
        d.extras = ACC_EXPLICIT | ACC_WATCH;
        d.facialHair = FH_STUBBLE;   // a few days' growth (the description above)
    }
    return d;
}

}  // namespace Game
#endif
