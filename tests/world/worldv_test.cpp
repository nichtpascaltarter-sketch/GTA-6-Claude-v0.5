// Tests for worldv, the world validator (src/world/validate.h, tools/worldv.cpp). They cover:
// - the reference patterns;
// - each check on texts in memory;
// - the fixtures:
//   - tests/world/fixtures/samples: the format's samples, which are clean when checked as files;
//   - tests/world/fixtures/world: the samples at their places in a world, with the records they name, clean as a world;
//   - tests/world/fixtures/bad: one file per group of checks, each finding marked "# expect <check>" on its line
//     ("w:<check>" for a warning);
// - planted errors in copies of the fixture world;
// - the cache and incremental runs, --only, and threads.
// Build and run from the repo root, as CI's GCC job does:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined
//       tests/world/worldv_test.cpp -o /tmp/neontide-worldv-test && /tmp/neontide-worldv-test
#include "../../tools/native_stubs.cpp"
#include "../../src/world/ntw.cpp"
#include "../../src/world/ntwschema.cpp"
#include "../../src/world/validate.cpp"
#include "../../src/world/validate_layout.cpp"
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <cstdio>

using namespace World;
using namespace World::Validate;
namespace fs = std::filesystem;

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

static const char* const kFixtures = "tests/world/fixtures";
static std::set<std::string> gSeen;   // every check id some test saw reported

static void note(const std::vector<Diagnostic>& ds) {
    for (const Diagnostic& d : ds) gSeen.insert(d.check);
}

static void dump(const std::vector<Diagnostic>& ds) {
    for (const Diagnostic& d : ds) printf("    %s\n", formatDiagnostic(d).c_str());
}

static std::string readText(const fs::path& p) {
    std::string s;
    validate_detail::readFile(p.string(), s);
    return s;
}

static void writeText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    FILE* f = fopen(p.string().c_str(), "wb");
    if (!f) {
        EXPECT(false, "can't write %s", p.string().c_str());
        return;
    }
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

static fs::path tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() /
                       ("worldv_test_" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + name);
    fs::create_directories(d);
    return d;
}

static std::vector<Diagnostic> runWorld(const std::string& world, Summary* sum = nullptr, const std::string& only = std::string(), int jobs = 1) {
    Options opt;
    opt.world = world;
    opt.only = only;
    opt.jobs = jobs;
    std::vector<Diagnostic> ds;
    Summary s;
    run(opt, ds, s);
    note(ds);
    if (sum) *sum = s;
    return ds;
}

static std::vector<Diagnostic> textChecks(const std::string& text) {
    std::vector<Diagnostic> ds;
    checkText("test.ntw", text, ds);
    note(ds);
    return ds;
}

static bool only(const std::vector<Diagnostic>& ds, const char* check, int line) {
    return ds.size() == 1 && ds[0].check == check && ds[0].line == line;
}

// ------------------------------------------------------------------------------------------------- patterns, helpers

static bool isItemKind(const char* kind) { return !strcmp(kind, "layout") || !strcmp(kind, "unit") || !strcmp(kind, "fitout"); }

