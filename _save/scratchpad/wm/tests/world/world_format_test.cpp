// Tests for the hand-built world's file formats (docs/NTW_FORMAT.md): the NTG grid codec, the NTW parser, formatter and
// schema (one sample file per kind), the LZ codec and world.bin's sections. Self-contained: no files are read.
// Build and run from the repo root, as CI's GCC job does:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined
//       tests/world/world_format_test.cpp -o /tmp/neontide-world-format-test && /tmp/neontide-world-format-test
#include "../../src/world/gridcodec.cpp"
#include "../../src/world/ntw.cpp"
#include "../../src/world/ntwschema.cpp"
#include "../../src/world/lz.cpp"
#include "../../src/world/worldbin.cpp"
#include <cstdio>
#include <cmath>

using namespace World;

static int gFails = 0;
#define EXPECT(c, ...)                         \
    do {                                       \
        if (!(c)) {                            \
            printf("FAIL line %d: ", __LINE__); \
            printf(__VA_ARGS__);               \
            printf("\n");                      \
            gFails++;                          \
        }                                      \
    } while (0)

static u32 gRng = 12345u;
static u32 rnd() {
    gRng = gRng * 1664525u + 1013904223u;
    return gRng >> 8;
}

// ---------------------------------------------------------------------------------------------------- grid codec
static void gridRoundTrip(const char* name, const std::vector<i16>& v, int w, int h, GridType t) {
    std::vector<u8> enc;
    EXPECT(encodeGrid(v.data(), w, h, t, enc), "%s: encode", name);
    std::vector<i16> dec;
    GridInfo gi;
    EXPECT(decodeGrid(enc.data(), enc.size(), dec, &gi) && dec == v && gi.width == w && gi.height == h, "%s: decode", name);
    for (int k = 0; k < 100; k++) {   // damaged streams decode to something or fail, never crash
        std::vector<u8> bad = enc;
        if (bad.size() > kGridHeaderBytes) bad[kGridHeaderBytes + rnd() % (bad.size() - kGridHeaderBytes)] ^= (u8)(1u << (rnd() % 8));
        std::vector<i16> d2;
        decodeGrid(bad.data(), bad.size(), d2, nullptr);
    }
    for (size_t cut = 0; cut < enc.size(); cut += 1 + enc.size() / 17) {
        std::vector<i16> d2;
        EXPECT(!decodeGrid(enc.data(), cut, d2, nullptr), "%s: truncated to %zu decoded", name, cut);
    }
}

