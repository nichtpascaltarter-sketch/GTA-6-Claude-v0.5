// Architecture of the ordinary blocks (included by buildings.cpp). After the lots, the site buildings and the enterable
// interiors are settled, every ordinary building gets an archetype from its district's palette (BuildingArch): a 1920s
// two-storey shop block, a MiMo garden-apartment court, Mediterranean-revival apartments under barrel tile, a 1970s
// balcony slab on Collins, a ranch house in Westbrook, a conch house on the keys. The archetype sets the floors, the
// massing carved out of the envelope (buildmesh.cpp), the roof form and the facade record (wall material, colours,
// window rhythm), and the street-level details read it (facadedetail.cpp).
//
// What gameplay reads stays as it was: the envelope (c, ax, hx, hy, front, the lot), BuildingStyle, the seed rules
// (house garage wings) and every building that hosts an interior (those keep the plain generator), so shops, homes,
// interiors, story frontages, spawns and parking see the same city. Heights change only on buildings without an
// interior, after the interiors were planned.
//
// Block faces get a character: each street side favours one archetype of the palette and avoids another, a building
// rarely repeats its neighbour's archetype, and corner lots get corner types (bodegas, corner towers, chamfers).
#include "buildings.h"

namespace World {

namespace blockstyle {

using namespace buildings_detail;

// ------------------------------------------------------------------------------------------------ colour palettes
// Tints multiplied onto the wall layer (the facade shader scales by 1.6 over a ~0.6 albedo layer, so 1.0 reads white)
const vec3 kDecoPastel[] = {vec3(0.72f, 0.95f, 0.86f), vec3(1.0f, 0.8f, 0.86f), vec3(0.86f, 0.8f, 1.0f), vec3(1.0f, 0.95f, 0.7f), vec3(0.72f, 0.88f, 1.0f),
                            vec3(1.0f, 0.84f, 0.72f), vec3(0.98f, 0.98f, 0.95f), vec3(0.7f, 0.95f, 0.95f)};
const vec3 kMimoWall[] = {vec3(0.99f, 0.99f, 0.97f), vec3(0.96f, 0.95f, 0.9f), vec3(0.93f, 0.95f, 0.96f), vec3(1.0f, 0.97f, 0.9f), vec3(0.9f, 0.96f, 0.95f)};
const vec3 kMimoAccent[] = {vec3(0.2f, 0.72f, 0.75f), vec3(0.95f, 0.45f, 0.35f), vec3(0.95f, 0.75f, 0.2f), vec3(0.35f, 0.75f, 0.6f), vec3(0.3f, 0.5f, 0.85f),
                            vec3(0.95f, 0.55f, 0.6f), vec3(0.5f, 0.75f, 0.9f)};
const vec3 kMedWarm[] = {vec3(1.0f, 0.92f, 0.76f), vec3(0.98f, 0.84f, 0.62f), vec3(0.95f, 0.75f, 0.55f), vec3(1.0f, 0.88f, 0.72f), vec3(0.93f, 0.7f, 0.56f),
                         vec3(1.0f, 0.95f, 0.85f), vec3(0.9f, 0.8f, 0.62f), vec3(0.98f, 0.78f, 0.68f)};
const vec3 kOcho[] = {vec3(1.0f, 0.6f, 0.25f), vec3(0.25f, 0.75f, 0.72f), vec3(0.98f, 0.82f, 0.25f), vec3(0.6f, 0.85f, 0.35f), vec3(0.95f, 0.4f, 0.45f),
                      vec3(0.55f, 0.5f, 0.85f), vec3(0.35f, 0.6f, 0.95f), vec3(1.0f, 0.7f, 0.55f), vec3(0.75f, 0.9f, 0.5f)};
const vec3 kModernWall[] = {vec3(0.97f, 0.97f, 0.96f), vec3(0.85f, 0.85f, 0.84f), vec3(0.62f, 0.63f, 0.64f), vec3(0.45f, 0.46f, 0.48f), vec3(0.92f, 0.9f, 0.86f)};
const vec3 kSuburb[] = {vec3(1.0f, 0.95f, 0.85f), vec3(0.98f, 0.92f, 0.78f), vec3(1.0f, 1.0f, 0.97f), vec3(0.9f, 0.93f, 0.95f), vec3(0.9f, 0.95f, 0.85f),
                        vec3(1.0f, 0.88f, 0.8f), vec3(0.95f, 0.9f, 0.82f), vec3(0.85f, 0.9f, 0.98f), vec3(1.0f, 0.97f, 0.82f)};
// sixties and seventies ranch houses (Westbrook, the Grove): stucco and painted block in white and cream, butter, peach,
// coral, sage, mint, sky, grey-blue, warm grey, tan and lilac
const vec3 kRanch[] = {vec3(0.99f, 0.99f, 0.97f), vec3(1.0f, 0.95f, 0.84f), vec3(1.0f, 0.93f, 0.72f), vec3(1.0f, 0.85f, 0.72f), vec3(0.98f, 0.76f, 0.66f),
                       vec3(0.82f, 0.9f, 0.76f), vec3(0.78f, 0.92f, 0.86f), vec3(0.78f, 0.88f, 0.98f), vec3(0.76f, 0.8f, 0.86f), vec3(0.86f, 0.83f, 0.78f),
                       vec3(0.93f, 0.84f, 0.68f), vec3(0.9f, 0.85f, 0.95f)};
const vec3 kConch[] = {vec3(0.72f, 0.95f, 0.9f), vec3(1.0f, 0.85f, 0.88f), vec3(0.75f, 0.85f, 1.0f), vec3(1.0f, 0.97f, 0.75f), vec3(1.0f, 1.0f, 1.0f),
                       vec3(0.85f, 0.95f, 0.75f), vec3(1.0f, 0.82f, 0.7f)};
// fifties block houses: mint, butter, salmon, sky, seafoam, peach, lilac, sand, white
const vec3 kCbs[] = {vec3(0.74f, 0.92f, 0.8f), vec3(1.0f, 0.94f, 0.66f), vec3(1.0f, 0.78f, 0.68f), vec3(0.72f, 0.87f, 0.98f), vec3(0.62f, 0.88f, 0.82f),
                     vec3(1.0f, 0.84f, 0.66f), vec3(0.87f, 0.82f, 0.95f), vec3(0.94f, 0.88f, 0.74f), vec3(0.98f, 0.98f, 0.96f)};
// folk Victorian paint: white, cream, butter, sage, porch blue, blush, grey
const vec3 kVictorian[] = {vec3(0.98f, 0.98f, 0.96f), vec3(1.0f, 0.95f, 0.84f), vec3(1.0f, 0.92f, 0.68f), vec3(0.78f, 0.86f, 0.72f), vec3(0.76f, 0.86f, 0.94f),
                           vec3(1.0f, 0.86f, 0.84f), vec3(0.82f, 0.83f, 0.84f)};
const vec3 kPaintedBrick[] = {vec3(0.92f, 0.88f, 0.8f), vec3(0.75f, 0.78f, 0.74f), vec3(0.6f, 0.68f, 0.6f), vec3(0.8f, 0.7f, 0.6f), vec3(0.55f, 0.6f, 0.68f)};
const vec3 kIndustrialWall[] = {vec3(0.82f, 0.82f, 0.8f), vec3(0.7f, 0.72f, 0.72f), vec3(0.62f, 0.68f, 0.74f), vec3(0.7f, 0.74f, 0.66f), vec3(0.85f, 0.8f, 0.7f),
                                vec3(0.55f, 0.56f, 0.58f)};
const vec3 kTileRoof[] = {vec3(1.0f, 1.0f, 1.0f), vec3(0.92f, 0.88f, 0.82f), vec3(1.05f, 0.95f, 0.85f), vec3(0.85f, 0.8f, 0.78f), vec3(1.1f, 1.02f, 0.9f)};
const vec3 kShingle[] = {vec3(0.55f, 0.55f, 0.56f), vec3(0.75f, 0.68f, 0.6f), vec3(0.5f, 0.45f, 0.42f), vec3(0.85f, 0.85f, 0.83f), vec3(0.62f, 0.58f, 0.5f),
                         vec3(0.45f, 0.5f, 0.55f)};
const vec3 kMetalRoof[] = {vec3(0.92f, 0.93f, 0.95f), vec3(0.6f, 0.3f, 0.25f), vec3(0.4f, 0.55f, 0.5f), vec3(0.75f, 0.76f, 0.78f), vec3(0.35f, 0.45f, 0.6f)};

template <size_t N> vec3 pick(const vec3 (&p)[N], Rng& r) { return p[r.next() % N]; }
inline vec3 jitter(vec3 c, Rng& r, float a = 0.03f) {
    return vmin(c * r.range(0.93f, 1.03f) + vec3(r.range(-a, a), r.range(-a, a), r.range(-a, a)), vec3(1.05f));
}

// ------------------------------------------------------------------------------------------------ archetype table
struct ArchInfo {
    const char* name;
    int minF, maxF;
    bool old;          // older fabric: window AC units, gates, water tanks, fire escapes, painted signs
};
const ArchInfo kArch[AR_COUNT] = {
    {"plain", 1, 1, false},
    {"taxpayer", 1, 1, true},      {"1920s block", 2, 3, true},   {"med revival", 1, 2, true},   {"mimo shop", 1, 2, false},
    {"bodega", 1, 2, true},        {"arcade", 2, 3, false},       {"gallery", 2, 2, true},
    {"walk-up", 3, 6, true},       {"mimo apts", 2, 4, false},    {"med apts", 3, 5, false},     {"office 60s", 5, 12, false},
    {"modern", 5, 14, false},      {"loft", 4, 7, true},
    {"condo slab", 12, 30, false}, {"glass condo", 14, 36, false}, {"mimo hotel", 8, 17, false}, {"podium condo", 16, 40, false},
    {"ranch", 1, 1, false},        {"bungalow", 1, 1, false},     {"med house", 1, 2, false},    {"two-storey", 2, 2, false},
    {"mimo house", 1, 1, false},   {"split level", 1, 2, false},  {"conch", 1, 2, false},        {"modern villa", 2, 3, false},
    {"streamline", 2, 4, false},   {"med hotel", 2, 4, false},    {"mimo motel", 2, 5, false},
    {"sawtooth", 1, 1, true},      {"ware office", 1, 1, false},  {"body shop", 1, 1, true},
    {"back house", 1, 2, false},   {"garage row", 1, 1, true},    {"shed", 1, 1, true},
    {"med villa", 2, 3, false},    {"colonial villa", 2, 2, false}, {"stilt house", 1, 1, true},
    {"mimo motor court", 2, 2, false}, {"med motel", 2, 2, false}, {"keys motel", 2, 2, true}, {"motor inn", 2, 2, false},
    {"mission strip", 1, 1, false}, {"mimo strip", 1, 1, false}, {"power center", 1, 1, false},
    {"gambrel barn", 1, 1, true},  {"pole barn", 1, 1, true},     {"gable barn", 1, 1, true},
    {"mission church", 1, 1, false}, {"board church", 1, 1, false}, {"brick church", 1, 1, true}, {"a-frame church", 1, 1, false},
    {"cbs house", 1, 1, false},    {"folk victorian", 2, 2, true}, {"raised keys house", 1, 2, false},
    {"mobile home", 1, 1, true},
};
static_assert(sizeof(kArch) / sizeof(kArch[0]) == AR_COUNT, "one ArchInfo per archetype");

// House forms inside an archetype, seeded from the envelope (massing.cpp builds them, facadedetail.cpp and the repetition
// log read them; the same answer in every LOD): the bungalow's Craftsman variants - side-gabled with the porch under the
// main roof and a dormer, hipped with a hipped dormer, the "airplane" with a small upper room astride the ridge - the
// city's Mission bungalow (stucco, a flat roof behind a parapet with a curved gable over the front, a loggia porch), and
// the two-storey house's American Foursquare (a square block, a pyramid hip with a dormer, a porch across the front).
enum HouseForm : int { HF_PLAIN = 0, HF_BUNG_SIDE, HF_BUNG_HIP, HF_BUNG_AIRPLANE, HF_FOURSQUARE, HF_BUNG_MISSION, HF_COUNT };
inline int houseForm(const Building& b) {
    if (b.style != BS_HOUSE || b.interior >= 0 || b.massing != MK_BOX) return HF_PLAIN;
    const float p = hashToFloat(hash32(b.seed ^ 0xF0F1Au));
    const bool town = b.region == REG_LAKE_TOWN || b.region == REG_HARLOW || b.region == REG_FORT_CASTELL || b.region == REG_REDLAND || b.region == REG_FARMLAND;
    const bool city = b.region == REG_NORTH_CITY || b.region == REG_CALLE_LUNA || b.region == REG_FLATS || b.region == REG_MIDTOWN;
    if (b.arch == AR_HOUSE_BUNGALOW) {
        // (side-gabled only where the house is about as wide as deep or wider; the upper room wants a roomy box; the Mission
        // bungalows in the city's old neighbourhoods and a few in the suburbs)
        const bool sub = b.region == REG_GROVE || b.region == REG_SUBURBS;
        const float pSide = b.hx >= b.hy * 0.85f && b.hx >= 3.6f ? (town ? 0.32f : (city ? 0.18f : 0.24f)) : 0.f;
        const float pHip = town ? 0.22f : (city ? 0.2f : 0.24f);
        const float pAir = b.hx >= 4.2f && b.hy >= 4.2f ? (town ? 0.16f : (city ? 0.06f : 0.1f)) : 0.f;
        const float pMission = b.hx >= 3.4f ? (city ? 0.28f : (sub ? 0.14f : 0.f)) : 0.f;
        if (p < pSide) return HF_BUNG_SIDE;
        if (p < pSide + pHip) return HF_BUNG_HIP;
        if (p < pSide + pHip + pAir) return HF_BUNG_AIRPLANE;
        if (p < pSide + pHip + pAir + pMission) return HF_BUNG_MISSION;
        return HF_PLAIN;
    }
    if (b.arch == AR_HOUSE_TWO) {
        const float r = b.hx / Max(b.hy, 0.1f);
        if (r < 0.7f || r > 1.45f || b.hx < 3.6f || b.hy < 3.6f) return HF_PLAIN;
        if (p < (town ? 0.55f : (city ? 0.4f : 0.15f))) return HF_FOURSQUARE;
    }
    return HF_PLAIN;
}
// The stilt house's roof (0 side gable, 1 front gable, 2 shed, 3 hip) and decks (0 all round, 1 the front, 2 the front and a
// side), and the mobile home's width (a double-wide where the lot is deep enough) and roof (tin, or a shingled roof-over):
// seeded here so that the repetition log sees what massing.cpp builds
inline int pickHashed(u32 h, const float* w, int n) {
    float sum = 0.f;
    for (int k = 0; k < n; k++) sum += w[k];
    float p = hashToFloat(h) * sum;
    for (int k = 0; k < n; k++) {
        if (p < w[k]) return k;
        p -= w[k];
    }
    return n - 1;
}
inline int stiltRoofKind(const Building& b) {
    const float w[4] = {0.35f, 0.25f, 0.25f, 0.15f};
    return pickHashed(hash32(b.seed ^ 0x57A1Fu), w, 4);
}
inline int stiltDeckKind(const Building& b) {
    const float w[3] = {0.35f, 0.4f, 0.25f};
    return pickHashed(hash32(b.seed ^ 0x57A20u), w, 3);
}
inline bool trailerDouble(const Building& b) { return 2.f * b.hy >= 8.4f && (hash32(b.seed ^ 0x7A1E6u) & 1u); }
inline bool trailerRoofOver(const Building& b) {
    Rng hr(b.seed ^ 0x40053u);   // (houseRoofFinish's stream: its first draw picks tin or the roof-over; tin on most farms)
    if (hr.f() < 0.72f) return false;
    return !(b.style == BS_FARMHOUSE && hr.chance(0.75f));
}
// What the repetition signature adds for a building's form inside its archetype and massing
// A ranch house's front porch (massing.cpp): 0 none, 1 a long low porch under a shed roof across the middle of a box
// ranch's front, 2 one in the inner corner of an L - more on the Grove's older ranches than on Westbrook's tract houses
inline int ranchPorch(const Building& b) {
    if (b.arch != AR_HOUSE_RANCH || b.interior >= 0 || (b.massing != MK_BOX && b.massing != MK_L)) return 0;
    const float share = b.region == REG_GROVE ? 0.55f : (b.region == REG_SUBURBS ? 0.38f : 0.42f);
    return hashToFloat(hash32(b.seed ^ 0x2A0C4u)) < share ? (b.massing == MK_BOX ? 1 : 2) : 0;
}

// The forms inside the villa types (massing.cpp), so that the island estates differ beyond their type and massing: the
// Mediterranean villa 0 plain (the tiled portico at the door), 1 an arcaded loggia across the front with a terrace on it,
// 2 a balcony on corbels over the entry; the colonial villa 0 the two-storey portico, 1 a full-width veranda with a
// balustraded balcony on it; the modern villa 0 plain, 1 a roof terrace with a pergola. Box-shaped fronts only (and the
// Mediterranean corner tower's: the loggia keeps beside the tower).
inline int villaForm(const Building& b) {
    if (b.style != BS_VILLA || b.interior >= 0) return 0;
    const float p = hashToFloat(hash32(b.seed ^ 0x7111Au));
    switch (b.arch) {
        case AR_VILLA_MED: return (b.massing == MK_BOX || b.massing == MK_CORNER_TOWER) ? (p < 0.38f ? 0 : (p < 0.72f ? 1 : 2)) : 0;
        case AR_VILLA_COLONIAL: return b.massing == MK_BOX && p >= 0.5f ? 1 : 0;
        case AR_VILLA_MODERN: return p < 0.45f ? 0 : 1;
        default: return 0;
    }
}

inline u32 formKey(const Building& b) {
    if (b.style == BS_VILLA) return 80u + (u32)villaForm(b);
    if (b.arch == AR_HOUSE_RANCH) return 48u + (u32)ranchPorch(b);
    if (b.arch == AR_SHACK_STILT || b.arch == AR_HOUSE_RAISED) return 16u + (u32)stiltRoofKind(b) * 4u + (u32)stiltDeckKind(b);
    if (b.arch == AR_HOUSE_TRAILER) return 32u + (trailerDouble(b) ? 1u : 0u) + (trailerRoofOver(b) ? 2u : 0u);
    return (u32)houseForm(b);
}

// The house's porch comes with its massing (massing.cpp), over the door: facadedetail.cpp adds no door canopy of its own
inline bool massingPorch(const Building& b) {
    return b.interior < 0 && ((b.style == BS_HOUSE && b.arch == AR_HOUSE_BUNGALOW && b.massing == MK_BOX) || houseForm(b) == HF_FOURSQUARE || ranchPorch(b) != 0);
}

struct Pick {
    u8 arch;
    float w;
};

// Palette of a district for a building style. main: on an avenue or boulevard; corner: on a street corner.
int palette(int reg, int style, bool main, bool corner, Pick* out) {
    int n = 0;
    auto add = [&](u8 a, float w) {
        if (w > 0.f) out[n++] = {a, w};
    };
    switch (style) {
        case BS_SHOPS:
            switch (reg) {
                case REG_CALLE_LUNA:
                    add(AR_SHOP_TAXPAYER, 3.f); add(AR_SHOP_1920, main ? 3.f : 2.f); add(AR_SHOP_MED, 2.5f); add(AR_SHOP_MIMO, main ? 2.f : 1.f);
                    add(AR_SHOP_ARCADE, main ? 0.8f : 0.f); add(AR_SHOP_BODEGA, corner ? 5.f : 0.6f);
                    break;
                case REG_NORTH_CITY:
                    add(AR_SHOP_TAXPAYER, 3.f); add(AR_SHOP_MIMO, 3.f); add(AR_SHOP_1920, 1.5f); add(AR_SHOP_MED, 1.5f); add(AR_SHOP_BODEGA, corner ? 3.f : 0.4f);
                    break;
                case REG_MIDTOWN:
                    add(AR_SHOP_1920, 3.f); add(AR_SHOP_TAXPAYER, 2.5f); add(AR_SHOP_MIMO, 2.f); add(AR_SHOP_MED, 1.f); add(AR_SHOP_BODEGA, corner ? 2.5f : 0.4f);
                    add(AR_SHOP_ARCADE, 0.5f);
                    break;
                case REG_BEACH:
                    add(AR_SHOP_MIMO, 3.f); add(AR_SHOP_MED, 2.f); add(AR_SHOP_TAXPAYER, 1.5f); add(AR_SHOP_ARCADE, 1.5f); add(AR_SHOP_1920, 1.f);
                    break;
                case REG_GROVE:
                    add(AR_SHOP_MED, 3.f); add(AR_SHOP_ARCADE, 2.5f); add(AR_SHOP_1920, 1.5f); add(AR_SHOP_MIMO, 1.f);
                    break;
                case REG_FLATS:
                    add(AR_SHOP_TAXPAYER, 3.f); add(AR_SHOP_BODEGA, corner ? 3.f : 0.8f); add(AR_SHOP_MIMO, 1.5f); add(AR_SHOP_1920, 1.f);
                    break;
                case REG_FORT_CASTELL:
                    add(AR_SHOP_1920, 4.f); add(AR_SHOP_TAXPAYER, 3.f); add(AR_SHOP_MED, 0.5f);
                    break;
                case REG_LAKE_TOWN:
                case REG_HARLOW:
                    add(AR_SHOP_1920, 4.f); add(AR_SHOP_TAXPAYER, 3.f); add(AR_SHOP_GALLERY, 0.8f);
                    break;
                case REG_KEY_TOWN:
                case REG_GULF_TOWN:
                    add(AR_SHOP_GALLERY, 3.f); add(AR_SHOP_TAXPAYER, 1.5f); add(AR_SHOP_MED, 1.5f); add(AR_SHOP_MIMO, 1.f);
                    break;
                default:
                    add(AR_SHOP_TAXPAYER, 2.f); add(AR_SHOP_MIMO, 1.5f); add(AR_SHOP_1920, 1.f); add(AR_SHOP_MED, 1.f);
                    break;
            }
            break;
        case BS_MIDRISE:
            switch (reg) {
                case REG_DOWNTOWN: add(AR_MID_OFFICE60, 3.f); add(AR_MID_WALKUP, 2.5f); add(AR_MID_MODERN, 3.f); add(AR_MID_LOFT, 0.5f); break;
                case REG_MIDTOWN: add(AR_MID_LOFT, 3.f); add(AR_MID_MODERN, 2.5f); add(AR_MID_WALKUP, 2.f); add(AR_MID_MIMO, 1.f); add(AR_MID_OFFICE60, 1.f); break;
                case REG_CALLE_LUNA: add(AR_MID_WALKUP, 2.5f); add(AR_MID_MIMO, 3.f); add(AR_MID_MED, 3.f); add(AR_MID_MODERN, 0.5f); break;
                case REG_NORTH_CITY: add(AR_MID_MIMO, 3.5f); add(AR_MID_MED, 2.f); add(AR_MID_WALKUP, 1.5f); add(AR_MID_MODERN, 1.5f); add(AR_MID_OFFICE60, 0.5f); break;
                case REG_BEACH: add(AR_MID_MIMO, 3.f); add(AR_MID_MED, 2.f); add(AR_MID_MODERN, 2.f); break;
                case REG_GROVE: add(AR_MID_MED, 4.f); add(AR_MID_MODERN, 1.5f); add(AR_MID_MIMO, 1.f); break;
                case REG_FLATS: add(AR_MID_WALKUP, 2.f); add(AR_MID_LOFT, 2.f); add(AR_MID_MIMO, 1.f); break;
                case REG_FORT_CASTELL: add(AR_MID_WALKUP, 3.f); add(AR_MID_LOFT, 2.f); break;
                default: add(AR_MID_MIMO, 2.f); add(AR_MID_MED, 1.f); add(AR_MID_WALKUP, 1.f); add(AR_MID_MODERN, 1.f); break;
            }
            break;
        case BS_CONDO:
            switch (reg) {
                case REG_BEACH: add(AR_CONDO_SLAB, 3.f); add(AR_CONDO_MIMO, 3.f); add(AR_CONDO_GLASS, 3.f); add(AR_CONDO_PODIUM, 1.5f); break;
                case REG_FINANCIAL: add(AR_CONDO_GLASS, 3.f); add(AR_CONDO_PODIUM, 3.f); add(AR_CONDO_SLAB, 1.f); break;
                case REG_KEY_CORAL: add(AR_CONDO_GLASS, 2.f); add(AR_CONDO_MIMO, 2.f); add(AR_CONDO_SLAB, 1.f); break;
                default: add(AR_CONDO_SLAB, 2.f); add(AR_CONDO_GLASS, 2.f); add(AR_CONDO_PODIUM, 1.f); add(AR_CONDO_MIMO, 1.f); break;
            }
            break;
        case BS_HOUSE:
            switch (reg) {
                case REG_SUBURBS:
                    add(AR_HOUSE_RANCH, 4.f); add(AR_HOUSE_MED, 2.f); add(AR_HOUSE_SPLIT, 1.5f); add(AR_HOUSE_TWO, 1.2f); add(AR_HOUSE_MIMO, 1.f); add(AR_HOUSE_BUNGALOW, 1.f);
                    add(AR_HOUSE_CBS, 1.6f);
                    break;
                case REG_GROVE: add(AR_HOUSE_MED, 3.f); add(AR_HOUSE_TWO, 2.f); add(AR_HOUSE_RANCH, 1.5f); add(AR_HOUSE_BUNGALOW, 1.5f); add(AR_HOUSE_MIMO, 1.f); break;
                case REG_NORTH_CITY: case REG_CALLE_LUNA: case REG_FLATS: case REG_MIDTOWN:
                    // (the block houses of Little Havana, Allapattah and Hialeah alongside the bungalows)
                    add(AR_HOUSE_BUNGALOW, 4.f); add(AR_HOUSE_CBS, 3.5f); add(AR_HOUSE_RANCH, 2.f); add(AR_HOUSE_MIMO, 1.5f); add(AR_HOUSE_MED, 1.5f);
                    add(AR_HOUSE_TWO, 0.6f);
                    break;
                case REG_LAKE_TOWN: case REG_HARLOW: case REG_FORT_CASTELL: case REG_REDLAND: case REG_FARMLAND:
                    // the old county towns: frame Victorians and bungalows round the centre, block houses and ranches outside
                    add(AR_HOUSE_BUNGALOW, 3.f); add(AR_HOUSE_TWO, 2.f); add(AR_HOUSE_RANCH, 2.f); add(AR_HOUSE_CONCH, 0.5f);
                    add(AR_HOUSE_VICTORIAN, reg == REG_REDLAND || reg == REG_FARMLAND ? 0.8f : 2.4f); add(AR_HOUSE_CBS, 1.6f);
                    // (mobile homes on the edges of the county towns, more out on the farm roads)
                    add(AR_HOUSE_TRAILER, reg == REG_REDLAND || reg == REG_FARMLAND ? 1.8f : (reg == REG_HARLOW ? 1.1f : 0.6f));
                    break;
                case REG_KEY_TOWN: case REG_GULF_TOWN: case REG_KEYS:
                    add(AR_HOUSE_CONCH, 5.f); add(AR_HOUSE_BUNGALOW, 1.f); add(AR_HOUSE_MIMO, 0.5f); add(AR_HOUSE_VICTORIAN, reg == REG_KEY_TOWN ? 1.5f : 0.f);
                    add(AR_HOUSE_CBS, 0.8f); add(AR_HOUSE_RAISED, reg == REG_KEY_TOWN ? 1.6f : 2.4f);
                    add(AR_HOUSE_TRAILER, reg == REG_KEY_TOWN ? 0.6f : 1.5f);   // (the fishing village's and the Keys' mobile homes)
                    break;
                default: add(AR_HOUSE_RANCH, 2.f); add(AR_HOUSE_BUNGALOW, 2.f); add(AR_HOUSE_MED, 1.f); add(AR_HOUSE_TWO, 1.f); add(AR_HOUSE_CBS, 1.f); break;
            }
            break;
        case BS_DECO:
            // the classic deco hotel (central tower, name blade, ziggurat crest) stays the most common
            add(AR_NONE, 3.f);
            add(AR_DECO_STREAMLINE, corner ? 4.f : 2.f);
            add(AR_DECO_MED, reg == REG_BEACH ? 1.5f : 2.5f);
            add(AR_DECO_MIMO, 1.5f);
            break;
        case BS_VILLA:
            // the classic generator's mansion, the white modernist villa, the Mediterranean-revival villa of Coral
            // Gables and Palm Beach, the white colonial with a two-storey portico
            add(AR_NONE, reg == REG_KEY_CORAL || reg == REG_BAY_ISLAND ? 0.6f : 2.f);   // (the island estates: mostly the types)
            add(AR_VILLA_MODERN, reg == REG_GROVE ? 1.2f : 2.f);
            add(AR_VILLA_MED, reg == REG_GROVE ? 2.5f : 1.8f);
            add(AR_VILLA_COLONIAL, reg == REG_GROVE ? 0.8f : 1.2f);
            break;
        case BS_SHACK:
            // the street lots of the fishing towns (the site shacks of the Sawgrass and the keys stay the generator's)
            if (reg == REG_GULF_TOWN || reg == REG_KEYS || reg == REG_KEY_TOWN) add(AR_SHACK_STILT, 1.f);
            break;
        case BS_MOTEL:
            // Key Solano's guest houses and motels lean to the islands' wood and tin; the arterials have the motor courts
            // and the seventies inns
            if (reg == REG_KEY_TOWN || reg == REG_KEYS || reg == REG_GULF_TOWN) {
                add(AR_MOTEL_KEYS, 3.f); add(AR_MOTEL_MIMO, 1.5f); add(AR_MOTEL_MED, 1.2f); add(AR_MOTEL_INN, 0.5f);
            } else {
                add(AR_MOTEL_MIMO, 3.f); add(AR_MOTEL_INN, 2.f); add(AR_MOTEL_MED, 1.5f); add(AR_MOTEL_KEYS, 0.3f);
            }
            break;
        case BS_FARMHOUSE:
            // Florida farmhouses: the cracker house (a raised wood house with a deep porch and a tin roof: the conch
            // house's mainland cousin), the two-storey I-house, the fifties ranch, the generator's plain house
            add(AR_HOUSE_CONCH, 2.5f); add(AR_HOUSE_TWO, 1.5f); add(AR_HOUSE_RANCH, 1.2f); add(AR_NONE, 1.f); add(AR_HOUSE_VICTORIAN, 1.f);
            add(AR_HOUSE_TRAILER, 1.f);   // (the mobile home by the barn)
            break;
        case BS_BARN:
            add(AR_BARN_GAMBREL, 2.f); add(AR_BARN_POLE, 2.f); add(AR_BARN_GABLE, 1.5f);
            break;
        case BS_CHURCH:
            if (reg == REG_LAKE_TOWN || reg == REG_HARLOW || reg == REG_FORT_CASTELL) {
                add(AR_CHURCH_CLAPBOARD, 3.f); add(AR_CHURCH_BRICK, 2.f); add(AR_CHURCH_MISSION, 0.8f); add(AR_NONE, 0.8f);
            } else if (reg == REG_GROVE || reg == REG_CALLE_LUNA) {
                add(AR_CHURCH_MISSION, 3.f); add(AR_CHURCH_BRICK, 1.f); add(AR_CHURCH_AFRAME, 0.8f); add(AR_NONE, 0.8f);
            } else {
                add(AR_CHURCH_AFRAME, 2.f); add(AR_CHURCH_MISSION, 2.f); add(AR_CHURCH_CLAPBOARD, 1.5f); add(AR_CHURCH_BRICK, 1.f); add(AR_NONE, 1.f);
            }
            break;
        case BS_STRIPMALL:
            add(AR_STRIP_MISSION, reg == REG_GROVE ? 3.f : 2.f);
            add(AR_STRIP_MIMO, 2.f);
            add(AR_STRIP_MODERN, main ? 2.5f : 1.5f);
            break;
        case BS_WAREHOUSE:
        case BS_FACTORY:
            if (reg == REG_PORT) break;
            add(AR_NONE, 3.f);
            add(AR_WARE_SAWTOOTH, 2.f);
            add(AR_WARE_OFFICE, 2.f);
            add(AR_WARE_BODYSHOP, main ? 2.f : (reg == REG_MIDTOWN || reg == REG_FLATS ? 1.f : 0.4f));
            break;
        default: break;
    }
    return n;
}

// Massing variants an archetype may take, given the lot and whether the street front must stay whole (storefronts:
// the renderer's shopfront lights and the shop doors sit on the front face of the envelope)
int massings(u8 arch, bool storefront, bool corner, float w, float d, Pick* out) {
    int n = 0;
    auto add = [&](u8 m, float wt) {
        if (wt > 0.f) out[n++] = {m, wt};
    };
    bool wide = w > 22.f, deep = d > 24.f;
    switch (arch) {
        case AR_SHOP_TAXPAYER: add(MK_BOX, 3.f); add(MK_SPLIT, wide ? 2.f : 0.5f); add(MK_STEP_BACK, deep ? 1.f : 0.f); break;
        case AR_SHOP_1920:
            add(MK_BOX, 3.f); add(MK_L, deep ? 1.5f : 0.f); add(MK_STEP_BACK, deep ? 1.5f : 0.f); add(MK_SPLIT, wide ? 1.5f : 0.f); add(MK_CORNER_TOWER, corner ? 1.5f : 0.f);
            add(MK_CHAMFER, corner ? 2.f : 0.f);
            break;
        case AR_SHOP_MED: add(MK_BOX, 2.f); add(MK_CORNER_TOWER, corner ? 3.f : 1.2f); add(MK_L, deep ? 1.f : 0.f); add(MK_SPLIT, wide ? 1.f : 0.f); break;
        case AR_SHOP_MIMO: add(MK_BOX, 2.f); add(MK_SPLIT, wide ? 2.f : 0.5f); add(MK_STEP_FRONT, deep ? 1.f : 0.f); break;
        case AR_SHOP_BODEGA: add(MK_BOX, corner ? 1.f : 3.f); add(MK_CHAMFER, corner ? 3.f : 0.f); add(MK_STEP_BACK, deep ? 1.f : 0.f); break;
        case AR_SHOP_ARCADE: add(MK_BOX, 2.f); add(MK_L, deep ? 1.f : 0.f); add(MK_CORNER_TOWER, corner ? 2.f : 0.6f); break;
        case AR_SHOP_GALLERY: add(MK_BOX, 3.f); add(MK_STEP_BACK, deep ? 1.f : 0.f); break;
        case AR_MID_WALKUP: add(MK_BOX, 2.f); add(MK_L, deep ? 2.f : 0.f); add(MK_U, deep && wide ? 1.5f : 0.f); add(MK_STEP_BACK, 1.f); break;
        case AR_MID_MIMO:
            add(MK_U, wide && deep ? 3.f : 0.f); add(MK_L, deep ? 2.5f : 0.5f); add(MK_COURT, wide && deep && !storefront ? 1.f : 0.f); add(MK_BOX, 1.f);
            add(MK_STEP_FRONT, storefront ? 1.5f : 0.f);
            break;
        case AR_MID_MED: add(MK_CORNER_TOWER, corner ? 3.f : 1.5f); add(MK_L, deep ? 1.5f : 0.f); add(MK_U, wide && deep ? 1.f : 0.f); add(MK_BOX, 1.f); add(MK_WINGS, wide ? 1.f : 0.f); break;
        case AR_MID_OFFICE60: add(MK_BOX, 2.f); add(MK_PODIUM_SLAB, deep ? 2.f : 0.5f); add(MK_STEP_BACK, 0.8f); break;
        case AR_MID_MODERN: add(MK_STEP_FRONT, 2.f); add(MK_PODIUM_SLAB, 2.f); add(MK_L, deep ? 1.f : 0.f); add(MK_BOX, 1.f); break;
        case AR_MID_LOFT: add(MK_BOX, 3.f); add(MK_STEP_FRONT, 1.f); add(MK_L, deep ? 1.f : 0.f); break;
        case AR_CONDO_SLAB: add(MK_BOX, 2.f); add(MK_PODIUM_SLAB, 2.f); add(MK_WINGS, wide ? 0.8f : 0.f); break;
        case AR_CONDO_GLASS: add(MK_PODIUM_SLAB, 2.f); add(MK_CURVE, 1.5f); add(MK_STEP_BACK, 1.f); break;
        case AR_CONDO_MIMO: add(MK_CURVE, 2.5f); add(MK_WINGS, wide ? 1.5f : 0.f); add(MK_PODIUM_SLAB, 1.f); add(MK_BOX, 0.5f); break;
        case AR_CONDO_PODIUM: add(MK_PODIUM_SLAB, 4.f); break;
        case AR_HOUSE_RANCH: add(MK_BOX, 2.f); add(MK_L, 2.f); add(MK_STEP_FRONT, 1.6f); break;
        case AR_HOUSE_BUNGALOW: add(MK_BOX, 3.f); add(MK_L, 1.2f); break;
        case AR_HOUSE_MED: add(MK_L, 2.f); add(MK_CORNER_TOWER, 1.f); add(MK_BOX, 1.f); break;
        case AR_HOUSE_TWO: add(MK_BOX, 3.f); add(MK_WINGS, 1.f); break;
        case AR_HOUSE_MIMO: add(MK_BOX, 2.f); add(MK_L, 1.5f); break;
        case AR_HOUSE_SPLIT: add(MK_SPLIT, 3.f); break;
        case AR_HOUSE_CONCH: add(MK_BOX, 3.f); add(MK_L, 1.f); break;
        case AR_HOUSE_CBS: add(MK_BOX, 2.f); add(MK_L, 1.5f); add(MK_STEP_FRONT, 1.f); break;
        case AR_HOUSE_VICTORIAN: add(MK_L, 3.f); add(MK_BOX, 1.f); break;
        case AR_HOUSE_RAISED: add(MK_BOX, 3.f); add(MK_L, 1.f); break;
        case AR_HOUSE_TRAILER: add(MK_BOX, 1.f); break;   // (single- or double-wide: massing.cpp buildTrailer)
        case AR_VILLA_MODERN: add(MK_STEP_BACK, 2.f); add(MK_L, 2.f); add(MK_SPLIT, 1.5f); break;
        case AR_VILLA_MED: add(MK_CORNER_TOWER, 2.f); add(MK_L, 1.5f); add(MK_WINGS, wide ? 1.5f : 0.4f); add(MK_BOX, 0.8f); break;
        case AR_VILLA_COLONIAL: add(MK_BOX, 2.f); add(MK_WINGS, wide ? 2.f : 0.5f); break;
        case AR_SHACK_STILT: add(MK_BOX, 3.f); add(MK_L, 1.3f); break;
        // motels: the front with its walkway and the parking in front stays whole; a back corner cut away, a lower office
        // end, a sign tower on a front corner
        case AR_MOTEL_MIMO: add(MK_BOX, 2.f); add(MK_L, deep ? 1.5f : 0.f); add(MK_SPLIT, wide ? 2.f : 0.f); add(MK_CORNER_TOWER, 1.f); break;
        case AR_MOTEL_MED: add(MK_BOX, 2.f); add(MK_L, deep ? 1.5f : 0.f); add(MK_SPLIT, wide ? 1.5f : 0.f); add(MK_CORNER_TOWER, 1.2f); break;
        case AR_MOTEL_KEYS: add(MK_BOX, 3.f); add(MK_L, deep ? 1.5f : 0.f); add(MK_SPLIT, wide ? 1.6f : 0.8f); break;   // (a lower office end)
        case AR_MOTEL_INN: add(MK_BOX, 2.f); add(MK_L, deep ? 1.5f : 0.f); add(MK_SPLIT, wide ? 1.5f : 0.f); break;
        // strip malls: an anchor end, an entry tower, a lower back
        case AR_STRIP_MISSION: add(MK_BOX, 1.5f); add(MK_CORNER_TOWER, 2.5f); add(MK_SPLIT, wide ? 1.5f : 0.f); break;
        case AR_STRIP_MIMO: add(MK_BOX, 2.5f); add(MK_STEP_BACK, deep ? 1.f : 0.f); add(MK_SPLIT, wide ? 1.f : 0.f); break;
        case AR_STRIP_MODERN: add(MK_SPLIT, wide ? 3.f : 0.f); add(MK_CORNER_TOWER, 1.5f); add(MK_BOX, 1.f); add(MK_STEP_BACK, deep ? 1.f : 0.f); break;
        case AR_BARN_GAMBREL: case AR_BARN_POLE: add(MK_BOX, 1.f); break;
        case AR_BARN_GABLE: add(MK_BOX, 1.5f); add(MK_WINGS, 1.f); break;
        case AR_CHURCH_MISSION: case AR_CHURCH_CLAPBOARD: case AR_CHURCH_BRICK: case AR_CHURCH_AFRAME: add(MK_BOX, 1.f); break;
        case AR_DECO_STREAMLINE: add(MK_ROUNDED, 4.f); break;
        case AR_DECO_MED: add(MK_CORNER_TOWER, corner ? 3.f : 1.5f); add(MK_BOX, 1.5f); add(MK_U, wide && deep ? 1.f : 0.f); break;
        case AR_DECO_MIMO: add(MK_BOX, 1.5f); add(MK_WINGS, wide ? 1.5f : 0.f); add(MK_L, deep ? 1.5f : 0.f); add(MK_U, wide && deep ? 1.f : 0.f); break;
        case AR_WARE_SAWTOOTH: add(MK_BOX, 3.f); break;
        case AR_WARE_OFFICE: add(MK_STEP_FRONT, 3.f); break;
        case AR_WARE_BODYSHOP: add(MK_BOX, 3.f); add(MK_SPLIT, wide ? 1.f : 0.f); break;
        default: add(MK_BOX, 1.f); break;
    }
    return n;
}

u8 roofFor(u8 arch, u8 massing, Rng& r) {
    switch (arch) {
        case AR_SHOP_TAXPAYER: return r.chance(0.12f) ? RFM_TILE_PENT : RFM_PARAPET;
        case AR_SHOP_1920: return RFM_PARAPET;
        case AR_SHOP_MED: return r.chance(0.7f) ? RFM_TILE_PENT : RFM_TILE_HIP;
        case AR_SHOP_MIMO: return RFM_EAVE;
        case AR_SHOP_BODEGA: return r.chance(0.25f) ? RFM_TILE_PENT : RFM_PARAPET;
        case AR_SHOP_ARCADE: return r.chance(0.55f) ? RFM_TILE_HIP : RFM_TILE_PENT;
        case AR_SHOP_GALLERY: return RFM_METAL_GABLE;
        case AR_MID_WALKUP: return RFM_PARAPET;
        case AR_MID_MIMO: return r.chance(0.8f) ? RFM_EAVE : RFM_PARAPET;
        case AR_MID_MED: return r.chance(0.6f) ? RFM_TILE_HIP : RFM_TILE_PENT;
        case AR_MID_OFFICE60: return r.chance(0.5f) ? RFM_EAVE : RFM_PARAPET;
        case AR_MID_MODERN: return r.chance(0.6f) ? RFM_TERRACE : RFM_PARAPET;
        case AR_MID_LOFT: return r.chance(0.5f) ? RFM_TERRACE : RFM_PARAPET;
        case AR_CONDO_SLAB: return RFM_PARAPET;
        case AR_CONDO_GLASS: return r.chance(0.5f) ? RFM_TERRACE : RFM_PARAPET;
        case AR_CONDO_MIMO: return r.chance(0.6f) ? RFM_EAVE : RFM_PARAPET;
        case AR_CONDO_PODIUM: return r.chance(0.5f) ? RFM_TERRACE : RFM_PARAPET;
        case AR_HOUSE_RANCH: return RFM_PARAPET;   // (houses: pitched roofs chosen by buildmesh from the archetype)
        case AR_HOUSE_MIMO: return r.chance(0.5f) ? RFM_BUTTERFLY : RFM_EAVE;
        case AR_VILLA_MODERN: return RFM_EAVE;
        case AR_VILLA_MED: return RFM_TILE_HIP;
        case AR_SHACK_STILT: return RFM_METAL_GABLE;
        case AR_MOTEL_MIMO: return r.chance(0.75f) ? RFM_EAVE : RFM_BUTTERFLY;
        case AR_MOTEL_MED: return RFM_TILE_HIP;
        case AR_MOTEL_KEYS: return RFM_METAL_GABLE;
        case AR_MOTEL_INN: return RFM_MANSARD;
        case AR_STRIP_MISSION: return RFM_TILE_PENT;
        case AR_STRIP_MIMO: return r.chance(0.6f) ? RFM_EAVE : RFM_PARAPET;
        case AR_STRIP_MODERN: return RFM_PARAPET;
        case AR_BARN_GAMBREL: case AR_BARN_POLE: case AR_BARN_GABLE: return RFM_METAL_GABLE;
        case AR_CHURCH_MISSION: return RFM_TILE_HIP;
        case AR_CHURCH_CLAPBOARD: case AR_CHURCH_BRICK: case AR_CHURCH_AFRAME: return RFM_METAL_GABLE;
        case AR_DECO_STREAMLINE: return r.chance(0.6f) ? RFM_EAVE : RFM_PARAPET;
        case AR_DECO_MED: return r.chance(0.55f) ? RFM_TILE_PENT : RFM_TILE_HIP;
        case AR_DECO_MIMO: return RFM_EAVE;
        case AR_WARE_SAWTOOTH: return RFM_SAWTOOTH;
        case AR_WARE_OFFICE: return RFM_PARAPET;
        case AR_WARE_BODYSHOP: return r.chance(0.5f) ? RFM_METAL_GABLE : RFM_PARAPET;
        default: return RFM_PARAPET;
    }
}

// Is there a cross street right beside the lot's side (sgn = -1 / +1 along ax)? Corner lots get corner types.
bool crossStreetAt(const RoadNetwork& roads, const Building& b, float sgn) {
    vec2 p = b.lotC + b.ax * (sgn * (b.lotHx + 5.f)) + b.front * (b.lotHy * 0.3f);
    float ds, dd, side;
    int e = roads.nearestEdge(p, 12.f, &ds, &dd, &side);
    if (e < 0) return false;
    const RoadEdge& ed = roads.edges[e];
    if (ed.cls == RC_HIGHWAY || ed.cls == RC_RAMP) return false;
    vec2 t = normalize(ed.tangentAt(ds).xy());
    return fabsf(dot(t, b.ax)) < 0.55f && dd < ed.halfWidth + ed.sidewalk + 6.f;
}

// ------------------------------------------------------------------------------------------------ facade records
// Rewrites the facade record of a building for its archetype (floor heights, window rhythm, materials, colours, the
// storefront and sign band). `store` keeps the shops' storefront ground floor.
void facadeFor(Building& b, FacadeGPU& f, u8 arch, Rng& r, bool store) {
    vec3 wall(0.9f), frame(0.2f), glass = kGlass[r.next() % ARRAY_COUNT(kGlass)];
    MaterialId mat = MAT_STUCCO;
    float floorH = f.floorH, groundH = f.groundH, bay = f.bayW, winW = f.winW, winH = f.winH, sill = f.sillH;
    int style = (int)f.style;
    u32 flags = f.flags & ~(1u | 2u | 4u);
    if (store) flags |= 1u;
    int reg = b.region;
    switch (arch) {
        case AR_SHOP_TAXPAYER:
            style = 0; floorH = 3.2f; groundH = r.range(4.3f, 5.0f); bay = r.range(3.0f, 4.2f); winW = 0.5f; winH = 0.55f; sill = 0.9f;
            wall = reg == REG_CALLE_LUNA || reg == REG_FLATS || reg == REG_KEY_TOWN ? pick(kOcho, r) : (r.chance(0.5f) ? pick(kPastels, r) : pick(kMimoWall, r));
            frame = pick(kBright, r);
            flags |= 2u;
            if (r.chance(0.2f)) { mat = MAT_BRICK; wall = vec3(1.f); }
            break;
        case AR_SHOP_1920: {
            style = 0; floorH = r.range(3.3f, 3.7f); groundH = r.range(4.6f, 5.4f); bay = r.range(2.4f, 3.2f); winW = r.range(0.42f, 0.55f); winH = r.range(0.58f, 0.66f); sill = 0.85f;
            bool brick = r.chance(reg == REG_FORT_CASTELL || reg == REG_LAKE_TOWN || reg == REG_HARLOW || reg == REG_MIDTOWN ? 0.65f : 0.35f);
            if (brick) { mat = MAT_BRICK; wall = r.chance(0.7f) ? vec3(1.f) * r.range(0.85f, 1.05f) : vec3(1.0f, 0.92f, 0.8f); }
            else if (r.chance(0.4f)) { mat = MAT_BRICK; wall = pick(kPaintedBrick, r) * 1.25f; }   // painted brick
            else wall = r.chance(0.5f) ? pick(kMedWarm, r) : pick(kPastels, r);
            frame = r.chance(0.5f) ? vec3(0.12f, 0.2f, 0.15f) : (r.chance(0.5f) ? vec3(0.45f, 0.1f, 0.1f) : vec3(0.1f));
            if (r.chance(0.6f)) flags |= 2u;
            break;
        }
        case AR_SHOP_MED:
            style = 0; floorH = r.range(3.3f, 3.6f); groundH = r.range(4.4f, 5.0f); bay = r.range(2.6f, 3.4f); winW = r.range(0.34f, 0.42f); winH = r.range(0.46f, 0.52f); sill = 0.85f;
            wall = pick(kMedWarm, r);
            frame = r.chance(0.6f) ? vec3(0.3f, 0.18f, 0.1f) : vec3(0.15f, 0.25f, 0.2f);
            if (r.chance(0.45f)) flags |= 2u;
            break;
        case AR_SHOP_MIMO:
            style = r.chance(0.35f) ? 2 : 0; floorH = r.range(3.1f, 3.4f); groundH = r.range(3.9f, 4.5f); bay = r.range(3.2f, 4.6f); winW = r.range(0.6f, 0.75f); winH = r.range(0.45f, 0.55f); sill = 1.0f;
            wall = r.chance(0.65f) ? pick(kMimoWall, r) : pick(kMimoAccent, r);
            frame = r.chance(0.5f) ? pick(kMimoAccent, r) : vec3(0.85f, 0.86f, 0.88f);
            flags |= 2u;
            break;
        case AR_SHOP_BODEGA:
            style = 0; floorH = 3.2f; groundH = r.range(4.2f, 4.8f); bay = r.range(2.8f, 3.6f); winW = 0.5f; winH = 0.55f; sill = 0.9f;
            wall = pick(kOcho, r);
            frame = pick(kBright, r);
            flags |= 2u | 4u;
            break;
        case AR_SHOP_ARCADE:
            style = 0; floorH = r.range(3.3f, 3.6f); groundH = r.range(4.6f, 5.2f); bay = r.range(3.2f, 4.0f); winW = r.range(0.36f, 0.44f); winH = r.range(0.48f, 0.54f); sill = 0.85f;
            wall = r.chance(0.7f) ? pick(kMedWarm, r) : pick(kMimoWall, r);
            frame = vec3(0.25f, 0.17f, 0.1f);
            if (r.chance(0.5f)) flags |= 2u;
            break;
        case AR_SHOP_GALLERY:
            style = 5; floorH = 3.1f; groundH = r.range(3.8f, 4.3f); bay = r.range(2.8f, 3.4f); winW = r.range(0.38f, 0.46f); winH = 0.6f; sill = 0.8f;
            mat = MAT_WOOD_SIDING; wall = pick(kConch, r);
            frame = vec3(0.95f);
            flags |= 2u;
            break;
        case AR_MID_WALKUP: {
            style = 0; floorH = r.range(3.0f, 3.3f); groundH = r.range(3.8f, 4.6f); bay = r.range(2.4f, 3.2f); winW = r.range(0.42f, 0.52f); winH = r.range(0.5f, 0.6f); sill = 0.9f;
            float m = r.f();
            if (m < 0.45f) { mat = MAT_BRICK; wall = vec3(1.f) * r.range(0.82f, 1.05f); }
            else if (m < 0.65f) { mat = MAT_BRICK; wall = pick(kPaintedBrick, r) * 1.2f; }
            else wall = reg == REG_CALLE_LUNA ? pick(kOcho, r) : pick(kPastels, r);
            frame = r.chance(0.6f) ? vec3(0.92f) : vec3(0.12f);
            if (store && r.chance(0.5f)) flags |= 2u;
            break;
        }
        case AR_MID_MIMO:
            style = r.chance(0.25f) ? 2 : 0; floorH = r.range(2.9f, 3.05f); groundH = r.range(3.0f, 3.4f); bay = r.range(3.4f, 4.6f); winW = r.range(0.55f, 0.7f); winH = r.range(0.42f, 0.52f); sill = 1.0f;
            if (store) groundH = r.range(4.0f, 4.5f);
            wall = r.chance(0.6f) ? pick(kMimoWall, r) : pick(kDecoPastel, r);
            frame = r.chance(0.5f) ? vec3(0.85f, 0.86f, 0.88f) : pick(kMimoAccent, r);
            flags |= 8u;
            break;
        case AR_MID_MED:
            style = 0; floorH = r.range(3.1f, 3.35f); groundH = r.range(3.6f, 4.4f); bay = r.range(2.8f, 3.6f); winW = r.range(0.32f, 0.4f); winH = r.range(0.46f, 0.52f); sill = 0.8f;
            if (store) groundH = r.range(4.4f, 5.0f);
            wall = pick(kMedWarm, r);
            frame = r.chance(0.6f) ? vec3(0.3f, 0.18f, 0.1f) : vec3(0.92f);
            break;
        case AR_MID_OFFICE60: {
            float s = r.f();
            style = s < 0.6f ? 2 : (s < 0.8f ? 1 : 0); floorH = r.range(3.6f, 3.9f); groundH = r.range(4.6f, 5.6f); bay = style == 1 ? r.range(1.5f, 2.1f) : r.range(2.6f, 3.4f);
            winW = r.range(0.55f, 0.7f); winH = 0.6f; sill = floorH * 0.25f;
            mat = r.chance(0.5f) ? MAT_CONCRETE_PANEL : (r.chance(0.5f) ? MAT_MARBLE : MAT_STUCCO);
            wall = pick(kModernWall, r);
            if (wall.x < 0.7f && mat == MAT_STUCCO) wall = vec3(0.95f);
            frame = r.chance(0.5f) ? vec3(0.12f, 0.13f, 0.14f) : vec3(0.6f, 0.62f, 0.65f);
            glass = r.chance(0.4f) ? vec3(0.45f, 0.6f, 0.55f) : (r.chance(0.5f) ? vec3(0.6f, 0.5f, 0.38f) : glass);
            flags |= 16u;
            break;
        }
        case AR_MID_MODERN: {
            float s = r.f();
            style = s < 0.4f ? 3 : (s < 0.7f ? 1 : 0); floorH = r.range(3.15f, 3.4f); groundH = r.range(4.6f, 5.6f); bay = style == 1 ? r.range(1.6f, 2.4f) : r.range(3.2f, 4.6f);
            winW = r.range(0.62f, 0.82f); winH = r.range(0.62f, 0.78f); sill = 0.4f;
            mat = r.chance(0.5f) ? MAT_CONCRETE_PANEL : MAT_STUCCO;
            wall = pick(kModernWall, r);
            frame = r.chance(0.6f) ? vec3(0.12f) : vec3(0.75f);
            break;
        }
        case AR_MID_LOFT:
            style = 0; floorH = r.range(4.0f, 4.6f); groundH = r.range(4.6f, 5.2f); bay = r.range(3.0f, 4.0f); winW = r.range(0.6f, 0.72f); winH = r.range(0.6f, 0.7f); sill = 0.8f;
            if (r.chance(0.7f)) { mat = MAT_BRICK; wall = r.chance(0.7f) ? vec3(1.f) * r.range(0.8f, 1.05f) : pick(kPaintedBrick, r) * 1.2f; }
            else { mat = MAT_CONCRETE_PANEL; wall = pick(kIndustrialWall, r); }
            frame = r.chance(0.7f) ? vec3(0.1f) : vec3(0.3f, 0.32f, 0.3f);
            break;
        case AR_CONDO_SLAB:
            style = 3; floorH = r.range(2.95f, 3.15f); groundH = r.range(4.6f, 6.0f); bay = r.range(3.6f, 5.4f); winW = r.range(0.7f, 0.85f); winH = 0.8f; sill = 0.2f;
            wall = r.chance(0.6f) ? pick(kMimoWall, r) : pick(kDecoPastel, r) * 0.98f;
            frame = r.chance(0.5f) ? vec3(0.8f, 0.85f, 0.88f) : vec3(0.3f, 0.32f, 0.35f);
            break;
        case AR_CONDO_GLASS:
            style = r.chance(0.6f) ? 1 : 3; floorH = r.range(3.1f, 3.4f); groundH = r.range(5.5f, 7.f); bay = style == 1 ? r.range(1.5f, 2.2f) : r.range(3.5f, 5.0f);
            winW = 0.85f; winH = 0.85f; sill = 0.15f;
            mat = MAT_CONCRETE_PANEL;
            wall = r.chance(0.7f) ? vec3(0.97f) : vec3(0.7f, 0.72f, 0.74f);
            frame = r.chance(0.5f) ? vec3(0.85f, 0.87f, 0.9f) : vec3(0.18f, 0.2f, 0.22f);
            glass = r.chance(0.5f) ? vec3(0.45f, 0.65f, 0.8f) : (r.chance(0.5f) ? vec3(0.45f, 0.75f, 0.72f) : vec3(0.62f, 0.66f, 0.7f));
            break;
        case AR_CONDO_MIMO:
            style = r.chance(0.5f) ? 2 : 0; floorH = r.range(2.95f, 3.1f); groundH = r.range(5.0f, 6.5f); bay = r.range(3.0f, 4.0f); winW = r.range(0.6f, 0.8f); winH = r.range(0.45f, 0.55f); sill = 0.95f;
            wall = pick(kMimoWall, r);
            frame = pick(kMimoAccent, r);
            flags |= 4u;
            break;
        case AR_CONDO_PODIUM:
            style = r.chance(0.5f) ? 1 : 3; floorH = r.range(3.1f, 3.3f); groundH = r.range(5.5f, 6.5f); bay = style == 1 ? r.range(1.6f, 2.2f) : r.range(3.6f, 5.0f);
            winW = 0.85f; winH = 0.82f; sill = 0.2f;
            mat = MAT_CONCRETE_PANEL;
            wall = pick(kModernWall, r);
            frame = vec3(0.2f, 0.22f, 0.25f);
            glass = r.chance(0.5f) ? vec3(0.4f, 0.6f, 0.78f) : vec3(0.5f, 0.7f, 0.7f);
            break;
        // houses: style 5 (shuttered windows), stucco or siding by type
        case AR_HOUSE_RANCH: {
            // a third in the pale suburban whites and creams, the rest in the ranch colours, a sixth in brick; trim in white,
            // a deeper tone of the wall or dark bronze (the ranch rows used to be white on white)
            style = 5; floorH = 3.0f; groundH = 3.0f; bay = r.range(3.6f, 4.8f); winW = r.range(0.32f, 0.45f); winH = 0.5f; sill = 1.0f;
            wall = r.chance(0.32f) ? pick(kSuburb, r) : pick(kRanch, r);
            if (r.chance(0.17f)) mat = MAT_BRICK, wall = vec3(1.0f, 0.9f, 0.85f) * r.range(0.85f, 1.f);
            const float t = r.f();
            frame = (t < 0.6f || mat == MAT_BRICK) ? vec3(0.95f) : (t < 0.85f ? wall * 0.72f : vec3(0.27f, 0.23f, 0.2f));
            break;
        }
        case AR_HOUSE_BUNGALOW:
            style = 5; floorH = 3.0f; groundH = 3.0f; bay = r.range(3.0f, 3.8f); winW = r.range(0.34f, 0.44f); winH = 0.55f; sill = 0.9f;
            if (r.chance(0.45f)) { mat = MAT_WOOD_SIDING; wall = r.chance(0.5f) ? pick(kConch, r) : pick(kSuburb, r); }
            else wall = r.chance(0.5f) ? pick(kSuburb, r) : pick(kPastels, r);
            frame = vec3(0.95f);
            break;
        case AR_HOUSE_MED:
            style = 5; floorH = 3.1f; groundH = 3.2f; bay = r.range(3.2f, 4.2f); winW = r.range(0.3f, 0.38f); winH = 0.5f; sill = 0.85f;
            wall = pick(kMedWarm, r);
            frame = vec3(0.95f);
            break;
        case AR_HOUSE_TWO:
            style = 5; floorH = 2.9f; groundH = 3.0f; bay = r.range(3.0f, 3.8f); winW = r.range(0.34f, 0.42f); winH = 0.55f; sill = 0.9f;
            if (r.chance(0.35f)) mat = MAT_WOOD_SIDING;
            wall = r.chance(0.4f) ? pick(kRanch, r) : pick(kSuburb, r);   // (some colour among the suburbs' whites)
            frame = vec3(0.95f);
            break;
        case AR_HOUSE_MIMO:
            style = 5; floorH = 3.0f; groundH = 3.0f; bay = r.range(3.8f, 5.0f); winW = r.range(0.55f, 0.7f); winH = 0.45f; sill = 1.1f;
            wall = r.chance(0.6f) ? pick(kMimoWall, r) : pick(kMimoAccent, r) * 1.15f;
            frame = vec3(0.92f);
            break;
        case AR_HOUSE_SPLIT:
            style = 5; floorH = 2.8f; groundH = 3.0f; bay = r.range(3.2f, 4.2f); winW = r.range(0.34f, 0.45f); winH = 0.5f; sill = 1.0f;
            if (r.chance(0.3f)) mat = MAT_WOOD_SIDING;
            wall = r.chance(0.5f) ? pick(kRanch, r) : pick(kSuburb, r);
            frame = vec3(0.95f);
            break;
        case AR_HOUSE_CONCH:
            style = 5; floorH = 3.0f; groundH = 3.2f; bay = r.range(2.6f, 3.4f); winW = r.range(0.36f, 0.44f); winH = 0.62f; sill = 0.8f;
            mat = MAT_WOOD_SIDING; wall = pick(kConch, r);
            frame = vec3(0.97f);
            break;
        case AR_HOUSE_CBS:
            // wide, low awning windows in painted block; white or a darker tone of the wall for the trim
            style = 5; floorH = 2.9f; groundH = 2.9f; bay = r.range(3.2f, 4.0f); winW = r.range(0.4f, 0.5f); winH = r.range(0.42f, 0.5f); sill = 1.05f;
            mat = MAT_STUCCO; wall = pick(kCbs, r);
            frame = r.chance(0.6f) ? vec3(0.97f) : wall * 0.72f;
            break;
        case AR_HOUSE_RAISED:
            // board siding in the island colours or white, white trim, tall windows (hurricane shutters in facadedetail.cpp)
            style = 5; floorH = 3.0f; groundH = 3.1f; bay = r.range(2.8f, 3.6f); winW = r.range(0.38f, 0.46f); winH = 0.6f; sill = 0.8f;
            mat = MAT_WOOD_SIDING; wall = r.chance(0.35f) ? vec3(0.97f) : pick(kConch, r);
            frame = vec3(0.97f);
            break;
        case AR_HOUSE_TRAILER: {
            // lap siding (vinyl or aluminium) in white, cream, beige or a pale colour; small low windows; aluminium frames
            style = 5; floorH = 2.45f; groundH = 2.45f; bay = r.range(2.3f, 2.9f); winW = r.range(0.34f, 0.44f); winH = r.range(0.4f, 0.48f); sill = 1.0f;
            mat = MAT_WOOD_SIDING;
            const vec3 col[] = {vec3(0.98f, 0.98f, 0.96f), vec3(1.f, 0.96f, 0.86f), vec3(0.94f, 0.9f, 0.8f), vec3(0.84f, 0.9f, 0.96f), vec3(0.86f, 0.94f, 0.86f),
                                vec3(1.f, 0.95f, 0.76f), vec3(0.9f, 0.88f, 0.86f)};
            wall = pick(col, r);
            frame = r.chance(0.6f) ? vec3(0.88f, 0.88f, 0.86f) : vec3(0.97f);
            break;
        }
        case AR_HOUSE_VICTORIAN:
            // tall narrow two-over-two windows in board siding, white trim
            style = 5; floorH = 3.0f; groundH = 3.3f; bay = r.range(2.5f, 3.1f); winW = r.range(0.28f, 0.34f); winH = r.range(0.6f, 0.66f); sill = 0.75f;
            mat = MAT_WOOD_SIDING; wall = pick(kVictorian, r);
            frame = r.chance(0.8f) ? vec3(0.97f) : vec3(0.3f, 0.36f, 0.3f);
            break;
        case AR_VILLA_MODERN:
            style = 0; floorH = 3.4f; groundH = 3.6f; bay = r.range(3.6f, 5.0f); winW = r.range(0.7f, 0.85f); winH = r.range(0.7f, 0.82f); sill = 0.3f;
            wall = r.chance(0.8f) ? vec3(0.99f) : vec3(0.88f, 0.88f, 0.86f);
            frame = vec3(0.15f);
            break;
        case AR_VILLA_MED:
            style = 5; floorH = 3.3f; groundH = 3.7f; bay = r.range(3.4f, 4.4f); winW = r.range(0.3f, 0.36f); winH = 0.58f; sill = 0.55f;
            wall = pick(kMedWarm, r);
            frame = r.chance(0.6f) ? vec3(0.3f, 0.2f, 0.12f) : vec3(0.95f);
            break;
        case AR_VILLA_COLONIAL: {
            style = 5; floorH = 3.4f; groundH = 3.7f; bay = r.range(3.0f, 3.6f); winW = r.range(0.36f, 0.42f); winH = 0.62f; sill = 0.7f;
            const vec3 col[] = {vec3(1.f), vec3(0.98f, 0.96f, 0.9f), vec3(1.f, 0.96f, 0.8f), vec3(0.9f, 0.94f, 0.96f), vec3(0.96f, 0.9f, 0.86f)};
            wall = pick(col, r);
            frame = vec3(0.97f);
            break;
        }
        case AR_SHACK_STILT: {
            style = 5; floorH = 2.8f; groundH = r.range(2.7f, 3.1f); bay = r.range(2.4f, 3.2f); winW = r.range(0.38f, 0.48f); winH = 0.5f; sill = 0.95f;
            float p = r.f();
            const vec3 weathered[] = {vec3(0.72f, 0.66f, 0.58f), vec3(0.62f, 0.6f, 0.56f), vec3(0.8f, 0.76f, 0.68f)};
            if (p < 0.45f) { mat = MAT_WOOD_SIDING; wall = pick(kConch, r); }
            else if (p < 0.75f) { mat = MAT_WOOD_SIDING; wall = pick(weathered, r); }
            else if (p < 0.88f) { mat = MAT_WOOD_SIDING; wall = vec3(0.97f); }
            else { mat = MAT_CORRUGATED; wall = pick(kIndustrialWall, r); }
            frame = r.chance(0.6f) ? vec3(0.95f) : pick(kMimoAccent, r);
            flags |= 8u;
            break;
        }
        case AR_MOTEL_MIMO:
            style = r.chance(0.5f) ? 2 : 0; floorH = r.range(2.8f, 3.0f); groundH = r.range(3.0f, 3.3f); bay = r.range(3.6f, 4.4f); winW = r.range(0.4f, 0.5f); winH = 0.45f; sill = 1.1f;
            wall = r.chance(0.55f) ? pick(kMimoWall, r) : pick(kDecoPastel, r);
            frame = pick(kMimoAccent, r);
            break;
        case AR_MOTEL_MED:
            style = 0; floorH = r.range(2.9f, 3.1f); groundH = r.range(3.1f, 3.4f); bay = r.range(3.6f, 4.4f); winW = r.range(0.3f, 0.38f); winH = 0.5f; sill = 0.95f;
            wall = pick(kMedWarm, r);
            frame = r.chance(0.6f) ? vec3(0.3f, 0.2f, 0.12f) : vec3(0.15f, 0.32f, 0.26f);
            break;
        case AR_MOTEL_KEYS:
            style = 5; floorH = r.range(2.8f, 3.0f); groundH = r.range(3.0f, 3.2f); bay = r.range(3.4f, 4.2f); winW = r.range(0.36f, 0.44f); winH = 0.55f; sill = 0.9f;
            mat = MAT_WOOD_SIDING; wall = pick(kConch, r);
            frame = vec3(0.97f);
            break;
        case AR_MOTEL_INN: {
            style = 0; floorH = r.range(2.8f, 3.0f); groundH = r.range(3.0f, 3.2f); bay = r.range(3.6f, 4.2f); winW = r.range(0.42f, 0.52f); winH = 0.48f; sill = 1.0f;
            const vec3 inn[] = {vec3(0.95f, 0.88f, 0.75f), vec3(0.88f, 0.78f, 0.66f), vec3(0.98f, 0.95f, 0.88f), vec3(0.85f, 0.85f, 0.82f)};
            if (r.chance(0.35f)) { mat = MAT_BRICK; wall = vec3(1.f) * r.range(0.85f, 1.f); }
            else wall = pick(inn, r);
            frame = r.chance(0.5f) ? vec3(0.12f) : vec3(0.4f, 0.28f, 0.18f);
            break;
        }
        case AR_STRIP_MISSION:
            style = 0; floorH = r.range(4.6f, 5.4f); groundH = floorH; bay = r.range(5.5f, 7.5f); winW = r.range(0.7f, 0.8f); winH = 0.62f; sill = 0.4f;
            wall = pick(kMedWarm, r);
            frame = r.chance(0.6f) ? vec3(0.3f, 0.2f, 0.12f) : vec3(0.15f, 0.32f, 0.26f);
            break;
        case AR_STRIP_MIMO:
            style = 0; floorH = r.range(4.2f, 4.8f); groundH = floorH; bay = r.range(5.f, 6.5f); winW = r.range(0.75f, 0.85f); winH = 0.6f; sill = 0.4f;
            wall = r.chance(0.5f) ? pick(kMimoWall, r) : pick(kDecoPastel, r);
            frame = pick(kMimoAccent, r);
            break;
        case AR_STRIP_MODERN: {
            style = 0; floorH = r.range(5.4f, 6.4f); groundH = floorH; bay = r.range(6.f, 8.f); winW = r.range(0.75f, 0.85f); winH = 0.55f; sill = 0.45f;
            const vec3 eifs[] = {vec3(0.9f, 0.86f, 0.78f), vec3(0.82f, 0.78f, 0.7f), vec3(0.7f, 0.68f, 0.64f), vec3(0.95f, 0.93f, 0.88f), vec3(0.78f, 0.74f, 0.66f)};
            wall = pick(eifs, r);
            if (r.chance(0.3f)) mat = MAT_STONE, wall = vec3(0.9f, 0.86f, 0.78f);
            frame = r.chance(0.5f) ? vec3(0.15f) : vec3(0.55f, 0.57f, 0.6f);
            break;
        }
        case AR_BARN_GAMBREL: case AR_BARN_GABLE: {
            style = 4; floorH = arch == AR_BARN_GAMBREL ? r.range(4.2f, 5.0f) : r.range(4.8f, 6.2f); groundH = floorH; bay = r.range(4.f, 6.f); winW = 0.35f; winH = 0.3f; sill = 2.2f;
            mat = MAT_WOOD_SIDING;
            float p = r.f();
            wall = p < 0.5f ? vec3(0.62f, 0.16f, 0.11f) * r.range(0.85f, 1.05f) : (p < 0.65f ? vec3(0.95f) : (p < 0.9f ? vec3(0.58f, 0.55f, 0.5f) : vec3(0.3f, 0.42f, 0.3f)));
            frame = vec3(0.95f);
            break;
        }
        case AR_CHURCH_MISSION:
            style = 0; floorH = r.range(7.f, 8.f); groundH = floorH; bay = r.range(3.6f, 4.4f); winW = r.range(0.22f, 0.28f); winH = 0.55f; sill = 2.4f;
            wall = r.chance(0.5f) ? vec3(0.98f, 0.96f, 0.9f) : pick(kMedWarm, r);
            frame = vec3(0.35f, 0.22f, 0.12f);
            break;
        case AR_CHURCH_CLAPBOARD:
            style = 0; floorH = r.range(5.6f, 6.6f); groundH = floorH; bay = r.range(3.f, 3.6f); winW = r.range(0.3f, 0.36f); winH = 0.62f; sill = 1.4f;
            mat = MAT_WOOD_SIDING; wall = vec3(0.97f);
            frame = vec3(0.95f);
            break;
        case AR_CHURCH_BRICK:
            style = 0; floorH = r.range(7.f, 8.5f); groundH = floorH; bay = r.range(3.6f, 4.4f); winW = r.range(0.28f, 0.34f); winH = 0.62f; sill = 2.f;
            mat = MAT_BRICK; wall = vec3(1.f) * r.range(0.8f, 1.f);
            frame = vec3(0.25f, 0.2f, 0.16f);
            glass = vec3(0.45f, 0.4f, 0.55f);
            break;
        case AR_CHURCH_AFRAME:
            style = 0; floorH = r.range(2.8f, 3.4f); groundH = floorH; bay = r.range(3.f, 4.f); winW = 0.6f; winH = 0.5f; sill = 1.f;
            wall = r.chance(0.5f) ? vec3(0.95f) : vec3(0.8f, 0.72f, 0.62f);
            if (r.chance(0.4f)) mat = MAT_STONE;
            frame = vec3(0.2f);
            break;
        case AR_BARN_POLE: {
            style = 4; floorH = r.range(4.4f, 5.4f); groundH = floorH; bay = r.range(4.f, 6.f); winW = 0.3f; winH = 0.25f; sill = 2.5f;
            mat = MAT_CORRUGATED;
            const vec3 galv[] = {vec3(0.8f, 0.81f, 0.82f), vec3(0.55f, 0.36f, 0.26f), vec3(0.42f, 0.55f, 0.45f), vec3(0.45f, 0.52f, 0.62f), vec3(0.7f, 0.68f, 0.6f)};
            wall = pick(galv, r);
            frame = vec3(0.3f);
            break;
        }
        case AR_DECO_STREAMLINE:
            style = r.chance(0.5f) ? 2 : 6; floorH = r.range(3.0f, 3.3f); groundH = r.range(4.0f, 4.6f); bay = r.range(2.2f, 3.0f); winW = r.range(0.5f, 0.62f); winH = r.range(0.5f, 0.58f); sill = 0.95f;
            wall = r.chance(0.55f) ? pick(kDecoPastel, r) : pick(kMimoWall, r);
            frame = pick(kMimoAccent, r);
            flags |= 2u | 4u;
            break;
        case AR_DECO_MED:
            style = 0; floorH = r.range(3.1f, 3.4f); groundH = r.range(4.0f, 4.6f); bay = r.range(2.6f, 3.4f); winW = r.range(0.32f, 0.4f); winH = r.range(0.46f, 0.52f); sill = 0.8f;
            wall = pick(kMedWarm, r);
            frame = r.chance(0.6f) ? vec3(0.28f, 0.17f, 0.1f) : vec3(0.15f, 0.3f, 0.25f);
            flags |= 2u;
            break;
        case AR_DECO_MIMO:
            style = r.chance(0.55f) ? 2 : 0; floorH = r.range(2.9f, 3.1f); groundH = r.range(3.6f, 4.2f); bay = r.range(3.0f, 4.0f); winW = r.range(0.6f, 0.78f); winH = r.range(0.42f, 0.52f); sill = 1.0f;
            wall = pick(kMimoWall, r);
            frame = pick(kMimoAccent, r);
            flags |= 2u | 4u;
            break;
        case AR_WARE_SAWTOOTH:
            style = 4; floorH = r.range(6.5f, 8.f); groundH = floorH; bay = r.range(5.f, 7.f); winW = f.winW; winH = f.winH; sill = f.sillH;
            if (r.chance(0.45f)) { mat = MAT_BRICK; wall = vec3(1.f) * r.range(0.8f, 1.f); }
            else { mat = r.chance(0.5f) ? MAT_CONCRETE_PANEL : MAT_CORRUGATED; wall = pick(kIndustrialWall, r); }
            frame = vec3(0.3f);
            break;
        case AR_WARE_OFFICE:
            style = 4; floorH = r.range(7.5f, 9.5f); groundH = floorH; bay = r.range(5.f, 7.f); winW = f.winW; winH = f.winH; sill = f.sillH;
            mat = r.chance(0.6f) ? MAT_CORRUGATED : MAT_CONCRETE_PANEL;
            wall = pick(kIndustrialWall, r);
            frame = vec3(0.3f);
            break;
        case AR_WARE_BODYSHOP:
            style = 4; floorH = r.range(5.5f, 6.5f); groundH = floorH; bay = r.range(4.5f, 6.f); winW = f.winW; winH = f.winH; sill = f.sillH;
            mat = MAT_STUCCO;
            wall = r.chance(0.6f) ? pick(kOcho, r) : pick(kIndustrialWall, r);
            frame = pick(kBright, r);
            flags |= 2u;
            break;
        default: return;
    }
    // farmhouses: white, cream and weathered board whatever the type
    if (b.style == BS_FARMHOUSE) {
        const vec3 farm[] = {vec3(0.97f), vec3(0.95f, 0.92f, 0.82f), vec3(0.98f, 0.95f, 0.75f), vec3(0.82f, 0.88f, 0.8f), vec3(0.7f, 0.66f, 0.6f), vec3(0.9f, 0.9f, 0.88f)};
        wall = pick(farm, r);
        if (r.chance(0.8f)) mat = MAT_WOOD_SIDING;
    }
    // district colour: Calle Luna and the island towns paint their stucco in saturated Caribbean colours whatever the
    // style; the Canvas District paints its warehouses and walk-ups loud (brick too: painted masonry reads as stucco);
    // Sol Beach and North Porto Sol lean to deco pastels
    if (style != 1 && arch != AR_MID_OFFICE60 && arch < AR_CONDO_SLAB && (arch != AR_MID_MODERN || reg == REG_MIDTOWN)) {
        float pc = 0.f, pb = 0.f;
        const vec3* pal = nullptr;
        int np = 0;
        if (reg == REG_CALLE_LUNA || reg == REG_FLATS) { pc = 0.45f; pb = 0.3f; pal = kOcho; np = ARRAY_COUNT(kOcho); }
        else if (reg == REG_MIDTOWN) { pc = 0.5f; pb = 0.4f; pal = kBright; np = ARRAY_COUNT(kBright); }
        else if (reg == REG_KEY_TOWN || reg == REG_GULF_TOWN) { pc = 0.4f; pal = kConch; np = ARRAY_COUNT(kConch); }
        else if (reg == REG_BEACH || reg == REG_NORTH_CITY) { pc = 0.3f; pal = kDecoPastel; np = ARRAY_COUNT(kDecoPastel); }
        if (pal && r.chance(mat == MAT_BRICK ? pb : pc)) {
            wall = pal[r.next() % (u32)np];
            if (mat == MAT_BRICK) mat = MAT_STUCCO;
            if (reg == REG_MIDTOWN && r.chance(0.5f)) wall = lerp(wall, vec3(1.f), 0.25f);   // (a little chalkier)
        }
    }
    // the Craftsman bungalows of the county towns (and of some city lots): board siding in earth tones - sage, olive, brown,
    // barn red, mustard, slate, cream - with cream trim; the foursquares in brick, board or stucco (their own stream)
    {
        const int hf = houseForm(b);
        const bool town = reg == REG_LAKE_TOWN || reg == REG_HARLOW || reg == REG_FORT_CASTELL || reg == REG_REDLAND || reg == REG_FARMLAND;
        Rng cr(b.seed ^ 0xC2AF7u);
        if ((hf == HF_BUNG_SIDE || hf == HF_BUNG_AIRPLANE || (hf == HF_BUNG_HIP && cr.chance(0.5f))) && (town || cr.chance(0.45f))) {
            const vec3 earth[] = {vec3(0.68f, 0.76f, 0.6f), vec3(0.64f, 0.64f, 0.48f), vec3(0.66f, 0.52f, 0.4f), vec3(0.7f, 0.4f, 0.34f),
                                  vec3(0.92f, 0.78f, 0.48f), vec3(0.58f, 0.66f, 0.72f), vec3(0.96f, 0.92f, 0.78f)};
            mat = MAT_WOOD_SIDING;
            wall = pick(earth, cr);
            frame = cr.chance(0.7f) ? vec3(0.96f, 0.93f, 0.84f) : vec3(0.97f);
        } else if (hf == HF_BUNG_MISSION) {
            // (smooth stucco in the warm Mediterranean tones, a pastel or white; dark wood or white window frames)
            mat = MAT_STUCCO;
            const float p = cr.f();
            wall = p < 0.5f ? pick(kMedWarm, cr) : (p < 0.8f ? pick(kPastels, cr) : vec3(0.97f, 0.96f, 0.93f));
            frame = cr.chance(0.5f) ? vec3(0.35f, 0.22f, 0.12f) : vec3(0.96f);
        } else if (hf == HF_FOURSQUARE) {
            const float p = cr.f();
            if (p < (town ? 0.4f : 0.25f)) {
                mat = MAT_BRICK;
                wall = cr.chance(0.7f) ? vec3(1.f) * cr.range(0.85f, 1.02f) : vec3(1.f, 0.9f, 0.8f);
            } else if (p < 0.8f) {
                mat = MAT_WOOD_SIDING;
                wall = cr.chance(0.5f) ? pick(kVictorian, cr) : pick(kSuburb, cr);
            } else {
                mat = MAT_STUCCO;
                wall = pick(kSuburb, cr);
            }
            frame = vec3(0.97f);
        }
    }
    // per-building drift: colour and wear (older fabric a little darker and greyer)
    if (mat != MAT_BRICK) {
        wall = jitter(wall, r);
        if (kArch[arch].old) wall = lerp(wall, vec3(dot(wall, vec3(0.33f))), r.range(0.f, 0.15f)) * r.range(0.9f, 1.f);
    }
    f.floorH = floorH;
    f.groundH = groundH;
    f.bayW = bay;
    f.winW = winW;
    f.winH = winH;
    f.sillH = sill;
    f.style = (float)style;
    f.wallColor = rgb8(Saturate(wall.x), Saturate(wall.y), Saturate(wall.z));
    f.frameColor = rgb8(Saturate(frame.x), Saturate(frame.y), Saturate(frame.z));
    // (a storefront's shop glass: the facade shader takes it towards clear - renderer PR #35)
    f.glassColor = rgb8(glass.x, glass.y, glass.z);
    f.flags = flags;
    f.wallLayer = (float)mat;
    f.litFrac = r.range(0.35f, 0.75f);
    f.roomDepth = r.range(4.f, 7.f);
}

// Towers keep their massing generator (buildmesh.cpp) and their height; the facade gets a look from the district's
// palette: blue, teal, silver, bronze or dark glass curtain walls, white ribbon-window slabs of the seventies, precast
// punched facades, dark granite; on Collins white and pastel balcony towers among the glass
void towerLook(Building& b, FacadeGPU& f, Rng& r) {
    enum { L_BLUE, L_TEAL, L_SILVER, L_BRONZE, L_DARK, L_RIBBON, L_PRECAST, L_GRANITE, L_BALCONY, L_PASTEL, L_N };
    float w[L_N] = {2.f, 1.5f, 1.5f, 1.f, 1.f, 2.f, 1.5f, 1.f, 0.f, 0.f};
    if (b.region == REG_FINANCIAL) w[L_BLUE] = 3.f, w[L_SILVER] = 2.5f, w[L_DARK] = 1.5f, w[L_PRECAST] = 0.8f, w[L_RIBBON] = 1.f;
    if (b.region == REG_BEACH || b.region == REG_KEY_CORAL) {
        for (float& x : w) x = 0.f;
        w[L_BALCONY] = 3.f, w[L_PASTEL] = 2.f, w[L_BLUE] = 2.f, w[L_TEAL] = 1.f, w[L_RIBBON] = 1.f;
    }
    int look = r.weighted(w, L_N);
    vec3 wall(0.9f), frame(0.2f), glass(0.5f, 0.6f, 0.7f);
    MaterialId mat = MAT_CONCRETE_PANEL;
    int style = 1;
    float bay = r.range(1.5f, 2.3f), winW = r.range(0.55f, 0.75f), winH = r.range(0.55f, 0.72f);
    switch (look) {
        case L_BLUE: glass = vec3(0.42f, 0.6f, 0.82f) * r.range(0.9f, 1.05f); frame = vec3(0.72f, 0.75f, 0.8f); wall = vec3(0.85f); break;
        case L_TEAL: glass = vec3(0.38f, 0.7f, 0.66f) * r.range(0.9f, 1.05f); frame = vec3(0.12f, 0.14f, 0.15f); wall = vec3(0.8f); break;
        case L_SILVER: glass = vec3(0.66f, 0.7f, 0.74f); frame = vec3(0.78f, 0.8f, 0.83f); wall = vec3(0.9f); break;
        case L_BRONZE: glass = vec3(0.72f, 0.56f, 0.38f); frame = vec3(0.32f, 0.22f, 0.14f); wall = vec3(0.6f, 0.5f, 0.4f); break;
        case L_DARK: glass = vec3(0.3f, 0.33f, 0.36f); frame = vec3(0.08f); wall = vec3(0.35f); break;
        case L_RIBBON:
            style = 2; bay = r.range(2.6f, 3.4f); mat = MAT_CONCRETE_PANEL; wall = vec3(0.96f, 0.96f, 0.94f) * r.range(0.92f, 1.f);
            glass = r.chance(0.5f) ? vec3(0.45f, 0.55f, 0.6f) : vec3(0.55f, 0.48f, 0.4f); frame = vec3(0.15f);
            break;
        case L_PRECAST:
            style = 0; bay = r.range(2.4f, 3.2f); mat = MAT_CONCRETE_PANEL; wall = r.chance(0.5f) ? vec3(0.95f, 0.9f, 0.8f) : vec3(0.88f, 0.84f, 0.76f);
            glass = kGlass[r.next() % ARRAY_COUNT(kGlass)]; frame = r.chance(0.5f) ? vec3(0.15f) : vec3(0.75f); winW = r.range(0.5f, 0.62f); winH = r.range(0.55f, 0.65f);
            break;
        case L_GRANITE:
            style = r.chance(0.5f) ? 2 : 0; bay = r.range(2.4f, 3.2f); mat = MAT_STONE; wall = vec3(0.55f, 0.5f, 0.48f) * r.range(0.8f, 1.f);
            glass = vec3(0.3f, 0.34f, 0.36f); frame = vec3(0.08f);
            break;
        case L_BALCONY:
            style = 3; bay = r.range(3.6f, 5.2f); winW = r.range(0.72f, 0.85f); winH = 0.8f; mat = MAT_STUCCO; wall = vec3(0.97f, 0.97f, 0.95f);
            glass = vec3(0.5f, 0.65f, 0.75f); frame = vec3(0.85f, 0.87f, 0.9f);
            break;
        case L_PASTEL:
            style = r.chance(0.5f) ? 3 : 0; bay = r.range(3.2f, 4.6f); mat = MAT_STUCCO; wall = pick(kDecoPastel, r);
            glass = vec3(0.45f, 0.62f, 0.75f); frame = vec3(0.9f);
            break;
    }
    f.style = (float)style;
    f.bayW = bay;
    if (style != 1) f.winW = winW, f.winH = winH;
    f.wallColor = rgb8(Saturate(wall.x), Saturate(wall.y), Saturate(wall.z));
    f.frameColor = rgb8(Saturate(frame.x), Saturate(frame.y), Saturate(frame.z));
    f.glassColor = rgb8(Saturate(glass.x), Saturate(glass.y), Saturate(glass.z));
    f.wallLayer = (float)mat;
}

// Secondary cladding of a podium / base / office front block (indices appended to the facade table)
u32 podiumFacade(BuildingSet& bs, const Building& b, const FacadeGPU& body, u8 arch, Rng& r, bool store) {
    FacadeGPU g = body;
    g.seed = b.seed * 747796405u + 2891336453u;
    vec3 w(0.85f), fr(0.12f);
    MaterialId m = MAT_CONCRETE_PANEL;
    switch (arch) {
        case AR_CONDO_PODIUM:
        case AR_CONDO_GLASS:
        case AR_CONDO_SLAB:
            // screened parking decks: long ribbons (the dark openings read as the deck slots), concrete or metal screens
            g.style = 2.f;
            g.floorH = 3.0f;
            g.groundH = body.groundH;
            g.bayW = r.range(5.f, 7.f);
            m = r.chance(0.6f) ? MAT_CONCRETE : MAT_METAL_BRUSHED;
            w = r.chance(0.6f) ? vec3(0.9f, 0.9f, 0.88f) : pick(kMimoAccent, r) * 0.9f;
            fr = vec3(0.2f);
            break;
        case AR_MID_MODERN:
        case AR_MID_OFFICE60:
            g.style = 0.f;
            g.bayW = r.range(3.2f, 4.2f);
            g.winW = 0.8f;
            g.winH = 0.7f;
            m = r.chance(0.5f) ? MAT_STONE : MAT_MARBLE;
            w = vec3(0.8f, 0.78f, 0.74f) * r.range(0.85f, 1.f);
            fr = vec3(0.1f);
            break;
        case AR_WARE_OFFICE:
            g.style = r.chance(0.5f) ? 2.f : 0.f;
            g.floorH = 3.4f;
            g.groundH = 3.8f;
            g.bayW = r.range(2.6f, 3.4f);
            g.winW = 0.6f;
            g.winH = 0.5f;
            g.sillH = 0.9f;
            m = r.chance(0.5f) ? MAT_STUCCO : MAT_BRICK;
            w = m == MAT_BRICK ? vec3(1.f) : pick(kMimoWall, r);
            fr = vec3(0.15f);
            break;
        default:
            // a second building on the lot (split lots): its own colour, same family
            m = (MaterialId)(int)body.wallLayer;
            w = vec3(unpackRGBA8(body.wallColor).x, unpackRGBA8(body.wallColor).y, unpackRGBA8(body.wallColor).z);
            w = jitter(w, r, 0.08f) * r.range(0.85f, 1.05f);
            if (m == MAT_BRICK && r.chance(0.5f)) { m = MAT_STUCCO; w = pick(kPastels, r); }
            fr = vec3(unpackRGBA8(body.frameColor).x, unpackRGBA8(body.frameColor).y, unpackRGBA8(body.frameColor).z);
            g.bayW = body.bayW * r.range(0.85f, 1.15f);
            break;
    }
    g.wallColor = rgb8(Saturate(w.x), Saturate(w.y), Saturate(w.z));
    g.frameColor = rgb8(fr.x, fr.y, fr.z);
    g.wallLayer = (float)m;
    g.flags = (body.flags & ~1u) | (store ? 1u : 0u);
    bs.facades.push_back(g);
    return (u32)bs.facades.size() - 1;
}

}  // namespace blockstyle

// ------------------------------------------------------------------------------------------------ the pass
void BuildingSet::restyleBlocks(WorldMap& map, const RoadNetwork& roads) {
    using namespace blockstyle;
    double t0 = TimeSeconds();
    int counts[AR_COUNT] = {};
    int restyled = 0;
    int prevFace = -1;
    u8 prevArch = AR_NONE, prevMass = MK_BOX;
    int prevFloors = -1;
    for (size_t i = 0; i < buildings.size(); i++) {
        Building& b = buildings[i];
        if (b.siteElem >= 0 || b.interior >= 0 || b.face < 0 || b.facade >= facades.size()) {
            prevFace = -1;
            continue;
        }
        bool sameFace = b.face == prevFace;
        prevFace = b.face;
        FacadeGPU& f = facades[b.facade];
        bool store = (f.flags & 1u) != 0;
        if (b.style == BS_TOWER) {
            // towers: a facade look from the district palette (massing and height stay the generator's)
            Rng tr(b.seed ^ 0x70E4u);
            towerLook(b, f, tr);
            b.archFlags &= 0x7fu;
            restyled++;
            continue;
        }
        // context: main road frontage, corner lot
        bool main = (b.archFlags & 0x80u) != 0;   // (set at lot time: frontage on an avenue or boulevard)
        b.archFlags &= 0x7fu;
        bool cl = crossStreetAt(roads, b, -1.f), cr = crossStreetAt(roads, b, 1.f);
        bool corner = cl || cr;
        if (corner) b.archFlags |= ABF_CORNER | (cl ? ABF_CORNER_LEFT : 0);
        if (b.style == BS_GASSTATION) {
            // gas stations: the kiosk dressed to the canopy kind buildmesh.cpp draws (same seeded pick): cream stucco
            // under the tile hip, white with a coloured frame under the sixties butterfly
            u32 kh = hash32(b.seed ^ 0x6A5C0u) % 100u;
            Rng gr(b.seed ^ 0x6A5C1u);
            if (kh >= 65u && kh < 85u) {
                vec3 c = pick(kMedWarm, gr);
                f.wallColor = rgb8(c.x, c.y, c.z);
                f.wallLayer = (float)MAT_STUCCO;
                f.frameColor = rgb8(0.35f, 0.22f, 0.12f);
            } else if (kh >= 40u && kh < 65u) {
                vec3 c = pick(kMimoAccent, gr);
                f.wallColor = rgb8(0.97f, 0.97f, 0.95f);
                f.frameColor = rgb8(c.x, c.y, c.z);
            }
            continue;
        }
        Pick pal[16];
        int np = palette(b.region, b.style, main, corner, pal);
        if (np == 0) continue;
        // block-face character: one archetype of the palette is favoured along this street side, one is avoided
        u32 fh = hash32((u32)b.face * 2654435761u + 0xB10Cu);
        int fav = (int)(fh % (u32)np), avoid = (int)((fh >> 8) % (u32)np);
        Rng ar(b.seed ^ 0xA2C4E7u);
        float w[16];
        for (int k = 0; k < np; k++) {
            w[k] = pal[k].w;
            if (k == fav) w[k] *= 2.6f;
            else if (k == avoid && np > 2) w[k] *= 0.3f;
            // the neighbour's archetype is rarely repeated right next door
            if (sameFace && pal[k].arch == prevArch && prevArch != AR_NONE) w[k] *= 0.25f;
        }
        u8 arch = pal[ar.weighted(w, np)].arch;
        if (arch == AR_NONE) {
            prevArch = AR_NONE;
            continue;
        }
        const ArchInfo& ai = kArch[arch];
        // floors: the archetype's range within the district's height (towers of the palette keep the lot's height)
        const RegionInfo& ri = regionInfo((Region)b.region);
        int floors = b.floors;
        int lo = ai.minF, hi = ai.maxF;
        if (b.style == BS_SHOPS || b.style == BS_MIDRISE) hi = Min(hi, Max(lo, (int)ri.maxFloors + (b.style == BS_MIDRISE ? 1 : 0)));
        if (b.style == BS_DECO) hi = Min(hi, Max(lo, (int)b.floors + 1));
        if (b.style == BS_CONDO) {
            // condos keep their floor count roughly (the skyline of the district), the archetype bends it
            lo = Max(lo, (int)(b.floors * 0.7f));
            hi = Max(lo, Min(hi, (int)(b.floors * 1.35f) + 2));
            if (b.region == REG_KEY_CORAL) lo = 4, hi = 9;
        }
        if (b.style == BS_HOUSE || b.style == BS_VILLA || b.style == BS_WAREHOUSE || b.style == BS_FACTORY || b.style == BS_FARMHOUSE || b.style == BS_BARN)
            floors = Clamp(floors, lo, hi);
        else floors = ar.irange(lo, Max(lo, hi));
        // neighbours of the same archetype differ in height
        if (sameFace && arch == prevArch && floors == prevFloors && hi > lo) floors = floors < hi ? floors + 1 : floors - 1;
        // massing
        Pick ms[12];
        int nm = massings(arch, store, corner, 2.f * b.hx, 2.f * b.hy, ms);
        // (the temple-front conch house - a gable-fronted bay with the porch beside it - in the towns, not on the farms)
        if (b.style == BS_HOUSE && arch == AR_HOUSE_CONCH && nm < 12) ms[nm++] = {MK_STEP_FRONT, 1.3f};
        float mw[12];
        for (int k = 0; k < nm; k++) mw[k] = ms[k].w * ((sameFace && ms[k].arch == prevMass) ? 0.5f : 1.f);
        u8 mass = nm > 0 ? ms[ar.weighted(mw, nm)].arch : MK_BOX;
        if (b.style == BS_HOUSE && arch == AR_HOUSE_SPLIT) floors = 2;
        b.arch = arch;
        b.massing = mass;
        b.roofForm = roofFor(arch, mass, ar);
        if (ai.old) b.archFlags |= ABF_OLD;
        if ((arch == AR_SHOP_BODEGA || arch == AR_MID_LOFT || arch == AR_WARE_BODYSHOP || arch == AR_SHOP_TAXPAYER || arch == AR_WARE_SAWTOOTH) &&
            ar.chance(b.region == REG_MIDTOWN ? 0.75f : (b.region == REG_CALLE_LUNA || b.region == REG_FLATS ? 0.45f : 0.2f)))
            b.archFlags |= ABF_MURAL;
        if (ar.chance(0.5f)) b.archFlags |= ABF_ACCENT;
        // roof colour of pitched roofs
        vec3 rc(1.f);
        switch (b.roofForm) {
            case RFM_TILE_HIP: case RFM_TILE_PENT: rc = pick(kTileRoof, ar); break;
            case RFM_METAL_GABLE: rc = pick(kMetalRoof, ar); break;
            default: rc = ((arch >= AR_HOUSE_RANCH && arch <= AR_HOUSE_CONCH) || arch == AR_HOUSE_CBS || arch == AR_HOUSE_VICTORIAN || arch == AR_HOUSE_RAISED) ? pick(kShingle, ar)
                                                                                                                                                  : vec3(1.f);
                break;
        }
        b.roofTint = rgb8(Saturate(rc.x * 0.95f), Saturate(rc.y * 0.95f), Saturate(rc.z * 0.95f));
        // facade record for the archetype; podium / office front cladding
        facadeFor(b, f, arch, ar, store);
        bool wantPodium = mass == MK_PODIUM_SLAB || arch == AR_WARE_OFFICE || ((mass == MK_SPLIT || mass == MK_STEP_FRONT) && b.style != BS_HOUSE);
        b.facade2 = wantPodium ? podiumFacade(*this, b, f, arch, ar, store) : 0xffffffffu;
        // heights from the new facade grid
        b.floors = (u16)Max(1, floors);
        if (b.style == BS_WAREHOUSE || b.style == BS_FACTORY || b.style == BS_BARN) b.height = f.floorH;
        else b.height = f.groundH + (b.floors - 1) * f.floorH;
        b.roof = (b.roofForm == RFM_TILE_HIP) ? ROOF_HIP : (b.roofForm == RFM_METAL_GABLE ? ROOF_GABLE : b.roof);
        if (b.style == BS_MOTEL || b.style == BS_STRIPMALL)
            b.roof = b.roofForm == RFM_TILE_HIP ? ROOF_HIP : (b.roofForm == RFM_METAL_GABLE ? ROOF_GABLE : ROOF_FLAT);
        // (the Keys motels: a tin hip roof on two in five - massing.cpp reads b.roof under the metal roof form)
        if (arch == AR_MOTEL_KEYS && hashToFloat(hash32(b.seed ^ 0x6E7Bu)) < 0.4f) b.roof = ROOF_HIP;
        if (b.style == BS_HOUSE || b.style == BS_VILLA || b.style == BS_FARMHOUSE) {
            // houses: hip or gable by type (buildmesh.cpp reads the archetype for the rest)
            b.roof = (arch == AR_HOUSE_BUNGALOW || arch == AR_HOUSE_CONCH || arch == AR_HOUSE_VICTORIAN || arch == AR_HOUSE_TRAILER) ? ROOF_GABLE
                                                                                                     : (arch == AR_HOUSE_MIMO || arch == AR_VILLA_MODERN ? ROOF_FLAT : ROOF_HIP);
            if (arch == AR_HOUSE_CONCH && ar.chance(0.4f)) b.roof = ROOF_HIP;
            if (arch == AR_HOUSE_TWO && ar.chance(0.4f)) b.roof = ROOF_GABLE;
            // side-gabled ranches and block houses: a third of the ranches, a fifth of the block houses (massing.cpp)
            if (arch == AR_HOUSE_RANCH && hashToFloat(hash32(b.seed ^ 0x6AB1Eu)) < 0.33f) b.roof = ROOF_GABLE;
            if (arch == AR_HOUSE_CBS && hashToFloat(hash32(b.seed ^ 0x6AB1Fu)) < 0.2f) b.roof = ROOF_GABLE;
            // Mediterranean Revival with a flat roof behind a parapet and a tile pent on a quarter (not with the corner tower)
            if (arch == AR_HOUSE_MED && mass != MK_CORNER_TOWER && hashToFloat(hash32(b.seed ^ 0x6AB20u)) < 0.25f) b.roof = ROOF_FLAT;
            // the hipped bungalows and the foursquares (houseForm: massing.cpp)
            const int hf = houseForm(b);
            if (hf == HF_BUNG_HIP || hf == HF_FOURSQUARE) b.roof = ROOF_HIP;
            if (hf == HF_BUNG_MISSION) b.roof = ROOF_FLAT;
        }
        prevArch = arch;
        prevMass = mass;
        prevFloors = b.floors;
        counts[arch]++;
        restyled++;
    }
    std::string list;
    for (int a = 1; a < AR_COUNT; a++)
        if (counts[a]) list += StrFormat("%s%s %d", list.empty() ? "" : ", ", kArch[a].name, counts[a]);
    LOG("Buildings: %d restyled by district palettes in %.1f ms (%s)", restyled, (TimeSeconds() - t0) * 1000.0, list.c_str());
    (void)map;
}

// ------------------------------------------------------------------------------------------------ block infill
namespace blockstyle {

// How much of a district's block middles fills up, and with what: back houses, garage rows, sheds, rear parking, yards
struct InfillMix {
    float p;                                  // chance a lot's back gets something
    float park, garage, shed, cottage, yard;  // relative weights
};
InfillMix infillMix(int reg, int style) {
    bool commercial = style == BS_SHOPS || style == BS_MIDRISE || style == BS_DECO || style == BS_CONDO || style == BS_MOTEL || style == BS_STRIPMALL;
    bool industrial = style == BS_WAREHOUSE || style == BS_FACTORY;
    bool home = style == BS_HOUSE || style == BS_VILLA;
    switch (reg) {
        case REG_CALLE_LUNA: case REG_NORTH_CITY: case REG_MIDTOWN: case REG_FLATS: case REG_FORT_CASTELL: case REG_BEACH: case REG_KEY_TOWN:
        case REG_LAKE_TOWN: case REG_HARLOW: case REG_DOWNTOWN:
            if (commercial) return {0.85f, 4.5f, 2.f, 1.5f, 1.f, 1.f};
            if (industrial) return {0.8f, 3.f, 0.5f, 3.f, 0.f, 1.5f};
            if (home) return {0.7f, 0.5f, 1.2f, 0.8f, 2.5f, 4.f};
            return {0.5f, 2.f, 1.f, 1.f, 0.5f, 2.f};
        case REG_SUBURBS: case REG_GROVE: case REG_KEY_CORAL: case REG_BAY_ISLAND:
            if (home) return {0.45f, 0.f, 0.f, 1.5f, 0.6f, 5.f};
            if (commercial) return {0.6f, 3.f, 0.f, 1.f, 0.f, 2.f};
            return {0.3f, 1.f, 0.f, 1.f, 0.f, 2.f};
        case REG_GULF_TOWN: case REG_KEYS: return {0.5f, 0.f, 0.f, 2.f, 1.f, 3.f};
        default: return {0.f, 0, 0, 0, 0, 0};
    }
}

}  // namespace blockstyle

void BuildingSet::infillBlocks(WorldMap& map, const std::function<bool(vec2, vec2, float, float)>& free,
                               const std::function<void(vec2, vec2, float, float)>& claim, const std::function<bool(vec2)>& nearStreet) {
    using namespace blockstyle;
    double t0 = TimeSeconds();
    std::vector<Building> added;
    int nPark = 0, nYard = 0, nStreetSide = 0, counts[3] = {};
    size_t n0 = buildings.size();
    for (size_t i = 0; i < n0; i++) {
        const Building b = buildings[i];
        if (b.siteElem >= 0 || b.face < 0 || b.lotHy <= 0.f) continue;
        InfillMix mix = infillMix(b.region, b.style);
        Rng r(b.seed ^ 0x1AF111u);
        if (!r.chance(mix.p)) continue;
        // up to two plots behind the lot's back edge, as deep as the block allows
        float used = 0.f;
        for (int plot = 0; plot < 2; plot++) {
            vec2 back = b.lotC - b.front * (b.lotHy + used);
            float W = Min(2.f * b.lotHx - 1.2f, 28.f);
            float D = 0.f;
            vec2 c;
            const float depths[] = {22.f, 16.f, 12.f, 9.f, 6.5f};
            // (a deeper neighbour's back corner can stand right beside the plot: step back a little further then)
            for (float gap : {0.5f, 2.5f})
                if (D <= 0.f)
                    for (float dd : depths) {
                        vec2 cc = back - b.front * (dd * 0.5f + gap);
                        if (free(cc, b.ax, W * 0.5f, dd * 0.5f)) {
                            D = dd;
                            c = cc;
                            used += gap - 0.4f;
                            break;
                        }
                    }
            if (D <= 0.f) break;
            claim(c, b.ax, W * 0.5f, D * 0.5f);
            used += D + 0.4f;
            bool home = b.style == BS_HOUSE || b.style == BS_VILLA;
            float wt[5] = {mix.park * (W >= 10.f && D >= 9.f ? 1.f : 0.f), mix.garage * (W >= 9.f ? 1.f : 0.f), mix.shed, mix.cottage * (D >= 9.f && W >= 8.f ? 1.f : 0.f),
                           mix.yard};
            if (plot == 1) wt[0] *= 0.5f, wt[3] *= 1.5f;
            int kind = r.weighted(wt, 5);
            if (wt[kind] <= 0.f) kind = 4;
            auto groundZ = [&](vec2 cc, vec2 ax, float hx, float hy) {
                float z = -1e9f;
                vec2 ay = perp(ax);
                for (int sx = -1; sx <= 1; sx++)
                    for (int sy = -1; sy <= 1; sy++) {
                        vec2 q = cc + ax * (sx * hx) + ay * (sy * hy);
                        z = Max(z, map.heightAt(q.x, q.y));
                    }
                return z;
            };
            auto yard = [&](u8 k, u32 salt) {
                OpenLot ol;
                ol.c = c;
                ol.ax = b.ax;
                ol.hx = W * 0.5f;
                ol.hy = D * 0.5f;
                ol.front = b.front;
                ol.z = map.heightAt(c.x, c.y);
                ol.kind = k;
                ol.region = b.region;
                ol.seed = hash32(b.seed ^ salt ^ (u32)plot * 0x9E37u);
                ol.home = home ? 1 : 0;
                openLots.push_back(ol);
            };
            if (kind == 0) {
                yard(OL_PARKING, 0x0BE11u);
                nPark++;
                continue;
            }
            if (kind == 4) {
                // a house's back yard; behind shops and sheds a paved service yard
                yard(home || r.chance(0.35f) ? OL_YARD : OL_SERVICE, 0x0BE13u);
                nYard++;
                continue;
            }
            // a building at the back of the plot (its door toward the yard and the street building)
            Building rb;
            rb.seed = hash32(b.seed ^ 0x5EA4B1u ^ (u32)plot * 0x51u);
            Rng br(rb.seed);
            rb.style = BS_GARAGE;
            rb.region = b.region;
            rb.ax = b.ax;
            rb.front = b.front;
            rb.lotKind = 0;
            rb.face = -1;
            float hx, hy;
            u8 arch;
            if (kind == 1) {
                // (no deeper than the plot: a garage row used to stand out of a shallow plot into the house lot beside it)
                arch = AR_REAR_GARAGE;
                hx = Min(W * 0.5f - 0.6f, br.range(4.5f, 11.f));
                hy = Min(D * 0.5f - 0.6f, br.range(3.f, 3.6f));
                if (hy < 2.6f) hy = 0.f;
            }
            else if (kind == 2) { arch = AR_REAR_SHED; hx = Min(W * 0.5f - 0.6f, br.range(2.2f, home ? 3.2f : 7.f)); hy = Min(D * 0.5f - 0.6f, br.range(1.8f, 4.5f)); }
            else { arch = AR_REAR_COTTAGE; hx = Min(W * 0.5f - 0.8f, br.range(3.5f, 5.2f)); hy = Min(D * 0.5f - 1.5f, br.range(3.f, 4.4f)); }
            if (hx < 1.6f || hy < 1.5f) {
                yard(home ? OL_YARD : OL_SERVICE, 0x0BE14u);
                continue;
            }
            // at the far back, offset along the street
            float slide = br.range(-1.f, 1.f) * Max(0.f, W * 0.5f - hx - 0.6f);
            rb.c = c - b.front * (D * 0.5f - hy - 0.5f) + b.ax * slide;
            // (not within a frontage's reach of a street: a block's back can be another street's front)
            bool streetSide = nearStreet(rb.c);
            for (int sx = -1; sx <= 1 && !streetSide; sx += 2)
                for (int sy = -1; sy <= 1 && !streetSide; sy += 2) streetSide = nearStreet(rb.c + b.ax * (sx * hx) + perp(b.ax) * (sy * hy));
            if (streetSide) {
                yard(home ? OL_YARD : OL_SERVICE, 0x0BE15u);
                nStreetSide++;
                continue;
            }
            rb.hx = hx;
            rb.hy = hy;
            rb.lotC = c;
            rb.lotHx = W * 0.5f;
            rb.lotHy = D * 0.5f;
            rb.arch = arch;
            rb.massing = MK_BOX;
            rb.archFlags = kArch[arch].old ? ABF_OLD : 0;
            rb.floors = (u16)(arch == AR_REAR_COTTAGE ? br.irange(1, 2) : 1);
            rb.roofColor = rgb8(0.9f, 0.9f, 0.9f);
            // facade record
            FacadeGPU f = {};
            f.seed = rb.seed;
            f.signIndex = (float)(br.next() % 512);
            f.litFrac = br.range(0.3f, 0.7f);
            f.roomDepth = br.range(3.f, 5.f);
            vec3 wall, frame(0.95f), glass = kGlass[br.next() % ARRAY_COUNT(kGlass)];
            MaterialId mat = MAT_STUCCO;
            if (arch == AR_REAR_COTTAGE) {
                f.style = 5.f; f.floorH = 2.9f; f.groundH = 3.0f; f.bayW = br.range(3.0f, 3.8f); f.winW = 0.38f; f.winH = 0.5f; f.sillH = 0.95f; f.flags = 8u;
                wall = br.chance(0.4f) ? pick(kConch, br) : pick(kSuburb, br);
                if (br.chance(0.35f)) mat = MAT_WOOD_SIDING;
                rb.roofForm = br.chance(0.5f) ? RFM_TILE_HIP : (br.chance(0.5f) ? RFM_METAL_GABLE : RFM_EAVE);
            } else if (arch == AR_REAR_GARAGE) {
                f.style = 4.f; f.floorH = br.range(2.8f, 3.2f); f.groundH = f.floorH; f.bayW = 3.2f; f.winW = 0.5f; f.winH = 0.25f; f.sillH = 1.8f;
                wall = br.chance(0.5f) ? pick(kIndustrialWall, br) : pick(kPastels, br);
                mat = br.chance(0.3f) ? MAT_BRICK : (br.chance(0.4f) ? MAT_CONCRETE_PANEL : MAT_STUCCO);
                if (mat == MAT_BRICK) wall = vec3(1.f);
                frame = vec3(0.3f);
                rb.roofForm = br.chance(0.7f) ? RFM_PARAPET : RFM_METAL_GABLE;
            } else {
                f.style = 4.f; f.floorH = br.range(2.6f, hx > 4.f ? 4.5f : 3.f); f.groundH = f.floorH; f.bayW = br.range(3.f, 4.5f); f.winW = 0.5f; f.winH = 0.25f; f.sillH = 1.6f;
                mat = br.chance(0.5f) ? MAT_CORRUGATED : MAT_WOOD_SIDING;
                wall = mat == MAT_CORRUGATED ? pick(kIndustrialWall, br) : (br.chance(0.5f) ? vec3(0.75f, 0.65f, 0.55f) : pick(kConch, br) * 0.9f);
                frame = vec3(0.3f);
                rb.roofForm = br.chance(0.6f) ? RFM_METAL_GABLE : RFM_PARAPET;
            }
            wall = jitter(wall, br);
            f.wallColor = rgb8(Saturate(wall.x), Saturate(wall.y), Saturate(wall.z));
            f.frameColor = rgb8(frame.x, frame.y, frame.z);
            f.glassColor = rgb8(glass.x, glass.y, glass.z);
            f.wallLayer = (float)mat;
            rb.facade = (u32)facades.size();
            facades.push_back(f);
            rb.height = f.groundH + (rb.floors - 1) * f.floorH;
            vec3 rc = rb.roofForm == RFM_TILE_HIP ? pick(kTileRoof, br) : (rb.roofForm == RFM_METAL_GABLE ? pick(kMetalRoof, br) : vec3(1.f));
            rb.roofTint = rgb8(Saturate(rc.x * 0.95f), Saturate(rc.y * 0.95f), Saturate(rc.z * 0.95f));
            rb.roof = rb.roofForm == RFM_TILE_HIP ? ROOF_HIP : (rb.roofForm == RFM_METAL_GABLE ? ROOF_GABLE : ROOF_FLAT);
            rb.baseZ = groundZ(rb.c, rb.ax, rb.hx, rb.hy) + 0.15f;
            added.push_back(rb);
            counts[kind - 1]++;
            // the rest of the plot: the yard in front of it
            yard(home || arch == AR_REAR_COTTAGE ? OL_YARD : OL_SERVICE, 0x0BE12u);
        }
    }
    // (the SkyLine viaduct corridor and its stations stay clear: transit.cpp prunes like the street lots)
    size_t before = added.size();
    transitPruneBuildings(added);
    buildings.insert(buildings.end(), added.begin(), added.end());
    LOG("Buildings: block infill %zu back buildings (garage rows %d, sheds %d, back houses %d, %zu in the viaduct corridor dropped, %d near a street left as "
        "yards), %d rear parking lots, %d yards in %.1f ms",
        added.size(), counts[0], counts[1], counts[2], before - added.size(), nStreetSide, nPark, nYard, (TimeSeconds() - t0) * 1000.0);
}

// The bare street frontage left between the lots. In the towns a block side often had three or four houses with open
// ground between them, and the older city has a gap here and there. Each gap wide enough for a lot becomes an open lot
// of the district's kind: a vacant lot (rough grass, sometimes the slab of a house long gone, a for-sale board), a small
// street parking lot (stalls, a low wall and a sign at the sidewalk), a side yard (lawn, a fence, trees) or, on the
// industrial streets, a paved yard behind a block wall. Open lots only: no building is added or moved, and some of the
// gaps stay bare ground.
namespace blockstyle {

struct FrontMix {
    float p;                              // share of the gaps that get a lot
    float vacant, park, yard, service;    // kind weights
    float setback, dMin, dMax;            // from the sidewalk; depth range
};

FrontMix frontMix(int reg) {
    switch (reg) {
        case REG_CALLE_LUNA: case REG_NORTH_CITY: case REG_MIDTOWN: return {0.8f, 0.5f, 0.33f, 0.12f, 0.05f, 2.f, 16.f, 24.f};
        case REG_FLATS: return {0.8f, 0.45f, 0.22f, 0.08f, 0.25f, 2.f, 16.f, 24.f};   // (the industrial streets: paved yards behind block walls)
        case REG_LAKE_TOWN: case REG_HARLOW: return {0.85f, 0.42f, 0.13f, 0.45f, 0.f, 3.f, 18.f, 26.f};
        case REG_FORT_CASTELL: return {0.85f, 0.4f, 0.15f, 0.3f, 0.15f, 3.f, 18.f, 26.f};
        case REG_KEY_TOWN: case REG_GULF_TOWN: return {0.8f, 0.55f, 0.15f, 0.3f, 0.f, 3.f, 16.f, 24.f};
        default: return {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    }
}

}  // namespace blockstyle

void BuildingSet::fillFrontage(WorldMap& map, const RoadNetwork& roads, const std::function<bool(vec2, vec2, float, float)>& free,
                               const std::function<void(vec2, vec2, float, float)>& claim) {
    using namespace blockstyle;
    double t0 = TimeSeconds();
    int counts[4] = {}, gaps = 0;
    std::vector<OpenLot> fill;
    std::vector<int> fillKind;
    for (size_t ei = 0; ei < roads.edges.size(); ei++) {
        const RoadEdge& e = roads.edges[ei];
        if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP || e.cls == RC_DIRT || e.cls == RC_RURAL) continue;
        if (e.flags & (RF_BRIDGE | RF_ELEVATED | RF_NOSIDEWALK)) continue;
        for (int side = -1; side <= 1; side += 2) {
            Rng rng(hash32(e.seed ^ (side > 0 ? 0xF1A7E1u : 0xF1A7E2u)));
            float s = e.cut0 + 4.f;
            const float limit = e.length - e.cut1 - 4.f;
            while (s + 10.f <= limit) {
                vec3 p3 = e.posAt(s);
                const FrontMix fm = frontMix(map.regionAt(p3.x, p3.y));
                if (fm.p <= 0.f) {
                    s += 8.f;
                    continue;
                }
                const float d = rng.range(fm.dMin, fm.dMax);
                const float off = e.halfWidth + e.sidewalk + fm.setback;
                // a free plot 10 m along the street starting here, widened 2 m at a time while it stays free (up to 26 m)
                float W = 0.f;
                vec2 c, ax, out;
                for (float w = 10.f; w <= Min(26.f, limit - s); w += 2.f) {
                    float sc = s + w * 0.5f;
                    vec3 C = e.posAt(sc);
                    vec2 t2 = normalize(e.tangentAt(sc).xy());
                    vec2 o = vec2(t2.y, -t2.x) * (float)side;
                    vec2 cc = C.xy() + o * (off + d * 0.5f);
                    if (frontMix(map.regionAt(cc.x, cc.y)).p <= 0.f || !free(cc, t2, w * 0.5f, d * 0.5f)) break;
                    W = w;
                    c = cc;
                    ax = t2;
                    out = o;
                }
                if (W <= 0.f) {
                    s += 3.f;
                    continue;
                }
                claim(c, ax, W * 0.5f, d * 0.5f);
                s += W + 0.6f;
                gaps++;
                Rng lr(hash32(e.seed ^ (u32)(s * 7.f) ^ (side > 0 ? 0x10Au : 0x20Bu)));
                if (!lr.chance(fm.p)) continue;
                // parking wants a lot deep enough for two rows of stalls and an aisle, or one row on a shallow lot
                float wt[4] = {fm.vacant, W >= 12.f ? fm.park * (e.cls <= RC_AVENUE ? 1.6f : 1.f) : 0.f, fm.yard, fm.service};
                int kind = lr.weighted(wt, 4);
                OpenLot ol;
                ol.c = c;
                ol.ax = ax;
                ol.hx = W * 0.5f;
                ol.hy = d * 0.5f;
                ol.front = -out;
                ol.z = map.heightAt(c.x, c.y);
                ol.kind = kind == 0 ? OL_VACANT : (kind == 1 ? OL_PARKING : (kind == 2 ? OL_YARD : OL_SERVICE));
                ol.region = (u8)map.regionAt(c.x, c.y);
                ol.seed = hash32(lr.next() ^ 0xF00Du);
                ol.home = kind == 2 ? 1 : 0;
                ol.street = 1;
                fill.push_back(ol);
                fillKind.push_back(kind);
            }
        }
    }
    // (the SkyLine viaduct corridor and its stations stay clear, as for the buildings: transit.cpp)
    std::vector<Building> probe(fill.size());
    for (size_t i = 0; i < fill.size(); i++) {
        probe[i].c = fill[i].c;
        probe[i].ax = fill[i].ax;
        probe[i].hx = fill[i].hx;
        probe[i].hy = fill[i].hy;
        probe[i].seed = (u32)i;
    }
    transitPruneBuildings(probe);
    for (const Building& pb : probe) {
        openLots.push_back(fill[pb.seed]);
        counts[fillKind[pb.seed]]++;
    }
    LOG("Buildings: frontage fill %d gaps: %d vacant lots, %d street parking lots, %d side yards, %d service yards, %d left bare (%.1f ms)", gaps, counts[0],
        counts[1], counts[2], counts[3], gaps - counts[0] - counts[1] - counts[2] - counts[3], (TimeSeconds() - t0) * 1000.0);
}

// Repetition of the ordinary blocks (debug log): for every building, how many buildings within 150 m share its
// signature (style + archetype + massing + roof form + height band + facade pattern), per district. Lower is better.
void BuildingSet::logRepetition() const {
    double t0 = TimeSeconds();
    auto band = [](float h) {
        const float edges[] = {5, 9, 14, 22, 35, 55, 90, 140};
        int k = 0;
        while (k < 8 && h > edges[k]) k++;
        return k;
    };
    // colour family as the street reads it: white, grey, dark, or one of six hues (a curtain wall's pattern is its glass)
    auto family = [](u32 c) {
        vec4 v = unpackRGBA8(c);
        float mx = Max(v.x, Max(v.y, v.z)), mn = Min(v.x, Min(v.y, v.z));
        float sat = mx > 1e-3f ? (mx - mn) / mx : 0.f;
        if (sat < 0.14f) return mx > 0.86f ? 0 : (mx > 0.55f ? 1 : 2);
        float hue;
        if (mx == v.x) hue = fmodf((v.y - v.z) / (mx - mn), 6.f);
        else if (mx == v.y) hue = (v.z - v.x) / (mx - mn) + 2.f;
        else hue = (v.x - v.y) / (mx - mn) + 4.f;
        if (hue < 0.f) hue += 6.f;
        return 3 + (int)floorf(hue + 0.5f) % 6;
    };
    size_t n = buildings.size();
    std::vector<u32> sig(n, 0);
    for (size_t i = 0; i < n; i++) {
        const Building& b = buildings[i];
        const FacadeGPU& f = facades[b.facade];
        int lay = (int)f.wallLayer;
        if (lay == MAT_PLASTER || lay == MAT_CONCRETE) lay = MAT_STUCCO;
        u32 h = hash32((u32)b.style * 131u + (u32)b.arch);
        h = hashCombine(h, (u32)b.massing * 17u + (u32)b.roofForm * 3u + (u32)b.roof * 101u);
        h = hashCombine(h, (u32)band(b.height));
        h = hashCombine(h, blockstyle::formKey(b) * 7919u);   // (the house's form within its type: massing.cpp)
        h = hashCombine(h, (u32)(int)f.style * 7u + (u32)lay * 61u + (f.flags & 1u));
        if ((int)f.style == 1) h = hashCombine(h, (u32)family(f.glassColor) * 977u + (u32)family(f.frameColor));
        sig[i] = h;
    }
    struct Acc { double twins = 0, nbrs = 0; int count = 0, unique = 0; };
    Acc acc[REG_COUNT];
    std::vector<int> nbl;
    for (size_t i = 0; i < n; i++) {
        const Building& b = buildings[i];
        if (b.siteElem >= 0 || b.region >= REG_COUNT) continue;
        buildingsNear(b.c, 150.f, nbl);
        int same = 0, tot = 0;
        for (int j : nbl) {
            if ((size_t)j == i || buildings[j].siteElem >= 0 || length(buildings[j].c - b.c) > 150.f) continue;
            tot++;
            if (sig[j] == sig[i]) same++;
        }
        Acc& a = acc[b.region];
        a.twins += same;
        a.nbrs += tot;
        a.count++;
        a.unique += same == 0 ? 1 : 0;
    }
    std::string s;
    Acc all;
    for (int r = 0; r < REG_COUNT; r++) {
        const Acc& a = acc[r];
        if (a.count < 100) continue;
        s += StrFormat("%s%s %.1f%%", s.empty() ? "" : ", ", regionInfo((Region)r).name, 100.0 * a.twins / Max(1.0, a.nbrs));
        all.twins += a.twins;
        all.nbrs += a.nbrs;
        all.count += a.count;
        all.unique += a.unique;
    }
    LOG("Buildings: repetition (neighbours within 150 m with the same style, archetype, massing, roof, height band and facade pattern): "
        "%.1f%% overall, %.0f%% of buildings without a twin; by district: %s (%.1f ms)",
        100.0 * all.twins / Max(1.0, all.nbrs), 100.0 * all.unique / Max(1, all.count), s.c_str(), (TimeSeconds() - t0) * 1000.0);
}

// ------------------------------------------------------------------------------------------------ paved ground
namespace blockstyle {

// The back-yard pool of a house or villa (buildmesh.cpp backyardPool builds it): whether there is one, its centre, the
// water's half extents along b.ax and across it, the deck's width round it and the shape (0 rectangle, 1 rounded ends,
// 2 a lap pool), fitted inside the side hedges and the back lot line, clear of the house. Seeded streams of its own, so
// the paved-ground map (pavedRects) and the scattered vegetation know it. pr is left where the deck's look goes on.
bool poolDeck(const Building& b, Rng& pr, vec2& pc, float& pw, float& pdd, float& dw, int& kind) {
    if ((b.style != BS_HOUSE && b.style != BS_VILLA) || b.region == REG_FARMLAND || b.siteElem >= 0 || b.arch == AR_HOUSE_TRAILER) return false;
    Rng q(b.seed ^ 0x9003Bu);
    // (the houses' share by district: the suburbs and the Grove most, the islands' villas all; few in the old city's small
    // lots, the small towns and the fishing village)
    float share = 0.3f;
    switch (b.region) {
        case REG_SUBURBS: share = 0.38f; break;
        case REG_GROVE: share = 0.5f; break;
        case REG_KEY_CORAL:
        case REG_BAY_ISLAND: share = 0.6f; break;
        case REG_KEYS:
        case REG_KEY_TOWN: share = 0.25f; break;
        case REG_NORTH_CITY:
        case REG_CALLE_LUNA:
        case REG_FLATS:
        case REG_MIDTOWN: share = 0.12f; break;
        case REG_LAKE_TOWN:
        case REG_HARLOW:
        case REG_FORT_CASTELL:
        case REG_REDLAND: share = 0.15f; break;
        case REG_GULF_TOWN:
        case REG_RIDGE:
        case REG_SAWGRASS: share = 0.1f; break;
        default: break;
    }
    if (b.style == BS_HOUSE && !q.chance(share)) return false;
    const float backSpace = b.lotHy * 2.f - b.hy * 2.f - 6.f;
    if (backSpace <= 6.f) return false;
    pc = b.c - b.front * (b.hy + 2.f + Min(backSpace * 0.4f, 4.f)) + b.ax * q.range(-b.hx * 0.3f, b.hx * 0.3f);
    pw = q.range(2.5f, 4.5f);
    pdd = q.range(1.8f, 2.6f);
    const bool villa = b.style == BS_VILLA;
    const float shape = pr.f();
    kind = shape < 0.3f ? 1 : 0;   // 1: rounded ends
    if (villa && shape > 0.7f) kind = 2;   // a lap pool
    dw = villa ? pr.range(1.6f, 2.4f) : pr.range(1.2f, 1.8f);
    if (kind == 2) {
        pdd = 1.3f;
        pw = Max(pw, 6.5f);
    }
    // room for the deck: inside the side hedges and the back lot line, clear of the house's back wall
    const float off = dot(b.lotC - b.c, b.front), lat = dot(b.lotC - b.c, b.ax);
    const float pv = dot(pc - b.c, b.front), pu = dot(pc - b.c, b.ax);
    const float roomU = b.lotHx - 0.9f - fabsf(pu - lat);
    const float roomV = Min(pv - (off - b.lotHy) - 0.7f, -b.hy - pv - 0.4f);
    pw = Min(pw, roomU - 0.9f);
    pdd = Min(pdd, roomV - 0.9f);
    if (pw < 1.6f || pdd < 1.f) return false;
    dw = Clamp(Min(dw, Min(roomU - pw, roomV - pdd)), 0.9f, 2.4f);
    return true;
}

// A paved front yard (the older city - Calle Luna, the Flats, North Porto Sol, Midtown - and a few in Westbrook): concrete
// or brick pavers from the house's front to the lot's street edge (buildmesh.cpp), the driveway part of it; facadedetail.cpp
// leaves the lawn and the front walk out there. 0 none, 1 concrete, 2 pavers.
int frontPaved(const Building& b) {
    if (b.style != BS_HOUSE || b.interior >= 0 || b.siteElem >= 0 || b.arch == AR_HOUSE_VICTORIAN || b.arch == AR_HOUSE_RAISED || b.arch == AR_HOUSE_CONCH ||
        b.arch == AR_HOUSE_TRAILER)
        return 0;
    const float share = (b.region == REG_CALLE_LUNA || b.region == REG_FLATS) ? 0.28f
                        : ((b.region == REG_NORTH_CITY || b.region == REG_MIDTOWN) ? 0.18f : (b.region == REG_SUBURBS ? 0.04f : 0.f));
    if (hashToFloat(hash32(b.seed ^ 0xF4A7Eu)) >= share) return 0;
    return hashToFloat(hash32(b.seed ^ 0xF4A7Fu)) < 0.6f ? 1 : 2;
}

// Paved rectangles of a building (centre, axis, half extents): the footprint, a house's garage wing and driveway (the
// rules of buildmesh.cpp), its pool deck and paved front yard, a strip mall's or gas station's forecourt
struct PavedRect {
    vec2 c, ax;
    float hx, hy;
};
constexpr int kPavedRectsMax = 6;
int pavedRects(const Building& b, PavedRect* out) {
    int n = 0;
    out[n++] = {b.c, b.ax, b.hx, b.hy};
    if (b.siteElem >= 0) return n;
    {
        Rng pr(b.seed ^ 0x9001Fu);
        vec2 pc;
        float pw, pdd, dw;
        int kind;
        if (poolDeck(b, pr, pc, pw, pdd, dw, kind)) out[n++] = {pc, b.ax, pw + dw, pdd + dw};
    }
    if (frontPaved(b)) {
        const float off = dot(b.lotC - b.c, b.front), lat = dot(b.lotC - b.c, b.ax), v0 = b.hy - 0.05f, v1 = off + b.lotHy - 0.3f;
        if (v1 - v0 > 1.f) out[n++] = {b.c + b.ax * lat + b.front * ((v0 + v1) * 0.5f), b.ax, b.lotHx - 0.15f, (v1 - v0) * 0.5f};
    }
    if (const int gk = garageKind(b)) {
        // (the wing and its driveway, or the driveway strip beside the house from the wing's middle: buildmesh.cpp)
        float side = garageSide(b);
        float gw = 3.2f, gd = Min(b.hy, 3.4f);
        vec2 gc = b.c + b.ax * (side * (b.hx + gw)) + b.front * (b.hy - gd);
        if (gk == 1) out[n++] = {gc, b.ax, gw, gd};
        vec2 dA = gk == 1 ? gc + b.front * gd : gc, dB = b.lotC + b.front * (b.lotHy + 1.5f);
        dB = dA + b.front * Max(1.f, dot(dB - dA, b.front));
        out[n++] = {(dA + dB) * 0.5f, b.ax, gk == 1 ? 2.6f : 1.15f, length(dB - dA) * 0.5f};
    }
    if (b.style == BS_STRIPMALL || b.style == BS_GASSTATION) {
        vec2 lotFront = b.lotC + b.front * b.lotHy, face = b.c + b.front * b.hy;
        float pd = length(lotFront - face) * 0.5f;
        if (pd > 2.f) out[n++] = {(lotFront + face) * 0.5f, b.ax, b.hx + 1.f, pd};
    }
    return n;
}

inline bool inRect(vec2 p, vec2 c, vec2 ax, float hx, float hy) {
    vec2 d = p - c;
    return fabsf(dot(d, ax)) < hx && fabsf(dot(d, perp(ax))) < hy;
}

}  // namespace blockstyle

bool BuildingSet::pavedAt(vec2 p) const {
    using namespace blockstyle;
    const int cps = kCellsPerSide;
    // buildings are bucketed by centre: their footprints and aprons reach at most ~80 m beyond it
    int x0 = Clamp((int)((p.x - 90.f + kWorldHalf) / kCellSize), 0, cps - 1), x1 = Clamp((int)((p.x + 90.f + kWorldHalf) / kCellSize), 0, cps - 1);
    int y0 = Clamp((int)((p.y - 90.f + kWorldHalf) / kCellSize), 0, cps - 1), y1 = Clamp((int)((p.y + 90.f + kWorldHalf) / kCellSize), 0, cps - 1);
    PavedRect rr[kPavedRectsMax];
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if ((size_t)y * cps + x < cellLists.size())
                for (int i : cellLists[(size_t)y * cps + x]) {
                    const Building& b = buildings[i];
                    if (length2(b.c - p) > (b.hx + b.hy + 60.f) * (b.hx + b.hy + 60.f)) continue;
                    int nr = pavedRects(b, rr);
                    for (int k = 0; k < nr; k++)
                        if (inRect(p, rr[k].c, rr[k].ax, rr[k].hx, rr[k].hy)) return true;
                }
            if ((size_t)y * cps + x < openCellLists.size())
                for (int li : openCellLists[(size_t)y * cps + x]) {
                    const OpenLot& L = openLots[li];
                    if ((L.kind == OL_PARKING || L.kind == OL_SERVICE) && inRect(p, L.c, L.ax, L.hx, L.hy)) return true;
                }
        }
    return false;
}

