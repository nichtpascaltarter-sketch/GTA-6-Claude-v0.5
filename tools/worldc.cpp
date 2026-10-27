// worldc: the world compiler (docs/HANDBUILT_WORLD.md, section 6.1). Reads every .ntw file under world/, checks it
// against the schema (src/world/ntwschema.h, docs/NTW_FORMAT.md) and compiles it into world.bin, which the build embeds in
// the exe as the RCDATA resource NT_WORLD (with the world.rc written next to it). What it compiles so far:
// - the baked terrain (stage 0);
// - the roads (stage 1), through the world model (src/world/worldmodel.h), with the world, district, signal and transit
//   files they name: the roads sections (src/world/roadsbin.h: the network, its ids, and the overrides the lane graph
//   reads so far), and the terrain graded under them ("terrain.grade");
// - the blocks' parcels, plazas and dressing, with the catalog's pieces (stage 1's part 2): the blocks section
//   (src/world/blocksbin.h), the parcels' grades in the terrain's grading, every height on the graded ground.
//   The other kinds' sections come with their consumers.
// Buildings, places and the rest of the library are errors until their compilers exist.
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/worldc.cpp -o build/worldc
// Run from the repo root:    build/worldc [--world=world] [--out=build/gen/world.bin]
//                            build/worldc --check=<file.ntw>     (one file against the schema, nothing written)
// Exit code 1 on any error, each reported as file:line.
#include "tools/native_stubs.cpp"
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include "src/world/gridcodec.cpp"
#include "src/world/lz.cpp"
#include "src/world/worldbin.cpp"
#include "src/core/math.cpp"
#include "src/world/roadnet.cpp"
#include "src/world/terrainops.cpp"
#include "src/world/roadgrade.cpp"
#include "src/world/worldmodel.cpp"
#include "src/world/worldmodel_roads.cpp"
#include "src/world/roadsbin.cpp"
#include "src/world/blocksbin.cpp"
#include "src/world/worldmodel_blocks.cpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace World;