static void testPatterns() {
    using namespace validate_detail;
    std::vector<std::vector<Named>> g;
    const Pattern edgeSide = parsePattern("<edge>:*");
    EXPECT(matchPattern(edgeSide, "cl.e0007:right", g) && g.size() == 1 && g[0].size() == 1 && g[0][0].cls == "edge" && g[0][0].id == "cl.e0007", "<edge>:*");
    EXPECT(!matchPattern(edgeSide, "cl.e0007", g) && !matchPattern(edgeSide, "cl.e0007:", g) && !matchPattern(edgeSide, "cl.e0007:a:b", g), "<edge>:* misses");
    const Pattern stops = parsePattern("all|<edge>,...");
    EXPECT(matchPattern(stops, "all", g) && g.size() == 1 && g[0].empty(), "all");
    EXPECT(matchPattern(stops, "cl.e0007,cl.e0009", g) && g.size() == 2 && g[1][0].id == "cl.e0009", "a list");
    EXPECT(!matchPattern(stops, "cl.e0007,,cl.e0009", g) && !matchPattern(stops, "Cl.e1", g), "list misses");
    const Pattern leads = parsePattern("out:<.room>|<.room>:out|<.room>:<.room>");
    EXPECT(matchPattern(leads, "out:living", g) && g[0].size() == 1 && g[0][0].cls == ".room", "out:room");
    EXPECT(matchPattern(leads, "lobby:stair", g) && g[0].size() == 2, "room:room");
    EXPECT(!matchPattern(leads, "living", g), "a lone room");
    const Pattern forSpot = parsePattern("<busstop>|<building>:<f.door|f.counter>");
    EXPECT(matchPattern(forSpot, "cl.b014.s07:f0.d1", g) && g[0].size() == 2 && g[0][1].cls == "f.door|f.counter", "building:floor element");
    EXPECT(matchPattern(forSpot, "cl.bus.08", g) && g[0][0].cls == "busstop", "a bus stop");
    const Pattern allow = parsePattern("<unit|fitout>,...");
    EXPECT(matchPattern(allow, "lib.unit.walkup.*,lib.fitout.cafe_a", g) && g.size() == 2 && g[0][0].id == "lib.unit.walkup.*", "a family");
    EXPECT(!matchPattern(allow, "lib.unit.*x", g), "a bad family");
    const Pattern lot = parsePattern("<edge>:*:*");
    EXPECT(matchPattern(lot, "cl.e0007:72.00:right", g) && !matchPattern(lot, "cl.e0007:right", g), "<edge>:*:*");

    EXPECT(numbered("cl.n0012", "cl.n", 4) && numbered("cl.n12345", "cl.n", 4) && !numbered("cl.n012", "cl.n", 4) && !numbered("cl.n00a2", "cl.n", 4) &&
               !numbered("cl.e0012", "cl.n", 4),
           "numbered");
    EXPECT(isDistrictId("cl") && isDistrictId("d2") && !isDistrictId("2d") && !isDistrictId("c_l") && !isDistrictId(""), "district ids");
    EXPECT(classWords("plan") == "plan" && classWords("parcel|lot") == "parcel or lot" && classWords("f.door|f.counter") == "door or counter" &&
               classWords("kit|prop") == "kit or prop piece" && classWords(".wall") == "wall" && classWords("a|b|c") == "a, b or c",
           "class words");

    {   // the layout checks' geometry
        using namespace layout_detail;
        const std::vector<V> square = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        EXPECT(where({2, 2}, square) == 1 && where({5, 2}, square) == -1 && where({4.01, 2}, square) == 0 && where({0, 0}, square) == 0, "where");
        EXPECT(std::fabs(area(square) - 16.0) < 1e-9 && std::fabs(centroid(square).x - 2.0) < 1e-9, "area and centroid");
        V x;
        EXPECT(crosses({0, 0}, {4, 4}, {0, 4}, {4, 0}, &x) && std::fabs(x.x - 2.0) < 1e-9, "an X crosses");
        EXPECT(!crosses({0, 0}, {4, 0}, {2, 0}, {2, 3}, nullptr) && !crosses({0, 0}, {4, 0}, {4, 0}, {4, 3}, nullptr), "a T or an L doesn't");
        V c;
        double r = 0;
        EXPECT(fitCircle({{5.55, 3.5}, {5.0, 4.05}, {4.45, 3.5}, {5.0, 2.95}}, &c, &r) && std::fabs(c.x - 5.0) < 1e-6 && std::fabs(c.y - 3.5) < 1e-6 && std::fabs(r - 0.55) < 1e-6,
               "a fitted circle");
        EXPECT(!fitCircle({{0, 0}, {1, 1}, {2, 2}}, &c, &r), "points on a line fit no circle");
        EXPECT(std::fabs(rayHit({1, 1}, {0, 1}, {0, 3}, {4, 3}) - 2.0) < 1e-9 && rayHit({1, 1}, {0, -1}, {0, 3}, {4, 3}) < 0, "a ray's hit");
    }

    // the schema's table: every pattern parses, and names only classes that worldv defines
    const std::set<std::string> globals = {"district", "node",  "edge", "busstop", "block", "parcel", "lot", "building", "line", "plan", "layout", "unit",
                                           "fitout",   "set",   "facade", "signal", "palette", "kit",  "prop", "veg",      "light", "fence"};
    const std::set<std::string> elements = {"wall", "room", "door", "window", "stair", "lift", "slot", "counter", "light", "spot"};
    for (const Ntw::RefDef& d : Ntw::schemaRefs()) {
        const Pattern p = parsePattern(d.pattern);
        EXPECT(!p.alts.empty(), "%s/%s: an empty pattern", d.kind, d.type);
        for (const PatAlt& a : p.alts)
            for (const PatPart& part : a.parts) {
                EXPECT(part.kind != 'l' || !part.text.empty(), "%s/%s %s: an empty part in %s", d.kind, d.type, d.key ? d.key : "(id)", d.pattern);
                if (part.kind != 'r') continue;
                const std::vector<std::string> cs = split(part.text, '|');
                for (const std::string& c : cs) {
                    bool known = globals.count(c) > 0 || c == "floor";
                    if (startsWith(c, ".")) known = c == ".option" ? std::string(d.kind) == "plan" : elements.count(c.substr(1)) > 0 && isItemKind(d.kind);
                    if (startsWith(c, "f.")) known = elements.count(c.substr(2)) > 0;
                    EXPECT(known, "%s/%s %s: worldv doesn't know the class %s here (pattern %s)", d.kind, d.type, d.key ? d.key : "(id)", c.c_str(), d.pattern);
                    // one reference's classes are all world-wide, all the item's elements, or all floor elements
                    EXPECT(isLocalClass(c) == isLocalClass(cs[0]) && isFloorClass(c) == isFloorClass(cs[0]), "%s/%s: the classes of %s mix kinds", d.kind, d.type,
                           part.text.c_str());
                }
            }
    }
    for (const Ntw::KindDef& k : Ntw::schemaKinds())
        for (const Ntw::RecordDef& r : k.records)
            for (const Ntw::FieldDef& f : r.fields) {
                if (f.type != Ntw::VT_REF) continue;
                bool row = false;
                for (const Ntw::RefDef& d : Ntw::schemaRefs()) row = row || (k.kind == std::string(d.kind) && r.type == std::string(d.type) && d.key && f.key == std::string(d.key));
                EXPECT(row, "%s/%s %s: a reference field without a row in schemaRefs()", k.kind, r.type, f.key);
            }
}

// --------------------------------------------------------------------------------------------- one file, in memory

