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
static const size_t kSchemaKinds = 1;   // terrain
static const Sample kSamples[] = {
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
    testLz();
    printf(gFails ? "world_format_test: %d FAILURES\n" : "world_format_test: all passed\n", gFails);
    return gFails ? 1 : 0;
}