namespace worldc {

std::string gWorld = "world";
std::string gOut = "build/gen/world.bin";
int gErrors = 0;

void error(const std::string& file, int line, const std::string& msg) {
    if (line > 0) fprintf(stderr, "%s:%d: error: %s\n", file.c_str(), line, msg.c_str());
    else fprintf(stderr, "%s: error: %s\n", file.c_str(), msg.c_str());
    gErrors++;
}

bool readFile(const std::string& path, std::vector<u8>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    u8 buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

// Writes a temp file beside `path`, then renames it over `path`: a build that reads the file meanwhile (another agent's
// build sharing build/gen) sees the old file or the new one, never a half-written one
bool writeFile(const std::string& path, const std::vector<u8>& bytes) {
    const std::string tmp = path + ".tmp" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (fclose(f) != 0 || !ok) {
        remove(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), path.c_str()) == 0) return true;
    remove(path.c_str());   // (Windows: rename doesn't replace; CI builds run alone there)
    if (rename(tmp.c_str(), path.c_str()) == 0) return true;
    remove(tmp.c_str());
    return false;
}

struct Source {
    std::string path;    // as given on disk
    std::string rel;     // relative to the world directory, with '/'
    std::vector<u8> text;
    Ntw::Document doc;
};

// Parses and checks one file; false (with its errors reported) when it can't be used
// The manifest's hash of a source: its text as LF lines without a byte-order mark, so a CRLF checkout (Windows without
// .gitattributes) compiles the same world.bin
u64 textHash(const std::vector<u8>& t) {
    size_t i = t.size() >= 3 && t[0] == 0xEF && t[1] == 0xBB && t[2] == 0xBF ? 3 : 0;
    u64 h = fnv1a64(nullptr, 0);
    for (; i < t.size(); i++)
        if (!(t[i] == '\r' && i + 1 < t.size() && t[i + 1] == '\n')) h = fnv1a64(&t[i], 1, h);
    return h;
}

bool load(Source& s) {
    if (!readFile(s.path, s.text)) {
        error(s.path, 0, "cannot read it");
        return false;
    }
    std::vector<Ntw::Error> errs;
    if (!Ntw::parse((const char*)s.text.data(), s.text.size(), Ntw::schemaOpensBlock, s.doc, errs)) {
        for (const Ntw::Error& e : errs) error(s.path, e.line, e.message);
        return false;
    }
    Ntw::checkDocument(s.doc, errs);
    for (const Ntw::Error& e : errs) error(s.path, e.line, e.message);
    return errs.empty();
}

double num(const Ntw::Record& r, const char* key, double fallback = 0.0) {   // (the schema checked the type)
    double v = fallback;
    if (const Ntw::Field* f = r.field(key)) Ntw::toNumber(f->value, &v);
    return v;
}

struct LayerSpec {
    const char* id;
    const char* section;
    GridType type;
    bool found = false;
    std::vector<u8> bytes;
};

// The baked terrain as compileTerrain read it: the heights and water the roads and parcels are graded into
// (worldmodel.h Hand::placeOnTerrain)
Hand::BakedTerrain gBaked;

// world/terrain/terrain.ntw and its grids -> the terrain record and the four grid sections
bool compileTerrain(const Source& src, WorldBinWriter& out) {
    const std::string& file = src.path;
    const std::string dir = src.path.substr(0, src.path.find_last_of("/\\") + 1);
    const int errorsBefore = gErrors;
    double res = 0, cell = 0, coastRes = 0, coastCell = 0, hUnit = 0, cUnit = 0;
    vec2 origin(0.f);
    int grids = 0;
    std::vector<std::pair<int, double>> levels;   // class, level
    LayerSpec layers[4] = {{"height", "terrain.height", GRID_I16, false, {}}, {"water", "terrain.water", GRID_U8, false, {}},
                           {"region", "terrain.region", GRID_U8, false, {}}, {"coast", "terrain.coast", GRID_I16, false, {}}};
    ByteWriter outlines, channels;
    u32 outlineCount = 0, channelCount = 0;
    for (const Ntw::Record& r : src.doc.records) {
        if (r.type == "grid") {
            grids++;
            res = num(r, "res");
            cell = num(r, "cell");
            Ntw::toPoint(r.value("origin", ""), &origin);
        } else if (r.type == "layer") {
            LayerSpec* L = nullptr;
            for (LayerSpec& l : layers)
                if (r.id == l.id) L = &l;
            if (!L) {
                error(file, r.line, "unknown layer '" + r.id + "' (height, water, region, coast)");
                continue;
            }
            if (L->found) {
                error(file, r.line, "layer '" + r.id + "' given twice");
                continue;
            }
            L->found = true;
            if (strcmp(r.value("type", ""), L->type == GRID_U8 ? "u8" : "i16") != 0)
                error(file, r.line, std::string("layer ") + L->id + " is type=" + (L->type == GRID_U8 ? "u8" : "i16"));
            if (L->type == GRID_I16) {
                if (!r.has("unit")) error(file, r.line, std::string("layer ") + L->id + " needs unit=");
                (r.id == "height" ? hUnit : cUnit) = num(r, "unit");
            }
            if (r.id == "coast") {
                if (!r.has("res") || !r.has("cell")) error(file, r.line, "layer coast needs res= and cell=");
                coastRes = num(r, "res");
                coastCell = num(r, "cell");
            }
            const std::string path = dir + r.value("file", "");
            if (!readFile(path, L->bytes)) {
                error(path, 0, "cannot read it (layer " + r.id + ")");
                continue;
            }
            std::vector<i16> values;
            GridInfo gi;
            if (!decodeGrid(L->bytes.data(), L->bytes.size(), values, &gi) || gi.type != L->type) {
                error(path, 0, "not a valid NTG grid of the layer's type");
                continue;
            }
            char hex[17];
            snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)gi.hash);
            if (strcmp(hex, r.value("hash", "")) != 0) error(file, r.line, std::string("layer ") + L->id + ": the grid's hash is " + hex + ", the file says " + r.value("hash", ""));
            if (r.id == "height") gBaked.height = values;
            else if (r.id == "water") gBaked.water = values;
            const int want = r.id == "coast" ? (int)coastRes : (int)res;
            if (gi.width != want || gi.height != want)
                error(path, 0, "the grid is " + std::to_string(gi.width) + " x " + std::to_string(gi.height) + ", not " + std::to_string(want));
        } else if (r.type == "water") {
            long long c = -1;
            if (r.id.size() < 2 || r.id[0] != 'c' || !Ntw::toInt(r.id.substr(1), &c) || c < 0 || c > 254) {
                error(file, r.line, "water classes are c0, c1, ... c254");
                continue;
            }
            const std::string lv = r.value("level", "");
            if (c == 0) {
                if (lv != "none") error(file, r.line, "class c0 is dry: level=none");
                continue;
            }
            double v = 0;
            if (!Ntw::toNumber(lv, &v)) {
                error(file, r.line, "level must be a number");
                continue;
            }
            levels.push_back(std::make_pair((int)c, v));
        } else if (r.type == "outline") {
            std::vector<vec2> pts;
            Ntw::toPoints(r.value("pts", ""), pts);
            if (pts.size() < 3) {
                error(file, r.line, "an outline needs at least 3 points");
                continue;
            }
            outlines.putString(r.id);
            outlines.putPoints(pts);
            outlineCount++;
        } else if (r.type == "channel") {
            std::vector<vec2> pts;
            Ntw::toPoints(r.value("pts", ""), pts);
            if (pts.size() < 2) {
                error(file, r.line, "a channel needs at least 2 points");
                continue;
            }
            channels.putString(r.id);
            channels.putF32((float)num(r, "width0"));
            channels.putF32((float)num(r, "width1"));
            channels.putF32((float)num(r, "depth"));
            channels.putPoints(pts);
            channelCount++;
        }
    }
    if (grids != 1) error(file, 0, "a terrain file has exactly one grid record");
    for (const LayerSpec& l : layers)
        if (!l.found) error(file, 0, std::string("no layer ") + l.id);
    std::sort(levels.begin(), levels.end());
    for (size_t i = 0; i < levels.size(); i++)
        if (levels[i].first != (int)i + 1) {
            error(file, 0, "water classes must be c1, c2, ... without gaps");
            break;
        }
    if (gErrors != errorsBefore) return false;
    ByteWriter t;
    t.putU32(1);
    t.putU32((u32)res);
    t.putF32((float)cell);
    t.putF32(origin.x);
    t.putF32(origin.y);
    t.putF32((float)hUnit);
    t.putU32((u32)levels.size());
    for (const auto& lv : levels) t.putF32((float)lv.second);
    t.putU32((u32)coastRes);
    t.putF32((float)coastCell);
    t.putF32((float)cUnit);
    t.putU32(outlineCount);
    t.bytes.insert(t.bytes.end(), outlines.bytes.begin(), outlines.bytes.end());
    t.putU32(channelCount);
    t.bytes.insert(t.bytes.end(), channels.bytes.begin(), channels.bytes.end());
    out.addCompressed("terrain", t.bytes);
    for (const LayerSpec& l : layers) out.add(l.section, WSK_GRID, l.bytes);
    gBaked.ok = true;
    gBaked.res = (int)res;
    gBaked.cell = (float)cell;
    gBaked.originX = origin.x;
    gBaked.originY = origin.y;
    gBaked.heightUnit = (float)hUnit;
    for (const auto& lv : levels) gBaked.levels.push_back((float)lv.second);
    printf("worldc: terrain %dx%d at %.0f m, %zu water levels, %u outlines, %u channels\n", (int)res, (int)res, cell, levels.size(), outlineCount, channelCount);
    return true;
}