static void testTexts() {
    const std::string ok = "ntw 1 facade id=lib.facade.ok\nbase h=0.20\n";
    EXPECT(textChecks(ok).empty(), "a clean file");
    std::vector<Diagnostic> ds = textChecks("\xEF\xBB\xBF" + ok);
    EXPECT(only(ds, "file.canonical", 1) && ds[0].message.find("byte-order mark") != std::string::npos, "a byte-order mark");
    ds = textChecks("ntw 1 facade id=lib.facade.ok\r\nbase h=0.20\r\n");
    EXPECT(only(ds, "file.canonical", 1) && ds[0].message.find("CRLF") != std::string::npos, "CRLF");
    ds = textChecks(ok.substr(0, ok.size() - 1));
    EXPECT(only(ds, "file.canonical", 2) && ds[0].message.find("line end") != std::string::npos, "no last line end");
    ds = textChecks("ntw 1 facade id=lib.facade.ok\n\n\nbase h=0.20\n");
    EXPECT(only(ds, "file.canonical", 3), "two blank lines");
    ds = textChecks("ntw 1 facade id=lib.facade.ok\nbase h=0.2\n");
    EXPECT(only(ds, "file.canonical", 2) && ds[0].message.find("h=0.20") != std::string::npos, "precision");
    ds = textChecks("ntw 1 facade id=lib.facade.ok\nbase h=0.20\n  material=lib.kit.mat\n");
    EXPECT(only(ds, "file.canonical", 2), "a needless continuation line");
    ds = textChecks("ntw 1 nosuchkind\n");
    EXPECT(only(ds, "file.schema", 1), "an unknown kind");
    ds = textChecks("node a\n");
    EXPECT(only(ds, "file.parse", 1), "no header");
    // a file with schema errors reports only those, but its ids still count for the rest of the world
    ds = textChecks("ntw 1 layout id=lib.layout.test.ok\nwall w1 a=0.00,0.00 b=1.00,0.00\ndoor d1 wall=w9 at=0.50 w=0.90 h=2.10 kind=hinged leads=out:hall\n");
    EXPECT(only(ds, "file.schema", 2), "quiet after schema errors");
    {
        validate_detail::File f;
        f.path = f.rel = "x.ntw";
        f.text = "ntw 1 layout id=lib.layout.test.ok\nwall w1 a=0.00,0.00 b=1.00,0.00\n";
        validate_detail::checkFile(f, true, false);
        EXPECT(!f.res.valid && f.res.defs.size() == 1 && f.res.defs[0].id == "lib.layout.test.ok" && f.res.scoped.size() == 1, "ids of a file with schema errors");
    }
    Diagnostic d;
    d.file = "world/x.ntw";
    d.line = 3;
    d.check = "ref.missing";
    d.message = "m";
    EXPECT(formatDiagnostic(d) == "world/x.ntw:3: error: m [ref.missing]", "diagnostic format");
    d.line = 0;
    d.severity = SEV_WARNING;
    EXPECT(formatDiagnostic(d) == "world/x.ntw: warning: m [ref.missing]", "diagnostic format, whole file");
    std::set<std::string> ids;
    for (const CheckInfo& c : checkList()) {
        EXPECT(ids.insert(c.id).second, "check %s listed twice", c.id);
        EXPECT(strlen(c.what) > 10, "check %s has no description", c.id);
    }
}

// ------------------------------------------------------------------------------------------------------- fixtures

static void testSamples() {
    Options opt;
    opt.files.push_back(std::string(kFixtures) + "/samples");
    std::vector<Diagnostic> ds;
    Summary s;
    run(opt, ds, s);
    note(ds);
    // the samples are whole (the city completed the two layout samples in its step 4.1): no findings
    std::vector<std::string> got;
    for (const Diagnostic& d : ds) got.push_back(d.file.substr(d.file.rfind('/') + 1) + ":" + std::to_string(d.line) + " " + d.check);
    const std::vector<std::string> want = {};
    EXPECT(got == want && s.files == 16 && !s.world, "the 16 samples, checked as files: %zu files, %zu diagnostics", s.files, ds.size());
    if (got != want) dump(ds);
}

// The "# expect" marks of a file: line, severity and check, one entry per finding. "w:<check>" is a warning, and a
// mark starting with ^ stands for its record's first line (a record's own findings are reported there, and its
// end-of-line comment sits on its last line)
static std::multiset<std::string> expectations(const std::string& text) {
    std::multiset<std::string> out;
    const std::vector<std::string> lines = validate_detail::split(text, '\n');
    int record = 0;   // the line of the record the current line belongs to
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& l = lines[i];
        const size_t first = l.find_first_not_of(' ');
        const size_t space = l.find(' ', first == std::string::npos ? 0 : first);
        const std::string token = first == std::string::npos ? std::string() : l.substr(first, space == std::string::npos ? std::string::npos : space - first);
        if (token.find('=') == std::string::npos) record = (int)i + 1;   // (a continuation line starts with key=value)
        const size_t mark = l.find("# expect ");
        if (mark == std::string::npos) continue;
        for (std::string t : validate_detail::split(l.substr(mark + 9), ' ')) {
            if (t.empty()) continue;
            int line = (int)i + 1;
            if (t[0] == '^') {
                line = record;
                t = t.substr(1);
            }
            out.insert(std::to_string(line) + (t.compare(0, 2, "w:") == 0 ? " warning " + t.substr(2) : " error " + t));
        }
    }
    return out;
}

// Every file under `dir` has exactly the findings its # expect marks name (`marked`: each file must have some)
static void matchMarks(const std::string& dir, const std::vector<Diagnostic>& ds, bool marked) {
    std::map<std::string, std::multiset<std::string>> got;
    for (const Diagnostic& d : ds) got[d.file].insert(std::to_string(d.line) + (d.severity == SEV_ERROR ? " error " : " warning ") + d.check);
    std::vector<std::string> files;
    std::vector<Diagnostic> problems;
    validate_detail::listTree(dir, files, problems);
    for (const std::string& f : files) {
        const std::multiset<std::string> want = expectations(readText(f));
        EXPECT(!marked || !want.empty(), "%s: no expectations", f.c_str());
        if (got[f] == want) continue;
        EXPECT(false, "%s: the findings differ from its # expect marks", f.c_str());
        for (const std::string& w : want)
            if (!got[f].count(w)) printf("    expected, not found: %s\n", w.c_str());
        for (const std::string& g : got[f])
            if (!want.count(g)) printf("    found, not expected: %s\n", g.c_str());
    }
}

static void testBad() {
    Options opt;
    const std::string dir = std::string(kFixtures) + "/bad";
    opt.files.push_back(dir);
    std::vector<Diagnostic> ds;
    Summary s;
    run(opt, ds, s);
    note(ds);
    EXPECT(s.files >= 20, "bad fixtures: %zu files", s.files);
    matchMarks(dir, ds, true);
}

