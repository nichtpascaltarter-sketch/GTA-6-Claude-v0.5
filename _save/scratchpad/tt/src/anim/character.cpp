// Character assembly: random population descriptions and the final skinned mesh (body + outfit + hair).
//
// CharacterDesc field conventions (indices used by randomCharacter and the mesh builders):
//   hairStyle : 0 bald, 1 buzz cut, 2 short, 3 curly/afro, 4 ponytail, 5 long straight, 6 bun, 7 braids,
//               8 slicked back, 9 bob, 10 quiff/side part
//   top       : 0 t-shirt, 1 tank top, 2 polo, 3 hawaiian shirt, 4 dress shirt (long sleeves), 5 hoodie,
//               6 suit jacket + shirt + tie, 7 police uniform shirt, 8 medic/EMT shirt, 9 hi-vis vest over tee,
//               10 bikini top, 11 one-piece swimsuit, 12 sundress, 13 blouse, 14 none (shirtless), 15 crop top,
//               16 oversized tee
//   bottom    : 0 jeans, 1 shorts, 2 cargo shorts, 3 slacks, 4 skirt, 5 swim trunks, 6 bikini bottom,
//               7 police trousers (with duty belt), 8 baggy jeans, 9 leggings, 10 work pants, 11 hot pants
//   shoes     : 0 sneakers, 1 leather dress shoes, 2 boots, 3 sandals/flip-flops, 4 barefoot, 5 flats, 6 running shoes
//   hat       : -1 none, 0 baseball cap, 1 backwards cap, 2 police cap, 3 hard hat, 4 sun hat, 5 fedora,
//               6 beanie, 7 bandana
//   glasses   : -1 none, 0 sunglasses, 1 aviators, 2 reading glasses
//   facialHair: -1 none, 0 stubble, 1 mustache, 2 goatee, 3 full beard, 4 short boxed beard
//   role      : 0 civilian, 1 police, 2 gang, 3 business, 4 beach, 5 worker, 6 medic
// Colors are linear RGB albedo. skinTone is written directly to the skin vertex color.
#include "anim_internal.h"