// The kinds the world model reads in stage 1: the roads and the blocks, and the files they name
bool modelKind(const std::string& k) {
    return k == "world" || k == "district" || k == "roads" || k == "signal" || k == "transit" || k == "block" || k == "catalog";
}

// Those files through the world model: the roads compiled into the roads sections, and into `roads` and `decks` (per
// edge, its stretches on structures) for the grading; the blocks (their ground and dressing, parcels and lots) into
// `blocks`, which placeOnTerrain finishes. Moves the files' documents out.
bool compileModel(std::vector<Source>& files, const std::vector<size_t>& which, WorldBinWriter& out, RoadNetwork& roads,
                  std::vector<std::vector<vec2>>& decks, Hand::BlocksOut& blocks) {
    const int errorsBefore = gErrors;
    std::vector<Hand::File> parsed;
    for (size_t i : which) {
        Hand::File f;
        f.path = files[i].path;
        f.kind = files[i].doc.kind;
        f.doc = std::move(files[i].doc);
        f.valid = true;   // (load() reported every file with errors, and run() stops before this on any)
        parsed.push_back(std::move(f));
    }
    Hand::Model model;
    std::vector<Hand::Error> errs;
    Hand::loadParsed(std::move(parsed), model, errs);
    Hand::RoadIndex index;
    Hand::compileRoads(model, roads, index, errs);
    Hand::compileBlocks(model, blocks, errs);
    const auto report = [&](const std::vector<Hand::Error>& found) {
        for (const Hand::Error& e : found) {
            const bool inFile = e.src.file >= 0 && e.src.file < (int)model.files.size();
            error(inFile ? model.files[(size_t)e.src.file].path : gWorld, inFile ? e.src.line : 0, e.message + " [" + e.code + "]");
        }
    };
    report(errs);
    if (gErrors != errorsBefore) return false;
    if (!blocks.blocks.ground.empty() || !blocks.blocks.props.empty() || !blocks.blocks.lamps.empty())
        printf("worldc: blocks %zu, %zu ground polygons, %zu props, %zu lamps, %zu graded parcels\n", model.blocks.size(), blocks.blocks.ground.size(),
               blocks.blocks.props.size(), blocks.blocks.lamps.size(), blocks.pads.size());
    // what the AI's graphs read from the blocks: the parcels, and the lots with their drive graphs, on the roads as
    // the game will have them (each side's own widths)
    const auto sections = [&]() {
        std::vector<Hand::Error> found;
        const bool ok = Hand::compileBlockSections(model, index, roads, blocks, found);
        report(found);
        size_t bays = 0, links = 0;
        for (const HandLot& l : blocks.blocks.lots) {
            bays += l.bays.size();
            links += l.links.size();
        }
        size_t implied = 0;
        for (const HandSpot& x : blocks.blocks.spots) implied += (x.flags & HandSpot::IMPLIED) != 0;
        const HandBlocks& hb = blocks.blocks;
        if (ok && (!hb.parcels.empty() || !hb.lots.empty() || !hb.paths.empty() || !hb.plazas.empty() || !hb.steps.empty() || !hb.spots.empty()))
            printf("worldc: %zu parcels, %zu lots (%zu bays on %zu links); walks: %zu paths, %zu plazas, %zu flights of steps; %zu spots (%zu implied)\n",
                   hb.parcels.size(), hb.lots.size(), bays, links, hb.paths.size(), hb.plazas.size(), hb.steps.size(), hb.spots.size(), implied);
        return ok;
    };
    if (roads.nodes.empty() && roads.edges.empty()) return sections();
    decks.assign(roads.edges.size(), {});
    for (size_t i = 0; i < index.edgeOf.size(); i++)
        for (const Hand::Deck& d : model.edges[(size_t)index.edgeOf[i]].decks) decks[i].push_back(vec2(d.from, d.to));
    std::vector<std::string> nodeIds, edgeIds;
    for (int mi : index.nodeOf) nodeIds.push_back(model.nodes[(size_t)mi].id);
    for (int mi : index.edgeOf) edgeIds.push_back(model.edges[(size_t)mi].id);
    writeRoads(roads, &nodeIds, &edgeIds, out);
    RoadOverrides overrides;   // (what the lane graph reads beyond the network: roadoverrides.h)
    Hand::compileRoadOverrides(model, index, roads, overrides);
    writeRoadOverrides(overrides, out);
    applyRoadSides(overrides, roads);   // (each side's own widths, as the game will have them: the blocks go on them)
    int lights = 0;
    for (const RoadNode& n : roads.nodes) lights += n.control == 2;
    printf("worldc: roads %zu nodes, %zu edges, %d signalised; %zu districts, %zu signal plans, %zu transit lines\n", roads.nodes.size(), roads.edges.size(), lights,
           model.districts.size(), model.signals.size(), model.transit.size());
    printf("worldc: road overrides: %zu edges' widths and speeds, %zu node rules\n", overrides.edges.size(), overrides.nodes.size());
    return sections();
}