// The plan pass needs a world: tests/world/fixtures/plans holds plans with their layouts, their findings marked
static void testPlanPass() {
    const std::string dir = std::string(kFixtures) + "/plans";
    Summary s;
    const std::vector<Diagnostic> ds = runWorld(dir, &s);
    EXPECT(s.files == 13 && s.world, "the plan pass's world: %zu files", s.files);
    matchMarks(dir, ds, false);
    // checked as files instead, the plan pass doesn't run (it reads the floors' layouts through the plan)
    Options opt;
    opt.files.push_back(dir);
    std::vector<Diagnostic> files;
    run(opt, files, s);
    bool any = false;
    for (const Diagnostic& d : files) any = any || d.check == "stair.floors" || d.check == "lift.stops" || d.check == "floor.access" || d.check == "door.height";
    EXPECT(!any, "the plan pass ran in a file run");
}

static void testWorld() {
    Summary s;
    std::vector<Diagnostic> ds = runWorld(std::string(kFixtures) + "/world", &s);
    EXPECT(ds.empty() && s.files == 30 && s.world && s.ids >= 60 && s.refs >= 170, "the fixture world: %zu files, %zu ids, %zu references, %zu diagnostics", s.files,
           s.ids, s.refs, ds.size());
    dump(ds);
    // checked as files instead, it has no checks across files to fail either
    Options opt;
    opt.files.push_back(std::string(kFixtures) + "/world");
    run(opt, ds, s);
    // without the plans, the attic (a hatch) and the roof (another floor's stair) have no way in
    EXPECT(ds.size() == 2 && ds[0].file.find("bungalow_b02_attic.ntw") != std::string::npos && ds[1].file.find("walkup_a_roof.ntw") != std::string::npos &&
               ds[0].check == "room.reach" && ds[0].severity == SEV_WARNING && s.files == 30 && !s.world,
           "the fixture world as files: %zu findings", ds.size());
    ds = runWorld(std::string(kFixtures) + "/no_such_world", &s);
    EXPECT(ds.size() == 1 && ds[0].check == "file.read" && s.errors == 1, "no world directory");
}

// ------------------------------------------------------------------------------------------------ planted errors

struct Edit {
    const char* file;    // relative to the world
    const char* from;    // nullptr: a new file with `to` as its text; "" with `to` empty: delete; "->": rename to `to`
    const char* to;
};
struct Plant {
    const char* name;
    std::vector<Edit> edits;
    std::vector<const char*> want;   // "<file>:<line> <check>", sorted as reported
};

static bool apply(const fs::path& world, const Edit& e) {
    const fs::path p = world / e.file;
    if (!e.from) {
        writeText(p, e.to);
        return true;
    }
    if (std::string(e.from) == "->") {
        std::error_code ec;
        fs::rename(p, world / e.to, ec);
        return !ec;
    }
    std::string s = readText(p);
    if (std::string(e.from).empty()) {
        std::error_code ec;
        return fs::remove(p, ec);
    }
    const size_t at = s.find(e.from);
    if (at == std::string::npos || s.find(e.from, at + 1) != std::string::npos) return false;
    s.replace(at, strlen(e.from), e.to);
    writeText(p, s);
    return true;
}

static std::vector<std::string> brief(const std::vector<Diagnostic>& ds, const std::string& world) {
    std::vector<std::string> out;
    for (const Diagnostic& d : ds) out.push_back(d.file.substr(world.size() + 1) + ":" + std::to_string(d.line) + " " + d.check);
    return out;
}

static fs::path copyWorld(const char* name) {
    const fs::path dir = tempDir(name) / "world";
    fs::copy(std::string(kFixtures) + "/world", dir, fs::copy_options::recursive);
    return dir;
}

// A typical floor's layout names its stair's floors relative to each floor that uses it (NTW_FORMAT 9.8): 0:<room> on
// that floor, +1:<room> / -1:<room> on the next floor up / down by height. The plans fixture has a clean tower (tower_b),
// a typical floor on the top floor (tower_c: +1 has no floor there) and one whose stair runs down (tower_d).
static fs::path copyFixture(const char* fixture, const char* name) {
    const fs::path dir = tempDir(name) / fixture;
    fs::copy(std::string(kFixtures) + "/" + fixture, dir, fs::copy_options::recursive);
    return dir;
}