int BuildingSet::pavedGrid(int cx, int cy, int n, std::vector<u8>& out) const {
    using namespace blockstyle;
    out.assign((size_t)Max(0, n) * Max(0, n), 0);
    if (n <= 0 || cx < 0 || cy < 0 || cx >= kCellsPerSide || cy >= kCellsPerSide) return 0;
    const vec2 o = cellOrigin(cx, cy);
    const float step = kCellSize / n;
    int count = 0;
    // rasterize one rectangle into the grid: the samples inside its bounding box, tested exactly
    auto raster = [&](vec2 c, vec2 ax, float hx, float hy) {
        vec2 ay = perp(ax);
        float ex = fabsf(ax.x) * hx + fabsf(ay.x) * hy, ey = fabsf(ax.y) * hx + fabsf(ay.y) * hy;
        int ix0 = Max(0, (int)floorf((c.x - ex - o.x) / step - 0.5f)), ix1 = Min(n - 1, (int)ceilf((c.x + ex - o.x) / step - 0.5f));
        int iy0 = Max(0, (int)floorf((c.y - ey - o.y) / step - 0.5f)), iy1 = Min(n - 1, (int)ceilf((c.y + ey - o.y) / step - 0.5f));
        for (int iy = iy0; iy <= iy1; iy++)
            for (int ix = ix0; ix <= ix1; ix++) {
                u8& f = out[(size_t)iy * n + ix];
                if (f) continue;
                vec2 p = o + vec2((ix + 0.5f) * step, (iy + 0.5f) * step);
                if (inRect(p, c, ax, hx, hy)) {
                    f = 1;
                    count++;
                }
            }
    };
    PavedRect rr[kPavedRectsMax];
    for (int y = Max(0, cy - 1); y <= Min(kCellsPerSide - 1, cy + 1); y++)
        for (int x = Max(0, cx - 1); x <= Min(kCellsPerSide - 1, cx + 1); x++) {
            size_t ci = (size_t)y * kCellsPerSide + x;
            if (ci < cellLists.size())
                for (int i : cellLists[ci]) {
                    int nr = pavedRects(buildings[i], rr);
                    for (int k = 0; k < nr; k++) raster(rr[k].c, rr[k].ax, rr[k].hx, rr[k].hy);
                }
            if (ci < openCellLists.size())
                for (int li : openCellLists[ci]) {
                    const OpenLot& L = openLots[li];
                    if (L.kind == OL_PARKING || L.kind == OL_SERVICE) raster(L.c, L.ax, L.hx, L.hy);
                }
        }
    return count;
}

}  // namespace World
