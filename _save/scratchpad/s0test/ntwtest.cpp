// Tests for the NTW parser and formatter, the schema check, the LZ codec and LZ sections in world.bin (scratch only).
// Build from a tree root: g++ -std=c++17 -O2 -Wall -Wextra -I. ntwtest.cpp
#include "tools/native_stubs.cpp"
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include "src/world/lz.cpp"
#include "src/world/worldbin.cpp"
#include <cstdio>
using namespace World;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static bool blocksTest(const std::string&, const std::string& type) { return type == "edge" || type == "furnish" || type == "variant"; }

static Ntw::Document parseOk(const std::string& s, Ntw::BlockPredicate b = blocksTest) {
    Ntw::Document d;
    std::vector<Ntw::Error> e;
    bool ok = Ntw::parse(s.data(), s.size(), b, d, e);
    CHECK(ok, "parse failed: %s", e.empty() ? "" : (std::to_string(e[0].line) + ": " + e[0].message).c_str());
    return d;
}

static int parseErrors(const std::string& s) {
    Ntw::Document d;
    std::vector<Ntw::Error> e;
    Ntw::parse(s.data(), s.size(), blocksTest, d, e);
    return (int)e.size();
}

static u32 rs = 99u;
static u32 rnd() { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

int main() {
    // ---- parser
    {
        const std::string s =
            "# lead comment\n"
            "ntw 1 roads district=cl  # header note\n"
            "\n"
            "node cl.n1 at=1.5,2 z=3.40 control=signal\n"
            "# before the edge\n"
            "edge cl.e7 from=cl.n1 to=cl.n2 name=\"SW 8th St\" color=#2f4f4f\n"
            "  lanes=1/1 speed=13.4   # a trailing note\n"
            "  crossing at=8.00 kind=signal\n"
            "  busstop cl.bus.08 at=40.00\n"
            "  # before end\n"
            "end\n"
            "# tail\n";
        Ntw::Document d = parseOk(s);
        CHECK(d.kind == "roads" && d.version == 1, "kind/version");
        CHECK(d.headerField("district") && std::string(d.headerField("district")->value) == "cl", "header field");
        CHECK(d.leadComments.size() == 1 && d.headerTrailing == " header note", "lead/header comments");
        CHECK(d.records.size() == 2, "top records %zu", d.records.size());
        const Ntw::Record& e = d.records[1];
        CHECK(e.type == "edge" && e.id == "cl.e7" && e.opensBlock, "edge");
        CHECK(e.children.size() == 2 && e.children[0].type == "crossing" && e.children[1].id == "cl.bus.08", "children");
        CHECK(std::string(e.value("name")) == "SW 8th St" && e.field("name")->quoted, "quoted");
        CHECK(std::string(e.value("color")) == "#2f4f4f", "colour with #");
        CHECK(std::string(e.value("speed")) == "13.4" && std::string(e.value("lanes")) == "1/1", "continuation");
        CHECK(e.trailing == " a trailing note", "trailing '%s'", e.trailing.c_str());
        CHECK(e.comments.size() == 1 && e.endComments.size() == 1, "comments %zu end %zu", e.comments.size(), e.endComments.size());
        CHECK(d.records[0].blankBefore && d.tailComments.size() == 1, "blank before / tail");
        vec2 p;
        CHECK(Ntw::toPoint(d.records[0].value("at"), &p) && p.x == 1.5f && p.y == 2.f, "point");
    }
    // errors
    CHECK(parseErrors("") == 1, "empty file");
    CHECK(parseErrors("node a\n") == 1, "no header");
    CHECK(parseErrors("ntw 1 x\nedge e1\n") == 1, "unclosed block");
    CHECK(parseErrors("ntw 1 x\nend\n") == 1, "stray end");
    CHECK(parseErrors("ntw 1 x\nnode a b=1 b=2\n") == 1, "duplicate key");
    CHECK(parseErrors("ntw 1 x\nnode a b=\"unterminated\n") == 1, "unterminated quote");
    CHECK(parseErrors("ntw 1 x\nx=1\n") == 1, "continuation without record");
    CHECK(parseErrors("ntw 1 x\nNode a\n") == 1, "uppercase type");
    CHECK(parseErrors("ntw 1 x\nnode A\n") == 1, "uppercase id");
    CHECK(parseErrors("ntw 1 x\nnode a b c=1\n") == 1, "bare word after id");
    CHECK(parseErrors("ntw 1 x\nnode a b=\n") == 1, "empty value");
    CHECK(parseErrors("ntw 1 x\nnode a\n# c\n  b=1\n") == 1, "continuation after comment");
    // values
    {
        double v;
        CHECK(Ntw::toNumber("-12.50", &v) && v == -12.5, "num");
        CHECK(!Ntw::toNumber("+1", &v) && !Ntw::toNumber("1.", &v) && !Ntw::toNumber(".5", &v) && !Ntw::toNumber("1 ", &v) && !Ntw::toNumber("0x10", &v), "strict num");
        std::vector<vec2> pts;
        CHECK(Ntw::toPoints("0,0;1.5,2;3,4", pts) && pts.size() == 3, "points");
        CHECK(!Ntw::toPoints("0,0;", pts) && !Ntw::toPoints("0,0,1", pts), "bad points");
        std::vector<std::string> l;
        CHECK(Ntw::toList("a,b,c", l) && l.size() == 3 && !Ntw::toList("a,,c", l), "list");
    }
    // ---- formatter
    {
        CHECK(Ntw::formatNumbers("1.5,2;-0.001,3", 2) == "1.50,2.00;0.00,3.00", "numbers '%s'", Ntw::formatNumbers("1.5,2;-0.001,3", 2).c_str());
        CHECK(Ntw::formatNumbers("both:2.4", 2) == "both:2.40" && Ntw::formatNumbers("1/1", 0) == "1/1" && Ntw::formatNumbers("lib.kit.a3", 2) == "lib.kit.a3", "mixed numbers");
        const std::string s = "ntw 1 roads\nnode n1   z=3.4  at=1,2\nedge e1 from=n1 to=n2\n  lanes=1/1\n crossing at=8\nend\n";
        Ntw::Document d = parseOk(s);
        Ntw::FormatRules rules;
        rules.fieldOrder = [](const std::string&, const std::string& type) -> const std::vector<std::string>* {
            static const std::vector<std::string> node = {"at", "z"};
            return type == "node" ? &node : nullptr;
        };
        rules.decimals = [](const std::string&, const std::string& type, const std::string& key) -> int { return type == "node" && (key == "at" || key == "z") ? 2 : -1; };
        const std::string f = Ntw::format(d, rules);
        const std::string want = "ntw 1 roads\nnode n1 at=1.00,2.00 z=3.40\nedge e1 from=n1 to=n2 lanes=1/1\n  crossing at=8\nend\n";
        CHECK(f == want, "format:\n%s---- want:\n%s", f.c_str(), want.c_str());
        Ntw::Document d2 = parseOk(f);
        CHECK(Ntw::format(d2, rules) == f, "idempotent");
        // comments survive, and long records wrap at 120 columns
        std::string longRec = "ntw 1 roads\n# c1\nnode n1";
        for (int i = 0; i < 30; i++) longRec += " k" + std::to_string(i) + "=" + std::to_string(i * 1000);
        longRec += " # tail\n";
        Ntw::Document d3 = parseOk(longRec);
        const std::string f3 = Ntw::format(d3, Ntw::FormatRules());
        size_t maxLine = 0, pos = 0, lines = 0;
        while (pos < f3.size()) {
            size_t e = f3.find('\n', pos);
            maxLine = Max(maxLine, e - pos);
            pos = e + 1;
            lines++;
        }
        CHECK(maxLine <= 120 + 8 && lines >= 4 && f3.find("# c1") != std::string::npos && f3.find("# tail") != std::string::npos, "wrap %zu lines max %zu:\n%s", lines, maxLine, f3.c_str());
        Ntw::Document d4 = parseOk(f3);
        CHECK(d4.records.size() == 1 && d4.records[0].fields.size() == 30, "wrapped record reparses");
        CHECK(Ntw::format(d4, Ntw::FormatRules()) == f3, "wrapped idempotent");
    }
    // ---- schema check on a terrain file
    {
        const std::string good = "ntw 1 terrain\ngrid res=2560 cell=8.00 origin=-10240.00,-10240.00\nlayer height file=h.ntg type=i16 unit=0.01 hash=00ff\nwater c1 level=0.0000\noutline lake pts=0,0;1,0;1,1\n";
        Ntw::Document d = parseOk(good, Ntw::schemaOpensBlock);
        std::vector<Ntw::Error> e;
        Ntw::checkDocument(d, e);
        CHECK(e.empty(), "schema good: %s", e.empty() ? "" : e[0].message.c_str());
        const std::string bad = "ntw 1 terrain\ngrid res=2.5 cell=8 origin=1\nlayer height file=h.ntg type=f32 hash=XY\nwater level=1\nfoo x=1\noutline lake\n";
        Ntw::Document db = parseOk(bad, Ntw::schemaOpensBlock);
        e.clear();
        Ntw::checkDocument(db, e);
        CHECK(e.size() >= 6, "schema bad: %zu errors", e.size());
        for (const Ntw::Error& x : e) printf("      (expected) %d: %s\n", x.line, x.message.c_str());
        Ntw::Document dk = parseOk("ntw 1 nosuchkind\n", Ntw::schemaOpensBlock);
        e.clear();
        Ntw::checkDocument(dk, e);
        CHECK(e.size() == 1, "unknown kind");
    }
    // ---- LZ
    {
        std::vector<std::vector<u8>> inputs;
        inputs.push_back({});
        inputs.push_back({7});
        std::vector<u8> text;
        for (int i = 0; i < 20000; i++) {
            const std::string line = "building cl.b" + std::to_string(i % 97) + ".h" + std::to_string(i % 13) + " plan=lib.plan.house.bungalow_b0" + std::to_string(i % 7) + " at=" + std::to_string(1700 + i % 300) + ".25,361.30\n";
            text.insert(text.end(), line.begin(), line.end());
        }
        inputs.push_back(text);
        std::vector<u8> rnd8(100000);
        for (auto& b : rnd8) b = (u8)rnd();
        inputs.push_back(rnd8);
        std::vector<u8> zeros(300000, 0);
        inputs.push_back(zeros);
        std::vector<u8> runs;
        for (int i = 0; i < 5000; i++) runs.insert(runs.end(), 1 + rnd() % 300, (u8)(rnd() % 4));
        inputs.push_back(runs);
        for (size_t k = 0; k < inputs.size(); k++) {
            const std::vector<u8>& in = inputs[k];
            std::vector<u8> z;
            lzCompress(in.data(), in.size(), z);
            std::vector<u8> back(in.size());
            const bool ok = lzDecompress(z.data(), z.size(), back.data(), back.size());
            CHECK(ok && back == in, "lz round trip %zu", k);
            printf("ok   lz input %zu: %zu -> %zu bytes\n", k, in.size(), z.size());
            // damaged and truncated streams fail or decode to something, never crash or overrun
            int rejected = 0;
            for (int t = 0; t < 200 && !z.empty(); t++) {
                std::vector<u8> bad = z;
                bad[rnd() % bad.size()] ^= (u8)(1u << (rnd() % 8));
                std::vector<u8> out2(in.size());
                rejected += !lzDecompress(bad.data(), bad.size(), out2.data(), out2.size());
            }
            for (size_t cut = 0; cut + 1 < z.size(); cut += 1 + z.size() / 23) {
                std::vector<u8> out2(in.size());
                CHECK(!lzDecompress(z.data(), cut, out2.data(), out2.size()) || in.empty(), "lz truncated %zu decoded", cut);
            }
            if (!z.empty()) printf("     %d/200 damaged streams rejected\n", rejected);
        }
    }
    // ---- LZ sections in world.bin
    {
        WorldBinWriter w;
        std::string txt;
        for (int i = 0; i < 2000; i++) txt += "manifest line " + std::to_string(i % 10) + "\n";
        w.addCompressed("text", std::vector<u8>(txt.begin(), txt.end()));
        w.addCompressed("tiny", std::vector<u8>{1, 2, 3});
        const std::vector<u8> bin = w.finish();
        WorldBinView v;
        std::string why;
        CHECK(v.open(bin.data(), bin.size(), &why), "open: %s", why.c_str());
        CHECK(v.find("text") && v.find("text")->kind == WSK_LZ && v.find("tiny")->kind == WSK_RAW, "kinds");
        std::vector<u8> b;
        CHECK(v.bytes("text", b) && std::string(b.begin(), b.end()) == txt, "lz section bytes");
        CHECK(v.bytes("tiny", b) && b.size() == 3, "raw section bytes");
        CHECK(!v.bytes("nothere", b), "missing section");
    }
    printf(fails ? "ntwtest: %d FAILURES\n" : "ntwtest: ALL OK\n", fails);
    return fails ? 1 : 0;
}