static void testRelativeStairs() {
    const std::string dir = std::string(kFixtures) + "/plans";
    const std::vector<Diagnostic> ds = runWorld(dir);
    auto messages = [&](const char* file, int line) {
        std::vector<std::string> m;
        for (const Diagnostic& d : ds)
            if (validate_detail::endsWith(d.file, file) && d.line == line && d.check == "stair.floors") m.push_back(d.message);
        return m;
    };
    const std::vector<std::string> top = messages("lib/layouts/test/tower_c_typ.ntw", 11), down = messages("lib/layouts/test/tower_d_typ.ntw", 10);
    EXPECT(top.size() == 1 && top[0] == "stair st1: to=+1:stair names the floor above, but floor f2 is the top floor of plan lib.plan.test.tower_c, "
                                         "with no floor above it",
           "+1 off the top: %s", top.empty() ? "nothing" : top[0].c_str());
    EXPECT(down.size() == 1 && down[0] == "stair st1: to=-1:stair isn't above from=0:stair (plan lib.plan.test.tower_d)", "a stair down: %s",
           down.empty() ? "nothing" : down[0].c_str());
    // the clean tower, its top floor reached through the typical floor's stair (no room.reach for its layout)
    for (const Diagnostic& d : ds) EXPECT(d.file.find("tower_b") == std::string::npos, "tower_b: %s:%d %s", d.file.c_str(), d.line, d.check.c_str());
    // on copies: what the relative ends resolve to, checked per floor
    const std::vector<Plant> plants = {
        // the floor above uses a layout without the room: the reference check sees only the stair's own layout
        {"the room above isn't there", {{"lib/layouts/test/tower_b_top.ntw", "room stair type=stair", "room landing type=stair"}, {"lib/layouts/test/tower_b_top.ntw", "hall:stair", "hall:landing"}},
         {"lib/layouts/test/tower_b_typ.ntw:10 stair.floors", "lib/layouts/test/tower_b_top.ntw:1 room.reach"}},
        // treads and risers against every pair of floors the layout joins: one finding for all of them
        {"risers that don't climb a typical floor", {{"lib/layouts/test/tower_b_typ.ntw", "kind=straight", "kind=straight treads=18 rise=0.17"}},
         {"lib/layouts/test/tower_b_typ.ntw:10 stair.floors"}},
        {"treads derived from the typical floor's height", {{"lib/layouts/test/tower_b_typ.ntw", "run=1.00,5.50;6.50,5.50", "run=1.00,5.50;4.00,5.50"}},
         {"lib/layouts/test/tower_b_typ.ntw:10 stair.tread"}},
        // the next floor up is the next by height, wherever the plan lists it
        {"floors listed out of order", {{"lib/plans/test/tower_b.ntw", "floor f1 layout=lib.layout.test.tower_b_typ z=3.00 h=3.00\nfloor f2 layout=lib.layout.test.tower_b_typ z=6.00 h=3.00",
                                         "floor f2 layout=lib.layout.test.tower_b_typ z=6.00 h=3.00\nfloor f1 layout=lib.layout.test.tower_b_typ z=3.00 h=3.00"}},
         {}},
        // the top floor taken away: f2's stair has nowhere to go, and nothing is left to reach tower_b_top
        {"the typical floor becomes the top", {{"lib/plans/test/tower_b.ntw", "floor f3 layout=lib.layout.test.tower_b_top z=9.00 h=3.00\n", ""}},
         {"lib/layouts/test/tower_b_typ.ntw:10 stair.floors", "lib/layouts/test/tower_b_top.ntw:1 room.reach"}},
    };
    for (const Plant& p : plants) {
        const fs::path world = copyFixture("plans", "relstair");
        bool applied = true;
        for (const Edit& e : p.edits) applied = apply(world, e) && applied;
        EXPECT(applied, "%s: an edit didn't apply", p.name);
        std::vector<std::string> want(p.want.begin(), p.want.end()), got;
        // (the fixture's own marked findings in tower_a, tower_c and tower_d stay: compare what's new)
        for (const std::string& g : brief(runWorld(world.generic_string()), world.generic_string()))
            if (g.find("tower_b") != std::string::npos) got.push_back(g);
        std::sort(want.begin(), want.end());
        std::sort(got.begin(), got.end());
        EXPECT(got == want, "%s: the findings differ", p.name);
        if (got != want) {
            for (const std::string& g : got) printf("    found: %s\n", g.c_str());
            for (const std::string& w : want) printf("    wanted: %s\n", w.c_str());
        }
        fs::remove_all(world.parent_path());
    }
}