namespace Anim {

namespace detail {

// Compact a mesh by dropping triangles flagged in hide and unreferenced vertices.
static void compactInto(const MeshB& src, const std::vector<u8>& hideTri, MeshB& dst) {
    std::vector<u32> remap(src.v.size(), 0xffffffffu);
    size_t nt = src.idx.size() / 3;
    for (size_t t = 0; t < nt; t++) {
        if (t < hideTri.size() && hideTri[t]) continue;
        for (int k = 0; k < 3; k++) {
            u32 vi = src.idx[t * 3 + k];
            if (remap[vi] == 0xffffffffu) {
                remap[vi] = (u32)dst.v.size();
                dst.v.push_back(src.v[vi]);
            }
            dst.idx.push_back(remap[vi]);
        }
    }
}

// Skin palette (sRGB), light to deep. randomCharacter blends neighbours for a continuous range.
static const float kSkinPal[][3] = {
    {0.97f, 0.83f, 0.73f}, {0.94f, 0.76f, 0.63f}, {0.89f, 0.69f, 0.55f}, {0.83f, 0.62f, 0.47f}, {0.76f, 0.55f, 0.40f},
    {0.67f, 0.47f, 0.33f}, {0.57f, 0.38f, 0.26f}, {0.47f, 0.31f, 0.21f}, {0.38f, 0.24f, 0.16f}, {0.29f, 0.18f, 0.12f},
    {0.22f, 0.14f, 0.10f},
};

static vec3 skinFromT(float t) {
    const int n = (int)(sizeof(kSkinPal) / sizeof(kSkinPal[0]));
    t = Saturate(t) * (n - 1);
    int i = Min((int)t, n - 2);
    float f = t - i;
    vec3 a(kSkinPal[i][0], kSkinPal[i][1], kSkinPal[i][2]), b(kSkinPal[i + 1][0], kSkinPal[i + 1][1], kSkinPal[i + 1][2]);
    return srgbToLinear(lerp(a, b, f));
}

static vec3 pickColor(Rng& r, const float (*pal)[3], int n) {
    int i = (int)(r.next() % (u32)n);
    vec3 c(pal[i][0], pal[i][1], pal[i][2]);
    // slight variation in value/saturation
    float v = r.range(0.9f, 1.08f);
    return srgbToLinear(saturate(c * v));
}

// Clothing palettes (sRGB)
static const float kCasualTop[][3] = {
    {0.95f, 0.95f, 0.93f}, {0.12f, 0.12f, 0.13f}, {0.85f, 0.2f, 0.2f}, {0.2f, 0.35f, 0.7f}, {0.95f, 0.75f, 0.2f}, {0.2f, 0.6f, 0.55f},
    {0.95f, 0.5f, 0.6f}, {0.55f, 0.55f, 0.58f}, {0.3f, 0.45f, 0.25f}, {0.98f, 0.6f, 0.3f}, {0.5f, 0.75f, 0.9f}, {0.6f, 0.25f, 0.5f},
    {0.9f, 0.88f, 0.75f}, {0.15f, 0.2f, 0.35f}, {0.1f, 0.65f, 0.75f}, {0.8f, 0.4f, 0.15f},
};
static const float kDenim[][3] = {{1.f, 1.f, 1.f}, {0.8f, 0.85f, 0.95f}, {0.55f, 0.55f, 0.6f}, {0.35f, 0.33f, 0.35f}, {1.1f, 1.1f, 1.15f}};
static const float kPants[][3] = {
    {0.2f, 0.2f, 0.22f}, {0.75f, 0.68f, 0.52f}, {0.35f, 0.4f, 0.3f}, {0.15f, 0.18f, 0.28f}, {0.55f, 0.55f, 0.55f}, {0.9f, 0.9f, 0.88f},
    {0.45f, 0.35f, 0.25f}, {0.6f, 0.2f, 0.2f},
};
static const float kShorts[][3] = {
    {0.75f, 0.68f, 0.52f}, {0.2f, 0.25f, 0.4f}, {0.35f, 0.4f, 0.3f}, {0.9f, 0.9f, 0.88f}, {0.15f, 0.15f, 0.16f}, {0.8f, 0.35f, 0.3f},
    {0.3f, 0.55f, 0.7f}, {0.55f, 0.5f, 0.45f},
};
static const float kSwim[][3] = {
    {0.9f, 0.2f, 0.25f}, {0.1f, 0.1f, 0.12f}, {0.1f, 0.5f, 0.8f}, {0.95f, 0.6f, 0.1f}, {0.2f, 0.7f, 0.5f}, {0.95f, 0.4f, 0.65f},
    {0.95f, 0.95f, 0.9f}, {0.4f, 0.2f, 0.6f}, {0.1f, 0.75f, 0.85f},
};
static const float kSuit[][3] = {{0.08f, 0.08f, 0.1f}, {0.15f, 0.17f, 0.25f}, {0.3f, 0.3f, 0.32f}, {0.55f, 0.5f, 0.42f}, {0.85f, 0.83f, 0.78f}};
static const float kShoe[][3] = {{0.95f, 0.95f, 0.95f}, {0.1f, 0.1f, 0.1f}, {0.8f, 0.2f, 0.2f}, {0.2f, 0.3f, 0.6f}, {0.6f, 0.6f, 0.6f}, {0.9f, 0.85f, 0.7f}};
static const float kGang[][3] = {{0.75f, 0.1f, 0.12f}, {0.1f, 0.25f, 0.75f}, {0.1f, 0.55f, 0.25f}, {0.45f, 0.15f, 0.6f}, {0.95f, 0.75f, 0.1f}};
static const float kHair[][3] = {
    {0.03f, 0.025f, 0.022f}, {0.07f, 0.045f, 0.03f}, {0.13f, 0.08f, 0.045f}, {0.25f, 0.15f, 0.08f}, {0.4f, 0.26f, 0.13f},
    {0.62f, 0.45f, 0.25f}, {0.82f, 0.68f, 0.42f}, {0.45f, 0.18f, 0.08f},
};

}  // namespace detail

CharacterDesc randomCharacter(u32 seed, int role) {
    using namespace detail;
    CharacterDesc d;
    Rng r(hash32(seed ^ 0xA511E9B3u) + 1u, (u64)hash32(seed + 0x68E31DA4u) | 1u);
    d.seed = seed;
    d.role = Clamp(role, 0, 6);
    // gender: police/worker skew male, beach/business balanced
    float femP = 0.5f;
    if (d.role == 1) femP = 0.3f;
    if (d.role == 2) femP = 0.2f;
    if (d.role == 5) femP = 0.12f;
    if (d.role == 6) femP = 0.4f;
    d.gender = r.chance(femP) ? FEMALE : MALE;
    bool fem = d.gender == FEMALE;
    // age: 18..80 (normalized 0..1 maps to 18..80), working roles narrower
    float ageYears;
    if (d.role == 0 || d.role == 4) ageYears = 18.f + 62.f * powf(r.f(), 1.35f);
    else if (d.role == 2) ageYears = r.range(18.f, 38.f);
    else ageYears = r.range(22.f, 60.f);
    d.age = (ageYears - 18.f) / 62.f;
    // height: normal distribution by gender
    d.height = fem ? 1.635f + 0.07f * r.gauss() : 1.765f + 0.075f * r.gauss();
    d.height = Clamp(d.height, fem ? 1.48f : 1.58f, fem ? 1.84f : 2.0f);
    d.height -= 0.03f * sstep(0.7f, 1.f, d.age);
    // body type
    float wt = 0.42f + 0.2f * r.gauss() + 0.12f * d.age;
    if (d.role == 4) wt -= 0.05f;
    if (d.role == 1 || d.role == 5) wt += 0.05f;
    d.weight = Clamp(wt, 0.05f, 1.f);
    float mu = (fem ? 0.28f : 0.45f) + 0.18f * r.gauss() - 0.15f * d.age;
    if (d.role == 1 || d.role == 5 || d.role == 2) mu += 0.12f;
    d.muscle = Clamp(mu, 0.f, 1.f);
    // skin tone: region mix (Latino, Black/Caribbean, White, Asian, mixed) -> continuous palette position
    float g = r.f();
    float st;
    if (g < 0.4f) st = r.range(0.2f, 0.62f);         // Latino / Mediterranean
    else if (g < 0.64f) st = r.range(0.52f, 1.0f);   // Black / Afro-Caribbean
    else if (g < 0.86f) st = r.range(0.0f, 0.32f);   // White
    else if (g < 0.95f) st = r.range(0.12f, 0.42f);  // Asian
    else st = r.range(0.2f, 0.85f);                  // mixed / other
    d.skinTone = skinFromT(st);
    bool darkSkin = st > 0.55f;
    // hair color
    int hc;
    if (st > 0.45f) hc = r.chance(0.85f) ? 0 : 1;
    else if (st > 0.22f) hc = r.chance(0.6f) ? 0 : (r.chance(0.7f) ? 1 : 2);
    else hc = r.irange(0, 7);
    vec3 hcol(kHair[hc][0], kHair[hc][1], kHair[hc][2]);
    float gray = sstep(0.35f, 0.85f, d.age) * r.range(0.4f, 1.f);
    hcol = lerp(hcol, vec3(0.55f, 0.54f, 0.52f), gray * 0.85f);
    if (!fem && r.chance(0.05f)) hcol = vec3(0.7f, 0.66f, 0.55f);   // bleached
    d.hairColor = srgbToLinear(hcol);
    // hair style
    if (fem) {
        const float w[] = {0.01f, 0.02f, 0.08f, darkSkin ? 0.2f : 0.08f, 0.18f, 0.25f, 0.1f, darkSkin ? 0.18f : 0.03f, 0.03f, 0.1f, 0.02f};
        d.hairStyle = r.weighted(w, 11);
    } else {
        float bald = 0.03f + 0.35f * sstep(0.3f, 0.9f, d.age);
        const float w[] = {bald, 0.2f, 0.26f, darkSkin ? 0.12f : 0.04f, 0.02f, 0.02f, 0.005f, darkSkin ? 0.07f : 0.01f, 0.08f, 0.01f, 0.14f};
        d.hairStyle = r.weighted(w, 11);
    }
    // facial hair
    d.facialHair = -1;
    if (!fem && d.age > 0.02f) {
        float fh = r.f();
        if (fh < 0.18f) d.facialHair = FH_STUBBLE;
        else if (fh < 0.24f) d.facialHair = FH_MUSTACHE;
        else if (fh < 0.34f) d.facialHair = FH_GOATEE;
        else if (fh < 0.42f) d.facialHair = FH_BEARD;
        else if (fh < 0.52f) d.facialHair = FH_SHORTBEARD;
    }
    // ---- outfit by role
    d.hat = -1;
    d.glasses = -1;
    auto casualTopColor = [&]() { return pickColor(r, kCasualTop, (int)(sizeof(kCasualTop) / sizeof(kCasualTop[0]))); };
    switch (d.role) {
        case 1: {   // police
            d.top = TOP_POLICE;
            d.bottom = BOT_POLICE;
            d.shoes = r.chance(0.6f) ? SHOE_BOOT : SHOE_DRESS;
            d.topColor = srgbToLinear(vec3(0.12f, 0.16f, 0.3f));
            d.bottomColor = srgbToLinear(vec3(0.1f, 0.12f, 0.2f));
            d.shoeColor = srgbToLinear(vec3(0.06f));
            d.hat = r.chance(0.55f) ? HAT_POLICE : -1;
            if (r.chance(0.35f)) d.glasses = GL_AVIATOR;
            if (d.facialHair > FH_MUSTACHE) d.facialHair = r.chance(0.5f) ? FH_MUSTACHE : -1;
            if (fem && (d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_CURLY)) d.hairStyle = HAIR_BUN;
            break;
        }
        case 2: {   // gang
            vec3 gc = pickColor(r, kGang, 5);
            float t = r.f();
            d.top = t < 0.4f ? TOP_TANK : (t < 0.8f ? TOP_OVERSIZED : TOP_HOODIE);
            d.topColor = r.chance(0.5f) ? gc : srgbToLinear(r.chance(0.5f) ? vec3(0.95f) : vec3(0.1f));
            d.bottom = r.chance(0.6f) ? BOT_BAGGY : BOT_SHORTS;
            d.bottomColor = d.bottom == BOT_BAGGY ? pickColor(r, kDenim, 5) : srgbToLinear(r.chance(0.5f) ? vec3(0.12f) : vec3(0.7f, 0.64f, 0.5f));
            d.shoes = SHOE_SNEAKER;
            d.shoeColor = r.chance(0.5f) ? srgbToLinear(vec3(0.95f)) : gc;
            float hh = r.f();
            if (hh < 0.3f) d.hat = HAT_CAP_BACK;
            else if (hh < 0.45f) d.hat = HAT_BANDANA;
            else if (hh < 0.55f) d.hat = HAT_BEANIE;
            if (r.chance(0.35f)) d.glasses = GL_SUN;
            if (d.hat >= 0 && (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_BUN)) d.hairStyle = HAIR_BRAIDS;
            break;
        }
        case 3: {   // business
            if (fem && r.chance(0.5f)) {
                d.top = TOP_BLOUSE;
                d.topColor = pickColor(r, kCasualTop, 16);
                d.bottom = r.chance(0.55f) ? BOT_SKIRT : BOT_SLACKS;
                d.bottomColor = pickColor(r, kSuit, 4);
                d.shoes = SHOE_FLATS;
            } else {
                d.top = TOP_SUIT;
                d.topColor = pickColor(r, kSuit, 5);
                d.bottom = BOT_SLACKS;
                d.bottomColor = d.topColor;
                d.shoes = SHOE_DRESS;
            }
            d.shoeColor = srgbToLinear(r.chance(0.6f) ? vec3(0.06f) : vec3(0.35f, 0.2f, 0.1f));
            if (r.chance(0.12f)) d.glasses = GL_READING;
            if (d.hairStyle == HAIR_CURLY && !fem) d.hairStyle = HAIR_SHORT;
            break;
        }
        case 4: {   // beach
            if (fem) {
                d.top = r.chance(0.7f) ? TOP_BIKINI : TOP_ONEPIECE;
                d.bottom = d.top == TOP_BIKINI ? BOT_BIKINI : BOT_BIKINI;
                d.topColor = pickColor(r, kSwim, 9);
                d.bottomColor = r.chance(0.7f) ? d.topColor : pickColor(r, kSwim, 9);
            } else {
                d.top = r.chance(0.8f) ? TOP_NONE : TOP_TANK;
                d.topColor = casualTopColor();
                d.bottom = BOT_TRUNKS;
                d.bottomColor = pickColor(r, kSwim, 9);
            }
            d.shoes = r.chance(0.6f) ? SHOE_SANDAL : SHOE_BARE;
            d.shoeColor = pickColor(r, kShoe, 6);
            if (r.chance(0.5f)) d.glasses = GL_SUN;
            float hh = r.f();
            if (hh < 0.2f) d.hat = HAT_SUNHAT;
            else if (hh < 0.35f) d.hat = HAT_CAP;
            if (d.hat >= 0 && (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_BUN)) d.hat = -1;
            break;
        }
        case 5: {   // construction worker
            d.top = TOP_HIVIS;
            d.topColor = casualTopColor();
            d.bottom = r.chance(0.5f) ? BOT_JEANS : BOT_WORK;
            d.bottomColor = d.bottom == BOT_JEANS ? pickColor(r, kDenim, 5) : srgbToLinear(vec3(0.35f, 0.3f, 0.22f));
            d.shoes = SHOE_BOOT;
            d.shoeColor = srgbToLinear(vec3(0.4f, 0.26f, 0.12f));
            d.hat = r.chance(0.8f) ? HAT_HARDHAT : HAT_CAP;
            if (r.chance(0.3f)) d.glasses = GL_SUN;
            if (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_BUN) d.hairStyle = fem ? HAIR_PONYTAIL : HAIR_SHORT;
            break;
        }
        case 6: {   // medic
            d.top = TOP_MEDIC;
            d.topColor = srgbToLinear(r.chance(0.5f) ? vec3(0.2f, 0.28f, 0.45f) : vec3(0.9f, 0.9f, 0.88f));
            d.bottom = BOT_WORK;
            d.bottomColor = srgbToLinear(vec3(0.12f, 0.14f, 0.22f));
            d.shoes = SHOE_BOOT;
            d.shoeColor = srgbToLinear(vec3(0.06f));
            if (fem && (d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_CURLY)) d.hairStyle = HAIR_PONYTAIL;
            break;
        }
        default: {  // civilian, hot coastal city
            float t = r.f();
            if (fem) {
                if (t < 0.2f) d.top = TOP_SUNDRESS;
                else if (t < 0.45f) d.top = TOP_TSHIRT;
                else if (t < 0.65f) d.top = TOP_TANK;
                else if (t < 0.75f) d.top = TOP_CROP;
                else if (t < 0.87f) d.top = TOP_BLOUSE;
                else d.top = TOP_POLO;
            } else {
                if (t < 0.36f) d.top = TOP_TSHIRT;
                else if (t < 0.52f) d.top = TOP_POLO;
                else if (t < 0.66f) d.top = TOP_HAWAIIAN;
                else if (t < 0.78f) d.top = TOP_TANK;
                else if (t < 0.88f) d.top = TOP_DRESS_SHIRT;
                else if (t < 0.94f) d.top = TOP_HOODIE;
                else d.top = TOP_OVERSIZED;
            }
            d.topColor = casualTopColor();
            float b = r.f();
            if (d.top == TOP_SUNDRESS) d.bottom = BOT_SKIRT;
            else if (fem) {
                if (b < 0.3f) d.bottom = BOT_JEANS;
                else if (b < 0.5f) d.bottom = BOT_SHORTS;
                else if (b < 0.62f) d.bottom = BOT_HOTPANTS;
                else if (b < 0.8f) d.bottom = BOT_SKIRT;
                else d.bottom = BOT_LEGGINGS;
            } else {
                if (b < 0.35f) d.bottom = BOT_JEANS;
                else if (b < 0.62f) d.bottom = BOT_SHORTS;
                else if (b < 0.8f) d.bottom = BOT_CARGO;
                else d.bottom = BOT_SLACKS;
            }
            if (d.bottom == BOT_JEANS) d.bottomColor = pickColor(r, kDenim, 5);
            else if (d.bottom == BOT_SHORTS || d.bottom == BOT_CARGO || d.bottom == BOT_HOTPANTS) d.bottomColor = pickColor(r, kShorts, 8);
            else if (d.bottom == BOT_SKIRT) d.bottomColor = d.top == TOP_SUNDRESS ? d.topColor : pickColor(r, kCasualTop, 16);
            else d.bottomColor = pickColor(r, kPants, 8);
            float s = r.f();
            if (d.bottom == BOT_SLACKS) d.shoes = SHOE_DRESS;
            else if (s < 0.45f) d.shoes = SHOE_SNEAKER;
            else if (s < 0.75f) d.shoes = SHOE_SANDAL;
            else if (s < 0.88f) d.shoes = fem ? SHOE_FLATS : SHOE_RUNNER;
            else d.shoes = SHOE_RUNNER;
            d.shoeColor = pickColor(r, kShoe, 6);
            if (d.shoes == SHOE_DRESS) d.shoeColor = srgbToLinear(r.chance(0.5f) ? vec3(0.06f) : vec3(0.35f, 0.2f, 0.1f));
            if (r.chance(0.3f)) d.glasses = r.chance(0.8f) ? GL_SUN : GL_READING;
            float hh = r.f();
            if (hh < 0.16f) d.hat = HAT_CAP;
            else if (hh < 0.2f) d.hat = HAT_CAP_BACK;
            else if (hh < 0.25f) d.hat = HAT_SUNHAT;
            else if (hh < 0.28f && !fem) d.hat = HAT_FEDORA;
            if (d.hat >= 0 && (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_BUN)) d.hat = -1;
            if (d.age > 0.7f && d.glasses < 0 && r.chance(0.4f)) d.glasses = GL_READING;
            break;
        }
    }
    return d;
}

void buildCharacterMesh(const CharacterDesc& d, const Skeleton& skel, SkinnedMeshData& out) {
    using namespace detail;
    BodyDims D;
    computeDims(d, D);
    BuildCtx c;
    c.d = &d;
    c.D = &D;
    c.sk = &skel;
    c.skin = saturate(d.skinTone);
    float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
    c.lipCol = lerp(mulColor(c.skin, vec3(0.78f, 0.52f, 0.56f)), mulColor(c.skin, vec3(0.92f, 0.68f, 0.74f)), sstep(0.02f, 0.4f, lum));
    vec3 palmTarget = vec3(0.52f, 0.33f, 0.24f);
    c.palmCol = lerp(c.skin, vmax(c.skin, palmTarget), 0.7f);
    buildBody(c);
    MeshB extra;
    std::vector<u8> hide(c.m.idx.size() / 3, 0);
    buildOutfit(c, extra, hide);
    MeshB fin;
    compactInto(c.m, hide, fin);
    fin.append(extra);
    fixUvSeams(fin);
    emitMesh(fin, out);
}

}  // namespace Anim