std::vector<Source> discover() {
    std::vector<Source> files;
    std::error_code ec;
    std::vector<std::string> paths;
    for (const auto& e : std::filesystem::recursive_directory_iterator(gWorld, ec))
        if (e.is_regular_file() && e.path().extension() == ".ntw") paths.push_back(e.path().generic_string());
    if (ec) error(gWorld, 0, "cannot list it: " + ec.message());
    std::sort(paths.begin(), paths.end());
    for (const std::string& p : paths) {
        Source s;
        s.path = p;
        s.rel = p.size() > gWorld.size() + 1 && p.compare(0, gWorld.size(), gWorld) == 0 ? p.substr(gWorld.size() + 1) : p;
        files.push_back(std::move(s));
    }
    return files;
}

int run() {
    std::vector<Source> files = discover();
    for (Source& s : files) load(s);
    if (gErrors) {
        fprintf(stderr, "worldc: %d error(s) in %zu file(s); %s not written\n", gErrors, files.size(), gOut.c_str());
        return 1;
    }
    WorldBinWriter out;
    int terrains = 0;
    std::string manifest = "world.bin manifest 1\n";
    std::vector<size_t> modelFiles;
    for (size_t i = 0; i < files.size(); i++) {
        const Source& s = files[i];
        char line[512];
        snprintf(line, sizeof(line), "%s %s %016llx\n", s.rel.c_str(), s.doc.kind.c_str(), (unsigned long long)textHash(s.text));
        manifest += line;
        if (s.doc.kind == "terrain") {
            if (s.rel != "terrain/terrain.ntw") error(s.path, 0, "the terrain file is world/terrain/terrain.ntw");
            else if (compileTerrain(s, out)) terrains++;
        } else if (modelKind(s.doc.kind)) {
            modelFiles.push_back(i);
        } else {
            error(s.path, 0, "no compiler for " + s.doc.kind + " files yet (buildings, places and the rest of the library come with their compilers)");
        }
    }
    if (terrains != 1 && !gErrors) error(gWorld, 0, "no world/terrain/terrain.ntw");
    if (!gErrors && !modelFiles.empty()) {
        RoadNetwork roads;
        std::vector<std::vector<vec2>> decks;
        Hand::BlocksOut blocks;
        if (compileModel(files, modelFiles, out, roads, decks, blocks)) {
            // the terrain graded under the parcels and the roads, and the blocks on it (worldmodel.h)
            std::string why, report;
            if (!Hand::placeOnTerrain(gBaked, roads, decks, blocks, out, &why, &report)) error(gWorld, 0, why);
            else if (!report.empty()) printf("%s\n", report.c_str());
        }
    }
    if (gErrors) {
        fprintf(stderr, "worldc: %d error(s); %s not written\n", gErrors, gOut.c_str());
        return 1;
    }
    out.addCompressed("manifest", std::vector<u8>(manifest.begin(), manifest.end()));
    const std::vector<u8> bin = out.finish();
    WorldBinView view;   // what was written must open again
    std::string why;
    if (!view.open(bin.data(), bin.size(), &why)) {
        fprintf(stderr, "worldc: internal error: %s\n", why.c_str());
        return 1;
    }
    if (!writeFile(gOut, bin)) {
        fprintf(stderr, "worldc: cannot write %s\n", gOut.c_str());
        return 1;
    }
    // the resource script next to it: the build compiles it (windres or rc) and links it into the exe
    const size_t slash = gOut.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? gOut : gOut.substr(slash + 1);
    const std::string rcPath = (slash == std::string::npos ? std::string() : gOut.substr(0, slash + 1)) + "world.rc";
    const std::string rc = "// written by tools/worldc.cpp: the hand-built world, embedded in the exe (src/world/handworld.cpp reads it)\n"
                           "NT_WORLD RCDATA \"" + base + "\"\n";
    if (!writeFile(rcPath, std::vector<u8>(rc.begin(), rc.end()))) {
        fprintf(stderr, "worldc: cannot write %s\n", rcPath.c_str());
        return 1;
    }
    printf("worldc: %zu file(s) -> %s (%zu bytes, %zu sections) and %s\n", files.size(), gOut.c_str(), bin.size(), view.sections.size(), rcPath.c_str());
    return 0;
}

int checkOne(const std::string& path) {
    Source s;
    s.path = path;
    s.rel = path;
    const bool ok = load(s);
    printf("worldc: %s %s\n", path.c_str(), ok ? "ok" : "has errors");
    return ok ? 0 : 1;
}

}  // namespace worldc

int main(int argc, char** argv) {
    std::string check;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a.rfind("--world=", 0) == 0) worldc::gWorld = a.substr(8);
        else if (a.rfind("--out=", 0) == 0) worldc::gOut = a.substr(6);
        else if (a.rfind("--check=", 0) == 0) check = a.substr(8);
        else {
            fprintf(stderr, "usage: worldc [--world=world] [--out=build/gen/world.bin] | worldc --check=<file.ntw>\n");
            return 2;
        }
    }
    if (!check.empty()) return worldc::checkOne(check);
    return worldc::run();
}