static void testPlants() {
    const char* unit = "ntw 1 unit id=lib.unit.walkup.studio_a slot_size=6.00x5.00 kind=studio\n"
                       "wall w1 a=0.00,0.00 b=6.00,0.00 t=0.12\n"
                       "room studio type=living poly=0.00,0.00;6.00,0.00;6.00,5.00;0.00,5.00 cap=2\n"
                       "door d1 wall=w1 at=3.00 w=0.90 h=2.10 kind=hinged leads=out:studio\n";
    const char* slots = "ntw 1 layout id=lib.layout.test.slots\n"
                        "wall w1 a=0.00,0.00 b=6.00,0.00 t=0.25 ext=front\n"
                        "wall w2 a=6.00,0.00 b=6.00,1.20 t=0.25 ext=right\n"
                        "wall w3 a=6.00,1.20 b=0.00,1.20 t=0.12\n"
                        "wall w4 a=0.00,1.20 b=0.00,0.00 t=0.25 ext=left\n"
                        "room hall type=corridor poly=0.00,0.00;6.00,0.00;6.00,1.20;0.00,1.20\n"
                        "door d1 wall=w1 at=1.00 w=0.90 h=2.10 kind=hinged leads=out:hall\n"
                        "slot u1 poly=0.00,1.20;6.00,1.20;6.00,6.20;0.00,6.20 entry=d1 allow=lib.unit.walkup.*\n"
                        "slot u2 poly=0.00,1.20;6.00,1.20;6.00,6.20;0.00,6.20 entry=d1 allow=lib.unit.none.*\n"
                        "slot u3 poly=0.00,1.20;6.00,1.20;6.00,6.20;0.00,6.20 entry=d1 allow=lib.layout.midrise.core_a,lib.unit.walkup.studio_a\n";
    const std::vector<Plant> plants = {
        {"an edge to no node", {{"districts/cl/roads.ntw", "to=cl.n0013 class=street name", "to=cl.n0099 class=street name"}}, {"districts/cl/roads.ntw:6 ref.missing"}},
        {"a curb cut to a path", {{"districts/cl/roads.ntw", "to=cl.b014.lot1", "to=cl.b014.pa1"}}, {"districts/cl/roads.ntw:17 ref.kind"}},
        {"a junction at no node", {{"districts/cl/roads.ntw", "junction cl.n0012", "junction cl.n0016"}}, {"districts/cl/roads.ntw:23 ref.missing"}},
        {"a front without its side", {{"districts/cl/blocks/b014/block.ntw", "front=cl.e0007:right", "front=cl.e0007"}}, {"districts/cl/blocks/b014/block.ntw:3 ref.form"}},
        {"a queue for another building's missing door",
         {{"districts/cl/blocks/b014/block.ntw", "for=cl.b014.s07:f0.d1", "for=cl.b014.s07:f0.d7"}},
         {"districts/cl/blocks/b014/block.ntw:19 ref.missing"}},
        {"an override of a missing room", {{"districts/cl/blocks/b014/h03.ntw", "room=f0.kitchen", "room=f0.cellar"}}, {"districts/cl/blocks/b014/h03.ntw:5 ref.missing"}},
        {"an override of a door as a wall", {{"districts/cl/blocks/b014/h03.ntw", "wall=f0.w2", "wall=f0.d1"}}, {"districts/cl/blocks/b014/h03.ntw:6 ref.kind"}},
        {"a building on a missing parcel", {{"districts/cl/blocks/b014/h03.ntw", "parcel=cl.b014.p03", "parcel=cl.b014.p09"}}, {"districts/cl/blocks/b014/h03.ntw:1 ref.missing"}},
        {"an entrance on a missing floor", {{"lib/plans/house/bungalow_b02.ntw", "door=f0.d1 kind=main", "door=f9.d1 kind=main"}}, {"lib/plans/house/bungalow_b02.ntw:10 ref.missing"}},
        // (and the shaft's other floors then stop elsewhere: the plan pass reports both sides)
        {"a lift stop on a floor its plan doesn't have", {{"lib/layouts/midrise/core_a.ntw", "stops=f0,f1,f2,f3", "stops=f0,f1,f2,f4"}},
         {"lib/layouts/midrise/core_a.ntw:22 ref.missing", "lib/layouts/midrise/core_a.ntw:22 lift.stops", "lib/layouts/midrise/walkup_a_f1.ntw:14 lift.stops"}},
        {"a stair to a missing room", {{"lib/layouts/midrise/core_a.ntw", "to=f1.stair", "to=f1.stairs"}}, {"lib/layouts/midrise/core_a.ntw:18 ref.missing"}},
        // the door= of a missing building isn't reported twice
        {"an anchor at a missing building", {{"places/places.ntw", "building=cl.b031.x01", "building=cl.b031.x09"}}, {"places/places.ntw:3 ref.missing"}},
        {"an anchor in a missing district", {{"places/places.ntw", "anchor cl.ev.busk1", "anchor zz.ev.busk1"}}, {"places/places.ntw:4 ref.missing"}},
        {"a stop and a route edge that don't exist",
         {{"transit/bus.8.ntw", "busstop=cl.bus.08", "busstop=cl.bus.09"}, {"transit/bus.8.ntw", "edges=cl.e0007,cl.e0011", "edges=cl.e0007,cl.e0012"}},
         {"transit/bus.8.ntw:2 ref.missing", "transit/bus.8.ntw:3 ref.missing"}},
        {"a trunk node that doesn't exist", {{"districts/cl/district.ntw", "node=tr.n00412", "node=tr.n00413"}}, {"districts/cl/district.ntw:9 ref.missing"}},
        {"a building twice", {{"districts/cl/blocks/b014/s08.ntw", nullptr, "ntw 1 building id=cl.b014.s07 block=cl.b014 parcel=cl.b014.p04\n"}},
         {"districts/cl/blocks/b014/s08.ntw:1 id.path", "districts/cl/blocks/b014/s08.ntw:1 id.unique"}},
        {"a line in the wrong file", {{"transit/bus.8.ntw", "->", "transit/bus8.ntw"}}, {"transit/bus8.ntw:1 id.path"}},
        {"a district renamed in its own file",
         {{"districts/cl/district.ntw", "district=cl", "district=cm"}},
         {"districts/cl/district.ntw:1 id.path", "districts/cl/district.ntw:1 ref.missing", "districts/cl/district.ntw:4 id.form", "districts/cl/district.ntw:5 id.form",
          "districts/cl/district.ntw:6 id.form"}},
        {"a catalog piece removed",
         {{"lib/catalog.ntw", "piece lib.kit.tv_stand code=FURN_TV_STAND size=1.40x0.45x0.55 collide=box\n", ""}},
         {"lib/layouts/house/bungalow_b02_f0.ntw:36 ref.missing", "lib/sets/living/family_04.ntw:4 ref.missing"}},
        {"slots allowing families and items", {{"lib/units/walkup/studio_a.ntw", nullptr, unit}, {"lib/layouts/test/slots.ntw", nullptr, slots}},
         {"lib/layouts/test/slots.ntw:9 ref.missing", "lib/layouts/test/slots.ntw:10 ref.kind"}},
        {"a building's slots", {{"districts/cl/blocks/b014/m01.ntw", nullptr, "ntw 1 building id=cl.b014.m01 block=cl.b014 parcel=cl.b014.p05\n"
                                                                             "place plan=lib.plan.midrise.walkup_a at=1692.00,375.00 z=3.45 yaw=0.0\n"
                                                                             "slot f1.u9 unit=lib.unit.walkup.studio_a\n"
                                                                             "slot f1.d1 unit=lib.unit.walkup.studio_a\n"},
                                {"lib/units/walkup/studio_a.ntw", nullptr, unit}},
         {"districts/cl/blocks/b014/m01.ntw:3 ref.missing", "districts/cl/blocks/b014/m01.ntw:4 ref.kind"}},
        // floors that nothing on them leads into, reached through a hatch or another floor's stair, or not
        {"an attic without its hatch", {{"lib/plans/house/bungalow_b02.ntw", " access=hatch:f0.hall", ""}}, {"lib/layouts/house/bungalow_b02_attic.ntw:1 room.reach"}},
        {"a roof without its hatch", {{"lib/plans/midrise/walkup_a.ntw", " access=hatch:f3.hall", ""}}, {"lib/layouts/midrise/walkup_a_roof.ntw:1 room.reach"}},
        {"a roof reached by the stair instead", {{"lib/plans/midrise/walkup_a.ntw", " access=hatch:f3.hall", ""}, {"lib/layouts/midrise/core_a.ntw", "to=f1.flat", "to=roof.plant"}},
         {"lib/layouts/midrise/core_a.ntw:20 stair.floors"}},
        // the plan pass: stairs and lifts against the floors they claim, hatches, heights
        {"risers that don't climb to the next floor", {{"lib/layouts/midrise/core_a.ntw", "treads=18 rise=0.16", "treads=18 rise=0.17"}},
         {"lib/layouts/midrise/core_a.ntw:18 stair.floors"}},
        {"a stair down", {{"lib/layouts/midrise/core_a.ntw", "stair st1 from=f0.stair to=f1.stair", "stair st1 from=f1.stair to=f0.stair"}},
         {"lib/layouts/midrise/core_a.ntw:18 stair.floors"}},
        {"a stair's top outside the room it names", {{"lib/layouts/midrise/core_a.ntw", "to=f1.stair", "to=f1.hall"}}, {"lib/layouts/midrise/core_a.ntw:18 stair.floors"}},
        {"treads derived from the floors, past the kit's defaults", {{"lib/layouts/midrise/core_a.ntw", "kind=dogleg treads=18 rise=0.16\n  landing=1.10", "kind=dogleg landing=1.10"}},
         {"lib/layouts/midrise/core_a.ntw:18 stair.tread"}},
        {"a lift a floor up out of line", {{"lib/layouts/midrise/walkup_a_f1.ntw", "lift lf1 at=4.50,7.50", "lift lf1 at=4.60,7.50"}},
         {"lib/layouts/midrise/core_a.ntw:22 lift.stops", "lib/layouts/midrise/walkup_a_f1.ntw:14 lift.stops"}},
        {"a hatch from its own floor", {{"lib/plans/house/bungalow_b02.ntw", "access=hatch:f0.hall", "access=hatch:attic.loft"}}, {"lib/plans/house/bungalow_b02.ntw:5 floor.access"}},
        {"a floor too low for its doors and windows", {{"lib/plans/shop/corner_a.ntw", "z=0.00 h=3.60", "z=0.00 h=2.00"}},
         {"lib/layouts/shop/corner_a_f0.ntw:9 door.height", "lib/layouts/shop/corner_a_f0.ntw:10 door.height", "lib/layouts/shop/corner_a_f0.ntw:11 window.height",
          "lib/layouts/shop/corner_a_f0.ntw:12 window.height"}},
        {"a layout no plan uses, with no way in",
         {{"lib/layouts/test/loft.ntw", nullptr, "ntw 1 layout id=lib.layout.test.loft\nroom loft type=store poly=0.00,0.00;4.00,0.00;4.00,4.00;0.00,4.00 outdoor=1\n"}},
         {"lib/layouts/test/loft.ntw:1 room.reach"}},
        {"a world without its district", {{"world.ntw", "district cl name=\"Calle Luna\" label=1560.00,350.00 order=1\n", ""}},
         {"districts/cl/blocks/b014/block.ntw:1 ref.missing", "districts/cl/district.ntw:1 ref.missing", "districts/cl/roads.ntw:1 ref.missing",
          "districts/cl/blocks/b031/block.ntw:1 ref.missing", "places/places.ntw:3 ref.missing", "places/places.ntw:4 ref.missing"}},
    };
    for (const Plant& p : plants) {
        const fs::path world = copyWorld("plant");
        bool applied = true;
        for (const Edit& e : p.edits) applied = apply(world, e) && applied;
        EXPECT(applied, "%s: an edit didn't apply", p.name);
        std::vector<std::string> want(p.want.begin(), p.want.end());
        std::vector<std::string> got = brief(runWorld(world.generic_string()), world.generic_string());
        std::sort(want.begin(), want.end());
        std::sort(got.begin(), got.end());
        EXPECT(got == want, "%s: the findings differ", p.name);
        if (got != want) {
            for (const std::string& g : got) printf("    found: %s\n", g.c_str());
            for (const std::string& w : want) printf("    wanted: %s\n", w.c_str());
        }
        fs::remove_all(world.parent_path());
    }
}

