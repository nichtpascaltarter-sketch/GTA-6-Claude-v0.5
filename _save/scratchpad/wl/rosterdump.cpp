// Dumps the outfits of the game's character roster (assets.cpp reqs) - scratch tool
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
static const char* TOPS[] = {"tshirt","tank","polo","hawaiian","dressshirt","hoodie","suit","police","medic","hivis","bikini","onepiece","sundress","blouse","none","crop","oversized","jumpsuit"};
static const char* BOTS[] = {"jeans","shorts","cargo","slacks","skirt","trunks","bikini","police","baggy","leggings","work","hotpants"};
static const char* SHOES[] = {"sneaker","dress","boot","sandal","bare","flats","runner","loafer"};
static const char* HATS[] = {"cap","capback","police","hardhat","sunhat","fedora","beanie","bandana"};
static const char* OUTS[] = {"overshirt","ziphoodie","cardigan","jacket","vest","blazer"};
static const char* BAGS[] = {"backpack","crossbody","tote"};
int main(int argc, char** argv) {
    struct Req { unsigned seed; int role; int fg; };
    std::vector<Req> reqs;
    for (int i = 0; i < 72; i++) reqs.push_back({0x1000u + (unsigned)i * 7919u, (i % 11 == 3) ? 3 : (i % 11 == 7 ? 5 : 0), i & 1});
    for (int i = 0; i < 10; i++) reqs.push_back({0x2000u + (unsigned)i * 104729u, 4, i & 1});
    int extra = argc > 1 ? atoi(argv[1]) : 0;
    for (int i = 0; i < extra; i++) reqs.push_back({0x9000u + (unsigned)i * 2654435761u, 0, i & 1});
    for (size_t i = 0; i < reqs.size(); i++) {
        unsigned seed = reqs[i].seed;
        CharacterDesc d = randomCharacter(seed, reqs[i].role);
        for (int k = 0; k < 24 && reqs[i].fg >= 0 && (int)d.gender != reqs[i].fg; k++) { seed = hash32(seed + 0x9e37u); d = randomCharacter(seed, reqs[i].role); }
        printf("%3zu r%d %s age %2.0f w%.2f | %-10s %-9s %-7s | hat %-8s out %-9s bag %-9s gl %d | top %.2f %.2f %.2f\n", i, reqs[i].role, d.gender ? "F" : "M", 18 + 62 * d.age, d.weight,
               TOPS[d.top], BOTS[d.bottom], SHOES[d.shoes], d.hat >= 0 ? HATS[d.hat] : "-", d.outer >= 0 ? OUTS[d.outer] : "-", d.bag >= 0 ? BAGS[d.bag] : "-", d.glasses,
               d.topColor.x, d.topColor.y, d.topColor.z);
    }
}