static void testGrids() {
    for (int w = 1; w <= 5; w++)
        for (int h = 1; h <= 4; h++) {
            std::vector<i16> v((size_t)w * (size_t)h);
            for (i16& x : v) x = (i16)((int)(rnd() % 65536) - 32768);
            gridRoundTrip("random", v, w, h, GRID_I16);
            for (i16& x : v) x = 7;
            gridRoundTrip("constant", v, w, h, GRID_I16);
        }
    std::vector<i16> ext(64 * 64);
    for (size_t i = 0; i < ext.size(); i++) ext[i] = (i & 1) ? 32767 : -32768;
    gridRoundTrip("extremes", ext, 64, 64, GRID_I16);
    const int W = 300, H = 200;
    std::vector<i16> smooth((size_t)W * H), classes((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const double z = 300.0 * sin(x * 0.013) * cos(y * 0.017) + 120.0 * sin(x * 0.05 + y * 0.03);
            smooth[(size_t)y * W + x] = (i16)(z < -150.0 ? -150.0 : z);
            classes[(size_t)y * W + x] = (i16)(((x / 50) * 7 + (y / 60) * 3) % 24);
        }
    gridRoundTrip("smooth", smooth, W, H, GRID_I16);
    gridRoundTrip("classes", classes, W, H, GRID_U8);
    std::vector<i16> bad(4, 300);
    std::vector<u8> e;
    EXPECT(!encodeGrid(bad.data(), 2, 2, GRID_U8, e), "a u8 grid accepted 300");
}

// ---------------------------------------------------------------------------------------------------- NTW
static bool testBlocks(const std::string&, const std::string& type) { return type == "edge" || type == "furnish"; }

static int parseErrors(const char* s) {
    Ntw::Document d;
    std::vector<Ntw::Error> e;
    Ntw::parse(s, strlen(s), testBlocks, d, e);
    return (int)e.size();
}

static void testParser() {
    const char* s =
        "# lead\n"
        "ntw 1 roads district=cl # header note\n"
        "\n"
        "node cl.n1 at=1.5,2 z=3.40\n"
        "# before the edge\n"
        "edge cl.e7 from=cl.n1 to=cl.n2 name=\"SW 8th St\" color=#2f4f4f\n"
        "  lanes=1/1 speed=13.4 # trailing\n"
        "  crossing at=8.00 kind=signal\n"
        "  busstop cl.bus.08 at=40.00\n"
        "end\n"
        "# tail\n";
    Ntw::Document d;
    std::vector<Ntw::Error> e;
    EXPECT(Ntw::parse(s, strlen(s), testBlocks, d, e), "parse");
    EXPECT(d.kind == "roads" && d.version == 1 && d.records.size() == 2, "document");
    if (d.records.size() == 2) {
        const Ntw::Record& r = d.records[1];
        EXPECT(r.opensBlock && r.children.size() == 2, "block children");
        EXPECT(std::string(r.value("name")) == "SW 8th St" && std::string(r.value("color")) == "#2f4f4f", "quoted and # values");
        EXPECT(std::string(r.value("lanes")) == "1/1" && r.trailing == " trailing", "continuation and trailing comment");
        EXPECT(r.comments.size() == 1 && d.records[0].blankBefore && d.tailComments.size() == 1, "comments kept");
    }
    EXPECT(parseErrors("") == 1, "empty file");
    EXPECT(parseErrors("node a\n") == 1, "no header");
    EXPECT(parseErrors("ntw 1 x\nedge e1\n") == 1, "unclosed block");
    EXPECT(parseErrors("ntw 1 x\nend\n") == 1, "stray end");
    EXPECT(parseErrors("ntw 1 x\nnode a b=1 b=2\n") == 1, "duplicate key");
    EXPECT(parseErrors("ntw 1 x\nnode a b=\"open\n") == 1, "unterminated quote");
    EXPECT(parseErrors("ntw 1 x\nx=1\n") == 1, "continuation without a record");
    EXPECT(parseErrors("ntw 1 x\nNode a\n") == 1 && parseErrors("ntw 1 x\nnode A\n") == 1, "case");
    EXPECT(parseErrors("ntw 1 x\nnode a\n# c\n  b=1\n") == 1, "continuation after a comment");
    double v = 0;
    EXPECT(Ntw::toNumber("-12.50", &v) && v == -12.5 && !Ntw::toNumber("+1", &v) && !Ntw::toNumber("1.", &v) && !Ntw::toNumber("0x10", &v), "numbers");
    std::vector<vec2> pts;
    EXPECT(Ntw::toPoints("0,0;1.5,2;3,4", pts) && pts.size() == 3 && !Ntw::toPoints("0,0;", pts), "points");
    EXPECT(Ntw::formatNumbers("1.5,2;-0.001,3", 2) == "1.50,2.00;0.00,3.00" && Ntw::formatNumbers("both:2.4", 2) == "both:2.40", "number formatting");
    EXPECT(Ntw::formatNumbers("8-18.5;22-4", 2) == "8.00-18.50;22.00-4.00" && Ntw::formatNumbers("-5--2", 0) == "-5--2" &&
               Ntw::formatNumbers("lib.kit.x-2:end", 2) == "lib.kit.x-2:end" && Ntw::formatNumbers("1e-3", 2) == "0.00" &&
               Ntw::formatNumbers("2.1x0.9x0.85", 2) == "2.10x0.90x0.85" && Ntw::formatNumbers("box:3x", 2) == "box:3x",
           "ranges and sizes");
}

struct Sample {
    const char* name;
    const char* text;
};
static const size_t kSchemaKinds = 17;   // terrain, world, district, roads, signal, block, building, plan, layout, unit,
                                         // fitout, set, facade, palette, places, transit, catalog
static const Sample kSamples[] = {
    {"block.ntw",
        "ntw 1 block id=cl.b014 district=cl\n"
        "parcel cl.b014.p03 poly=1690.20,350.10;1712.00,350.10;1712.00,372.40;1690.20,372.40 use=residential grade=3.45\n"
        "  blend=2.00 ground=lawn front=cl.e0007:right\n"
        "  paint kind=paving poly=1690.20,350.10;1694.00,350.10;1694.00,362.00;1690.20,362.00\n"
        "  fence kind=lib.fence.picket_white line=1690.20,350.10;1712.00,350.10 gate=6.00:1.00\n"
        "end\n"
        "path cl.b014.pa1 line=1690.20,358.00;1698.00,358.00 w=1.20 kind=walk\n"
        "plaza cl.b014.pz1 poly=1714.00,350.00;1730.00,350.00;1730.00,366.00;1714.00,366.00 ground=paving tags=market\n"
        "steps line=1714.00,358.00;1716.00,358.00 w=3.00 treads=4 rise=0.15\n"
        "tree kind=lib.veg.royal_palm at=1693.10,352.40 h=11.50 yaw=40.0\n"
        "tree kind=lib.veg.live_oak at=1709.80,369.00 h=9.00 canopy=8.00\n"
        "prop kind=lib.prop.trash_bin at=1712.60,350.30 yaw=180.0\n"
        "light kind=lib.light.porch_warm at=1701.10,354.90,2.60 sched=evening\n"
        "spot kind=talk_ring at=1722.00,358.00 cap=4 hours=17.00-22.00\n"
        "spot kind=queue line=1726.00,364.50;1726.00,360.00 cap=8 roles=customer for=cl.b014.s07:f0.d1\n"
        "lot cl.b014.lot1 entry=cl.e0007:72.00:right levels=1\n"
        "  row line=1732.00,352.00;1732.00,372.00 bays=8 w=2.50 len=5.00 yaw=90.0\n"
        "end\n"},
    {"building.ntw",
        "ntw 1 building id=cl.b014.h03 block=cl.b014 parcel=cl.b014.p03\n"
        "place plan=lib.plan.house.bungalow_b02 at=1701.10,361.30 z=3.45 yaw=90.0 mirror=1\n"
        "config palette=lib.palette.bungalow.coral roof=lib.kit.roof_tile_terracotta porch=screened garage=carport_left\n"
        "furnish household=family_young variant=b\n"
        "override room=f0.kitchen variant=c\n"
        "override add=lib.kit.mural_wall_02 wall=f0.w2 side=out at=2.00 h=2.40\n"
        "occupancy residents=4 schedule=family_school\n"
        "outbuilding g1 plan=lib.plan.garage.shed_a at=1708.00,370.00 yaw=90.0\n"},
    {"catalog.ntw",
        "ntw 1 catalog\n"
        "piece lib.kit.sofa_3seat code=FURN_SOFA size=2.10x0.90x0.85 variants=4 collide=box\n"},
    {"district.ntw",
        "ntw 1 district district=cl\n"
        "bounds poly=950.00,-920.00;2620.00,-920.00;2620.00,920.00;950.00,920.00\n"
        "label at=1560.00,350.00\n"
        "zone cl.audio.main kind=audio poly=950.00,-920.00;2620.00,-920.00;2620.00,920.00 tags=street_latin,traffic_light\n"
        "zone cl.turf.north kind=turf poly=1500.00,300.00;1900.00,300.00;1900.00,700.00;1500.00,700.00 tags=cuervos\n"
        "zone cl.ocho.nights kind=density poly=1650.00,330.00;1760.00,330.00;1760.00,520.00;1650.00,520.00\n"
        "  peds=0.60,0.40,0.30,0.20,0.20,0.30,0.50,0.80,1.00,1.00,1.00,1.10,1.20,1.10,1.00,1.00,1.10,1.30,1.60,1.90,2.20,2.40,2.00,1.20\n"
        "  days=fri,sat\n"
        "trunk node=tr.n00412\n"},
    {"facade.ntw",
        "ntw 1 facade id=lib.facade.bungalow_siding\n"
        "wall material=lib.kit.mat_siding color=wall\n"
        "trim kind=band color=trim h=0.20\n"
        "window kind=dhung color=trim shutters=fixed\n"
        "door style=lib.kit.door_panel_5 color=door\n"},
    {"layout.ntw",
        "ntw 1 layout id=lib.layout.house.bungalow_b02_f0\n"
        "wall w1 a=0.00,0.00 b=11.60,0.00 t=0.25 ext=front\n"
        "wall w2 a=11.60,0.00 b=11.60,9.40 t=0.25 ext=right\n"
        "wall w3 a=11.60,9.40 b=0.00,9.40 t=0.25 ext=back\n"
        "wall w4 a=0.00,9.40 b=0.00,0.00 t=0.25 ext=left\n"
        "wall w5 a=5.20,0.00 b=5.20,6.00 t=0.12\n"
        "wall w6 a=0.00,6.00 b=11.60,6.00 t=0.12\n"
        "wall w7 a=0.00,7.20 b=11.60,7.20 t=0.12\n"
        "wall w8 a=6.00,7.20 b=6.00,9.40 t=0.12\n"
        "wall w9 a=8.60,7.20 b=8.60,9.40 t=0.12\n"
        "room living type=living poly=0.00,0.00;5.20,0.00;5.20,6.00;0.00,6.00 cap=6 sched=evening lux=150\n"
        "room kitchen type=kitchen poly=5.20,0.00;11.60,0.00;11.60,6.00;5.20,6.00 cap=4 lux=300\n"
        "room hall type=hall poly=0.00,6.00;11.60,6.00;11.60,7.20;0.00,7.20 cap=2\n"
        "room bed type=bed poly=0.00,7.20;6.00,7.20;6.00,9.40;0.00,9.40 cap=2\n"
        "room bath type=bath poly=6.00,7.20;8.60,7.20;8.60,9.40;6.00,9.40 cap=1\n"
        "room bed2 type=bed poly=8.60,7.20;11.60,7.20;11.60,9.40;8.60,9.40 cap=1\n"
        "door d1 wall=w1 at=2.20 w=0.95 h=2.10 kind=hinged hinge=left swing=in leads=out:living\n"
        "door d2 wall=w5 at=3.00 w=0.90 h=2.10 kind=hinged hinge=left swing=in leads=living:kitchen\n"
        "door d3 wall=w6 at=2.60 w=0.90 h=2.10 kind=hinged hinge=right swing=in leads=living:hall\n"
        "door d4 wall=w6 at=8.40 w=0.90 h=2.10 kind=opening leads=kitchen:hall\n"
        "door d5 wall=w7 at=3.00 w=0.90 h=2.10 kind=hinged hinge=left swing=in leads=hall:bed\n"
        "door d6 wall=w7 at=7.30 w=0.90 h=2.10 kind=hinged hinge=right swing=in leads=hall:bath lock=key\n"
        "door d7 wall=w7 at=10.10 w=0.90 h=2.10 kind=hinged hinge=left swing=in leads=hall:bed2\n"
        "door d8 wall=w2 at=2.00 w=0.90 h=2.10 kind=hinged hinge=right swing=out leads=kitchen:out lock=key\n"
        "window n1 wall=w1 at=4.00 w=1.20 h=1.40 sill=0.90 kind=dhung room=living\n"
        "window n2 wall=w1 at=8.40 w=1.20 h=1.40 sill=0.90 kind=dhung room=kitchen\n"
        "window n3 wall=w3 at=1.50 w=1.00 h=1.40 sill=0.90 kind=dhung room=bed2\n"
        "window n4 wall=w3 at=4.30 w=0.60 h=0.60 sill=1.50 kind=jalousie room=bath\n"
        "window n5 wall=w3 at=8.60 w=1.20 h=1.40 sill=0.90 kind=dhung room=bed\n"
        "window n6 wall=w4 at=6.50 w=1.20 h=1.40 sill=0.90 kind=dhung room=living\n"
        "furnish living variants=a,b,c\n"
        "  variant a set=lib.set.living.cottage_01 anchor=w5\n"
        "  variant b set=lib.set.living.family_04 anchor=w5\n"
        "  item kind=lib.kit.sofa_3seat at=2.50,4.80 yaw=180.0 variant=c\n"
        "  item kind=lib.kit.tv_stand at=2.50,0.60 variant=c\n"
        "end\n"
        "spot s1 kind=sofa room=living at=2.50,4.60 yaw=0.0 cap=2 hours=18.00-23.00 roles=resident\n"
        "light l1 room=living at=2.60,3.00,2.75 fixture=lib.kit.ceiling_bowl\n"},
    {"layout_core.ntw",
        "ntw 1 layout id=lib.layout.midrise.core_a\n"
        "wall w1 a=0.00,0.00 b=6.00,0.00 t=0.25 ext=front\n"
        "wall w2 a=0.00,5.00 b=3.00,5.00 t=0.20\n"
        "wall w3 a=6.00,0.00 b=6.00,10.00 t=0.25 ext=right\n"
        "wall w4 a=6.00,10.00 b=0.00,10.00 t=0.25 ext=back\n"
        "wall w5 a=0.00,10.00 b=0.00,0.00 t=0.25 ext=left\n"
        "wall w6 a=3.00,5.00 b=3.00,10.00 t=0.20\n"
        "wall w7 a=3.00,5.00 b=6.00,5.00 t=0.12\n"
        "room lobby type=lobby poly=0.00,0.00;6.00,0.00;6.00,5.00;0.00,5.00 cap=20 sched=always lux=300\n"
        "room stair type=stair poly=0.00,5.00;3.00,5.00;3.00,10.00;0.00,10.00 cap=6\n"
        "room liftlobby type=lobby poly=3.00,5.00;6.00,5.00;6.00,10.00;3.00,10.00 cap=8\n"
        "door d1 wall=w1 at=3.00 w=1.80 h=2.40 kind=revolving leads=out:lobby hours=7.00-22.00\n"
        "door d2 wall=w2 at=1.50 w=0.90 h=2.10 kind=hinged hinge=left swing=out leads=lobby:stair lock=none\n"
        "door d3 wall=w7 at=1.50 w=2.40 h=2.40 kind=opening leads=lobby:liftlobby\n"
        "window n1 wall=w1 at=1.20 w=1.20 h=2.00 sill=0.40 kind=storefront room=lobby\n"
        "stair st1 from=f0.stair to=f1.stair run=0.75,6.00;0.75,9.20;2.25,9.20;2.25,6.00 w=1.10 kind=dogleg treads=18 rise=0.16\n"
        "  landing=1.10\n"
        "stair st2 from=f0.lobby to=roof.plant run=5.55,3.50;5.00,4.05;4.45,3.50;5.00,2.95;5.55,3.50 w=0.75 kind=spiral treads=12\n"
        "  rise=0.22 walk=0.55\n"
        "lift lf1 at=4.50,7.50 size=1.60x1.40 stops=f0,f1,f2,f3 cap=8 call=4.50,5.80\n"},
    {"palette.ntw",
        "ntw 1 palette family=bungalow\n"
        "palette coral wall=#e8a08a trim=#f4efe6 door=#2f4f4f accent=#d9c27a\n"},
    {"places.ntw",
        "ntw 1 places\n"
        "place place.mari_apartment building=cl.b014.m01 door=f0.d1 walk=1781.20,160.40 curb=1783.00,160.40,3.41 dir=0.00,1.00\n"
        "anchor cl.pd.station kind=police_station at=1850.00,240.00 building=cl.b031.x01 door=f0.d1\n"
        "anchor cl.ev.busk1 kind=event event=busk at=1722.00,356.00 yaw=90.0\n"},
    {"plan.ntw",
        "ntw 1 plan id=lib.plan.house.bungalow_b02\n"
        "meta class=house family=bungalow era=1925 floors=1 beds=2 baths=1 size=11.60x9.40 tags=porch,attic\n"
        "footprint poly=0.00,0.00;11.60,0.00;11.60,9.40;0.00,9.40\n"
        "floor f0 layout=lib.layout.house.bungalow_b02_f0 z=0.00 h=2.85\n"
        "floor attic layout=lib.layout.house.bungalow_b02_attic z=2.85 h=1.70 access=hatch:f0.hall\n"
        "roof form=hip pitch=24.0 overhang=0.45\n"
        "facade style=lib.facade.bungalow_siding\n"
        "option porch choices=none,screened,open default=none\n"
        "feature porch.screened when=porch:screened mesh=lib.kit.porch_screened span=1.20,0.00;7.80,0.00 depth=2.40\n"
        "entrance main door=f0.d1 kind=main\n"},
    {"roads.ntw",
        "ntw 1 roads district=cl\n"
        "node cl.n0012 at=1702.00,365.50 z=3.40 control=signal plan=lib.signal.4way_proleft offset=12.0\n"
        "node cl.n0013 at=1702.00,481.00 z=3.55 control=stop stops=cl.e0007,cl.e0009\n"
        "edge cl.e0007 from=cl.n0012 to=cl.n0013 class=street name=\"SW 8th St\" lanes=1/1 lanewidth=3.20 parking=both:2.40\n"
        "  sidewalk=both:3.00 speed=13.4 shape=1702.00,365.50;1702.40,420.00;1702.00,481.00 profile=0.00:3.40;115.50:3.55\n"
        "  lanes from=80.00 to=end dir=fwd count=2 add=left taper=12.00\n"
        "  noswitch dir=fwd from=95.00 to=end\n"
        "  moves at=n1 lanes=l,sr\n"
        "  crossing at=8.00 kind=signal\n"
        "  crossing at=60.00 kind=zebra\n"
        "  busstop cl.bus.08 at=40.00 side=right shelter=lib.prop.shelter_a wait=1704.80,403.00:3.00 routes=bus.8\n"
        "  parkbay from=12.00 to=34.00 side=left kind=parallel meter=1\n"
        "  nostop from=0.00 to=12.00 side=right why=junction\n"
        "  loading from=44.00 to=56.00 side=left hours=6.00-11.00\n"
        "  curbcut at=72.00 side=right w=4.00 to=cl.b014.lot1\n"
        "end\n"
        "junction cl.n0012 turns=custom\n"
        "  stopline edge=cl.e0007 at=6.50\n"
        "  connect from=cl.e0007:0 to=cl.e0011:0 turn=left protected=1\n"
        "end\n"},
    {"set.ntw",
        "ntw 1 set id=lib.set.living.cottage_01 room=living style=cottage income=mid\n"
        "fits w=3.20-6.00 d=3.00-5.00\n"
        "item kind=lib.kit.sofa_3seat at=0.00,3.20 yaw=180.0\n"
        "item kind=lib.kit.rug_oval at=0.00,2.00\n"},
    {"signal.ntw",
        "ntw 1 signal id=lib.signal.4way_proleft\n"
        "plan cycle=90.0 legs=4\n"
        "phase p1 green=14.0 amber=3.0 allred=1.0 moves=n_lp,s_lp\n"
        "phase p2 green=30.0 amber=4.0 allred=2.0 moves=n_s,n_r,n_l,s_s,s_r,s_l walk=e,w walktime=7.0 flash=12.0\n"
        "phase p3 green=30.0 amber=4.0 allred=2.0 moves=e_s,e_r,e_l,w_s,w_r,w_l walk=n,s walktime=7.0 flash=12.0\n"},
    {"terrain.ntw",
        "ntw 1 terrain\n"
        "# a small bake: one grid of each layer kind, two water classes, an outline and a channel\n"
        "source tool=terrainbake generator=procedural seed=0x5EA11A7A commit=0c288db\n"
        "grid res=64 cell=8.00 origin=-256.00,-256.00\n"
        "layer height file=height.ntg type=i16 unit=0.01 hash=ea53b9afef88dfc9 range=-4.93,16.85\n"
        "layer water file=water.ntg type=u8 hash=b0c57636cd3fb154\n"
        "water c0 level=none\n"
        "water c1 level=0.0000\n"
        "layer coast file=coast.ntg type=i16 res=16 cell=32.00 unit=0.250 hash=856c8ef74cf14105\n"
        "outline mainland pts=-200.00,-200.00;200.00,-200.00;200.00,150.00;-200.00,150.00\n"
        "channel ch.00 width0=12.00 width1=20.00 depth=2.50 pts=-100.00,-200.00;-80.00,0.00;-60.00,150.00\n"},
    {"transit.ntw",
        "ntw 1 transit line=bus.8 kind=bus name=\"8 Calle Ocho\"\n"
        "route edges=cl.e0007,cl.e0011\n"
        "stop s1 busstop=cl.bus.08\n"
        "service headway=600.0 first=5.50 last=23.50\n"},
    {"world.ntw",
        "ntw 1 world\n"
        "world palmera name=\"Palmera\"\n"
        "district cl name=\"Calle Luna\" label=1560.00,350.00 order=1\n"},
};

static void testSchemaAndFormat() {
    const Ntw::FormatRules rules = Ntw::schemaFormatRules();
    int kinds = 0;
    for (const Sample& s : kSamples) {
        Ntw::Document d;
        std::vector<Ntw::Error> e;
        const bool ok = Ntw::parse(s.text, strlen(s.text), Ntw::schemaOpensBlock, d, e);
        Ntw::checkDocument(d, e);
        EXPECT(ok && e.empty(), "%s: %s", s.name, e.empty() ? "parse" : (std::to_string(e[0].line) + ": " + e[0].message).c_str());
        const std::string f = Ntw::format(d, rules);
        EXPECT(f == s.text, "%s: not in canonical form:\n%s", s.name, f.c_str());
        Ntw::Document d2;
        std::vector<Ntw::Error> e2;
        EXPECT(Ntw::parse(f.data(), f.size(), Ntw::schemaOpensBlock, d2, e2) && Ntw::format(d2, rules) == f, "%s: formatting is not stable", s.name);
        // the same file with CRLF line ends and a byte-order mark (a Windows checkout without .gitattributes) reads the same
        std::string crlf = "\xEF\xBB\xBF";
        for (const char* c = s.text; *c; c++) {
            if (*c == '\n') crlf += '\r';
            crlf += *c;
        }
        Ntw::Document d3;
        std::vector<Ntw::Error> e3;
        const bool ok3 = Ntw::parse(crlf.data(), crlf.size(), Ntw::schemaOpensBlock, d3, e3);
        Ntw::checkDocument(d3, e3);
        EXPECT(ok3 && e3.empty() && Ntw::format(d3, rules) == s.text, "%s: reads differently with CRLF line ends and a byte-order mark", s.name);
        kinds += Ntw::findKind(d.kind) != nullptr;
    }
    EXPECT(kinds == (int)(sizeof(kSamples) / sizeof(kSamples[0])), "samples of unknown kinds");
    EXPECT(Ntw::schemaKinds().size() == kSchemaKinds, "the schema has %zu kinds", Ntw::schemaKinds().size());
    // schema errors are found and placed
    const char* bad = "ntw 1 terrain\ngrid res=2.5 cell=8 origin=1\nlayer height file=h.ntg type=f32 hash=XY\nwater level=1\nfoo x=1\noutline lake\n";
    Ntw::Document db;
    std::vector<Ntw::Error> eb;
    Ntw::parse(bad, strlen(bad), Ntw::schemaOpensBlock, db, eb);
    Ntw::checkDocument(db, eb);
    EXPECT(eb.size() >= 6, "terrain schema errors: %zu", eb.size());
}

// The reference table and the schema agree: every reference field has a row, and every row names a real field (or a
// record whose own id is a reference)
static void testRefs() {
    const std::vector<Ntw::RefDef>& refs = Ntw::schemaRefs();
    auto hasRow = [&](const char* kind, const char* type, const char* key) {
        for (const Ntw::RefDef& r : refs)
            if (!strcmp(r.kind, kind) && !strcmp(r.type, type) && r.key && !strcmp(r.key, key)) return true;
        return false;
    };
    for (const Ntw::KindDef& k : Ntw::schemaKinds())
        for (const Ntw::RecordDef& rd : k.records)
            for (const Ntw::FieldDef& f : rd.fields)
                if (f.type == Ntw::VT_REF) EXPECT(hasRow(k.kind, rd.type, f.key), "no reference row for %s %s %s", k.kind, rd.type, f.key);
    for (const Ntw::RefDef& r : refs) {
        const Ntw::KindDef* k = Ntw::findKind(r.kind);
        bool ok = k != nullptr && r.pattern && r.pattern[0];
        if (ok && !strcmp(r.type, "ntw")) {
            bool found = false;
            for (const Ntw::FieldDef& f : k->header) found |= r.key && !strcmp(f.key, r.key);
            ok = found;
        } else if (ok) {
            const Ntw::RecordDef* rd = Ntw::findRecord(*k, r.type);
            bool found = rd && !r.key && rd->id == 2;
            if (rd && r.key)
                for (const Ntw::FieldDef& f : rd->fields) found |= !strcmp(f.key, r.key);
            ok = found;
        }
        EXPECT(ok, "reference row %s %s %s names no field", r.kind, r.type, r.key ? r.key : "(id)");
        // every pattern parses: alternatives with parts, each reference naming its classes
        const Ntw::RefPattern p = Ntw::parseRefPattern(r.pattern);
        bool parts = !p.alts.empty() && p.text == r.pattern;
        for (const Ntw::RefAlt& a : p.alts) {
            parts = parts && !a.parts.empty();
            for (const Ntw::RefPart& part : a.parts) parts = parts && (part.kind != 'r' || (!part.text.empty() && !strchr(part.text.c_str(), ':')));
        }
        EXPECT(parts, "reference row %s %s %s: the pattern %s doesn't parse", r.kind, r.type, r.key ? r.key : "(id)", r.pattern);
    }

    // values read with a pattern: the references in each list item, as "class=id" joined by ' ', groups by '/'
    struct Case {
        const char* pattern;
        const char* value;
        bool fits;
        const char* names;
    };
    const Case cases[] = {
        {"auto|<signal>", "auto", true, ""},
        {"auto|<signal>", "lib.signal.fourway", true, "signal=lib.signal.fourway"},
        {"*:<edge>,...", "n:cl.e0001,e:cl.e0002", true, "edge=cl.e0001/edge=cl.e0002"},
        {"*:<edge>,...", "n-cl.e0001", false, ""},
        {"<edge>:*", "cl.e0001:0", true, "edge=cl.e0001"},
        {"<edge>:*", "cl.e0001", false, ""},
        {"<edge>:*:*", "cl.e0007:72.00:right", true, "edge=cl.e0007"},
        {"<parcel|lot>", "cl.b014.lot1", true, "parcel|lot=cl.b014.lot1"},
        {"<unit|fitout>,...", "lib.unit.walkup.*,lib.fitout.shop.corner_a", true, "unit|fitout=lib.unit.walkup.*/unit|fitout=lib.fitout.shop.corner_a"},
        {"<busstop>|<building>:<f.door|f.counter>", "cl.b014.s07:f0.d1", true, "building=cl.b014.s07 f.door|f.counter=f0.d1"},
        {"out:<.room|.slot>|<.room|.slot>:out|<.room|.slot>:<.room|.slot>", "out:hall", true, ".room|.slot=hall"},
        {"<f.room>|0:<.room>", "0:hall", true, ".room=hall"},
        {"<f.room>|+1:<.room>|-1:<.room>", "+2:hall", false, ""},
        {"<node>", "Bad Id", false, ""},
        {"<edge>,...", "cl.e0001,,cl.e0002", false, ""},
        {"all|<edge>,...", "all", true, ""},
    };
    for (const Case& c : cases) {
        std::vector<std::vector<Ntw::RefName>> groups;
        const bool fits = Ntw::matchRefs(Ntw::parseRefPattern(c.pattern), c.value, groups);
        std::string names;
        for (size_t g = 0; fits && g < groups.size(); g++) {
            if (g) names += "/";
            for (size_t i = 0; i < groups[g].size(); i++) names += (i ? " " : "") + groups[g][i].cls + "=" + groups[g][i].id;
        }
        EXPECT(fits == c.fits && names == c.names, "pattern %s, value %s: %s '%s'", c.pattern, c.value, fits ? "fits" : "doesn't fit", names.c_str());
    }
    const std::vector<Ntw::RefRule>* node = Ntw::refRules("roads", "node");
    EXPECT(node && node->size() == 4 && !strcmp((*node)[0].key, "plan") && (*node)[0].pattern.alts.size() == 2, "refRules: roads node");
    const std::vector<Ntw::RefRule>* junction = Ntw::refRules("roads", "junction");
    EXPECT(junction && junction->size() == 1 && !(*junction)[0].key, "refRules: a junction's id");
    const std::vector<Ntw::RefRule>* stair = Ntw::refRules("unit", "stair");
    EXPECT(stair && stair->size() == 2 && Ntw::refRules("roads", "ntw") && !Ntw::refRules("terrain", "grid") && !Ntw::refRules("nokind", "node"), "refRules: lookups");
}

// ---------------------------------------------------------------------------------------------------- LZ, world.bin
static void testLz() {
    std::vector<std::vector<u8>> inputs(5);
    inputs[1].push_back(7);
    for (int i = 0; i < 5000; i++) {
        const std::string line = "building cl.b" + std::to_string(i % 97) + " plan=lib.plan.house.bungalow_b0" + std::to_string(i % 7) + "\n";
        inputs[2].insert(inputs[2].end(), line.begin(), line.end());
    }
    for (int i = 0; i < 30000; i++) inputs[3].push_back((u8)rnd());
    inputs[4].assign(100000, 0);
    for (const std::vector<u8>& in : inputs) {
        std::vector<u8> z;
        lzCompress(in.data(), in.size(), z);
        std::vector<u8> back(in.size() + 1);
        EXPECT(lzDecompress(z.data(), z.size(), back.data(), in.size()) && std::equal(in.begin(), in.end(), back.begin()), "lz round trip of %zu bytes", in.size());
        for (int k = 0; k < 100 && !z.empty(); k++) {
            std::vector<u8> bad = z;
            bad[rnd() % bad.size()] ^= (u8)(1u << (rnd() % 8));
            lzDecompress(bad.data(), bad.size(), back.data(), in.size());
        }
    }
    WorldBinWriter w;
    std::string txt;
    for (int i = 0; i < 500; i++) txt += "line " + std::to_string(i % 10) + "\n";
    w.addCompressed("text", std::vector<u8>(txt.begin(), txt.end()));
    w.add("raw", WSK_RAW, std::vector<u8>{1, 2, 3});
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    std::string why;
    EXPECT(view.open(bin.data(), bin.size(), &why), "world.bin open: %s", why.c_str());
    std::vector<u8> b;
    EXPECT(view.bytes("text", b) && std::string(b.begin(), b.end()) == txt && view.find("text")->kind == WSK_LZ, "lz section");
    EXPECT(view.bytes("raw", b) && b.size() == 3 && !view.bytes("missing", b), "raw section");
    std::vector<u8> damaged = bin;
    damaged[damaged.size() - 1] ^= 1;
    EXPECT(!view.open(damaged.data(), damaged.size(), &why), "a damaged world.bin opened");
    damaged = bin;
    damaged[24 + 20] ^= 1;   // an unused byte of the first section's name: only the hash covers it
    EXPECT(!view.open(damaged.data(), damaged.size(), &why), "a damaged section table opened");
}

int main() {
    testGrids();
    testParser();
    testSchemaAndFormat();
    testRefs();
    testLz();
    printf(gFails ? "world_format_test: %d FAILURES\n" : "world_format_test: all passed\n", gFails);
    return gFails ? 1 : 0;
}