// ------------------------------------------------------------------------------------- cache, --only and threads

static void testCache() {
    const fs::path world = copyWorld("cache");
    const std::string w = world.generic_string(), cache = (world.parent_path() / "worldv.cache").generic_string();
    Options opt;
    opt.world = w;
    opt.cache = cache;
    std::vector<Diagnostic> ds;
    Summary s;
    run(opt, ds, s);
    EXPECT(ds.empty() && s.checked == 30 && s.cached == 0 && s.cacheWritten && fs::exists(cache), "a full run writes the cache");
    opt.incremental = true;
    run(opt, ds, s);
    EXPECT(ds.empty() && s.checked == 0 && s.cached == 30 && s.ids >= 60 && s.refs >= 170, "an incremental run reuses it all (%zu checked)", s.checked);
    // a changed file is checked again, and the checks across files see its new references
    EXPECT(apply(world, {"districts/cl/blocks/b014/h03.ntw", "wall=f0.w2", "wall=f0.d1"}), "edit");
    run(opt, ds, s);
    EXPECT(s.checked == 1 && s.cached == 29 && ds.size() == 1 && ds[0].check == "ref.kind" && ds[0].line == 6, "one file changed: %zu checked, %zu findings", s.checked,
           ds.size());
    // a change elsewhere that breaks an unchanged file's reference: the unchanged file reports it, from the cache
    EXPECT(apply(world, {"districts/cl/blocks/b014/h03.ntw", "wall=f0.d1", "wall=f0.w2"}), "undo");
    EXPECT(apply(world, {"lib/catalog.ntw", "piece lib.kit.rug_oval", "piece lib.kit.rug_round"}), "rename a piece");
    run(opt, ds, s);
    EXPECT(s.checked == 2 && ds.size() == 1 && ds[0].file == w + "/lib/sets/living/cottage_01.ntw" && ds[0].check == "ref.missing",
           "a piece renamed: %zu checked, %zu findings", s.checked, ds.size());
    const std::vector<Diagnostic> incremental = ds;
    opt.incremental = false;
    run(opt, ds, s);
    EXPECT(s.checked == 30 && brief(ds, w) == brief(incremental, w), "a full run finds what the incremental one did");
    // --only with the cache: the rest of the world still counts
    opt.incremental = true;
    opt.only = "lib/sets";
    run(opt, ds, s);
    EXPECT(s.cached == 30 && s.reported == 2 && ds.size() == 1, "--only from the cache: %zu reported, %zu findings", s.reported, ds.size());
    opt.only.clear();
    // a damaged cache, or another validator's, is ignored
    writeText(cache, "worldv-cache 1 0000000000000000\nfile x\n");
    run(opt, ds, s);
    EXPECT(s.checked == 30 && s.cached == 0 && ds.size() == 1, "another validator's cache");
    std::string text = readText(cache);
    writeText(cache, text.substr(0, text.size() / 2) + "\nr x\n");
    run(opt, ds, s);
    EXPECT(s.checked == 30 && ds.size() == 1, "a damaged cache");
    writeText(cache, text.substr(0, text.size() / 3));
    run(opt, ds, s);
    EXPECT(ds.size() == 1, "a truncated cache: %zu findings", ds.size());
    fs::remove_all(world.parent_path());
}

