// worldc: the world compiler (docs/HANDBUILT_WORLD.md, section 6.1). Reads every .ntw file under world/, checks it
// against the schema (src/world/ntwschema.h, docs/NTW_FORMAT.md) and compiles it into world.bin, which the build embeds in
// the exe as the RCDATA resource NT_WORLD (with the world.rc written next to it). Stage 0 compiles the baked terrain;
// the other kinds get their compilers as the format grows.
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
#include <algorithm>
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

bool writeFile(const std::string& path, const std::vector<u8>& bytes) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return fclose(f) == 0 && ok;
}

struct Source {
    std::string path;    // as given on disk
    std::string rel;     // relative to the world directory, with '/'
    std::vector<u8> text;
    Ntw::Document doc;
};

// Parses and checks one file; false (with its errors reported) when it can't be used
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
    printf("worldc: terrain %dx%d at %.0f m, %zu water levels, %u outlines, %u channels\n", (int)res, (int)res, cell, levels.size(), outlineCount, channelCount);
    return true;
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
    for (const Source& s : files) {
        char line[512];
        snprintf(line, sizeof(line), "%s %s %016llx\n", s.rel.c_str(), s.doc.kind.c_str(), (unsigned long long)fnv1a64(s.text.data(), s.text.size()));
        manifest += line;
        if (s.doc.kind == "terrain") {
            if (s.rel != "terrain/terrain.ntw") error(s.path, 0, "the terrain file is world/terrain/terrain.ntw");
            else if (compileTerrain(s, out)) terrains++;
        } else {
            error(s.path, 0, "no compiler for " + s.doc.kind + " files yet");
        }
    }
    if (terrains != 1 && !gErrors) error(gWorld, 0, "no world/terrain/terrain.ntw");
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