static void testOnly() {
    const std::string w = std::string(kFixtures) + "/world";
    Summary s;
    // a planted error in a copy, to see what --only reports
    const fs::path world = copyWorld("only");
    EXPECT(apply(world, {"districts/cl/blocks/b014/h03.ntw", "room=f0.kitchen", "room=f0.cellar"}) &&
               apply(world, {"lib/plans/house/bungalow_b02.ntw", "door=f0.d1 kind=main", "door=f9.d1 kind=main"}),
           "edits");
    const std::string c = world.generic_string();
    std::vector<Diagnostic> ds = runWorld(c, &s);
    EXPECT(ds.size() == 2 && s.reported == 30, "both errors");
    ds = runWorld(c, &s, "districts/cl");
    EXPECT(ds.size() == 1 && ds[0].line == 5 && s.reported == 8, "--only a directory in the world: %zu findings, %zu files", ds.size(), s.reported);
    ds = runWorld(c, &s, c + "/lib/plans/house/bungalow_b02.ntw");
    EXPECT(ds.size() == 1 && ds[0].line == 10 && s.reported == 1, "--only a file by its path");
    ds = runWorld(c, &s, "cl.b014.h03");
    EXPECT(ds.size() == 1 && ds[0].line == 5 && s.reported == 1 && s.checked == 1, "--only a building id: %zu findings, %zu checked", ds.size(), s.checked);
    ds = runWorld(c, &s, "cl.b014.p03");
    EXPECT(ds.empty() && s.reported == 1 && s.onlyMatched, "--only a parcel id: its block file");
    ds = runWorld(c, &s, "lib.palette.bungalow.coral");
    EXPECT(ds.empty() && s.reported == 1, "--only a palette");
    ds = runWorld(c, &s, "no.such.thing");
    EXPECT(ds.empty() && !s.onlyMatched && s.reported == 0, "--only nothing");
    fs::create_directories(world / "districts/empty");
    ds = runWorld(c, &s, "districts/empty");
    EXPECT(ds.empty() && s.onlyMatched && s.reported == 0, "--only a directory with no files");
    ds = runWorld(c, &s, "./" + std::string(kFixtures) + "/../fixtures/../fixtures/world/../../../" + std::string(kFixtures) + "/world");
    EXPECT(!s.onlyMatched, "--only a directory outside the world");
    Options opt;
    opt.files.push_back(std::string(kFixtures) + "/bad");
    opt.only = "lib.layout.test.local";
    run(opt, ds, s);
    const size_t local = expectations(readText(std::string(kFixtures) + "/bad/layout_local.ntw")).size();
    EXPECT(s.reported == 1 && ds.size() == local, "--only an id in a file run: %zu reported, %zu findings", s.reported, ds.size());
    opt.only = fs::absolute(std::string(kFixtures) + "/bad/plan_ids.ntw").generic_string();
    run(opt, ds, s);
    const size_t plan = expectations(readText(std::string(kFixtures) + "/bad/plan_ids.ntw")).size();
    EXPECT(s.reported == 1 && ds.size() == plan, "--only an absolute path in a file run: %zu reported, %zu findings", s.reported, ds.size());
    fs::remove_all(world.parent_path());
    (void)w;
}

static void testThreads() {
    const fs::path world = copyWorld("threads");
    EXPECT(apply(world, {"districts/cl/roads.ntw", "to=cl.b014.lot1", "to=cl.b014.pa1"}) && apply(world, {"places/places.ntw", "anchor cl.ev.busk1", "anchor zz.ev.busk1"}) &&
               apply(world, {"lib/layouts/midrise/core_a.ntw", "stops=f0,f1,f2,f3", "stops=f0,f1,f2,f4"}),
           "edits");
    Summary s1, s4;
    const std::vector<Diagnostic> one = runWorld(world.generic_string(), &s1, std::string(), 1);
    const std::vector<Diagnostic> four = runWorld(world.generic_string(), &s4, std::string(), 4);
    EXPECT(one.size() >= 3 && brief(one, world.generic_string()) == brief(four, world.generic_string()) && s1.refs == s4.refs && s1.records == s4.records,
           "1 thread and 4 find the same");
    for (size_t i = 0; i < one.size() && i < four.size(); i++) EXPECT(one[i].message == four[i].message, "messages differ: %s", one[i].message.c_str());
    fs::remove_all(world.parent_path());
}

int main() {
    std::error_code ec;
    if (!fs::is_directory(kFixtures, ec)) {
        printf("worldv_test: run it from the repo root (no %s here)\n", kFixtures);
        return 1;
    }
    testPatterns();
    testTexts();
    testSamples();
    testBad();
    testWorld();
    testPlanPass();
    testRelativeStairs();
    testPlants();
    testCache();
    testOnly();
    testThreads();
    for (const CheckInfo& c : checkList()) EXPECT(gSeen.count(c.id), "no test made worldv report %s", c.id);
    if (gFails) {
        printf("worldv_test: %d FAILED\n", gFails);
        return 1;
    }
    printf("worldv_test: all passed\n");
    return 0;
}
