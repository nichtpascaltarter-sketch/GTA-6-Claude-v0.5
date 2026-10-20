// worldc: the world compiler (docs/HANDBUILT_WORLD.md, section 6.1). Reads the hand-built world's text under world/ and
// writes world.bin, which the build embeds in the exe as the RCDATA resource NT_WORLD (with the world.rc written next to
// it). Stage 0: the baked terrain (world/terrain/terrain.ntw and its grids).
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/worldc.cpp -o build/worldc
// Run from the repo root:    build/worldc [--world=world] [--out=build/gen/world.bin]
// Exit code 1 on any error, with the file and line.
#include "tools/native_stubs.cpp"
#include "src/world/ntw.cpp"
#include "src/world/gridcodec.cpp"
#include "src/world/worldbin.cpp"
#include <algorithm>
#include <chrono>
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

bool noBlocks(const std::string&, const std::string&) { return false; }

// A required number field
bool num(const std::string& file, const Ntw::Record& r, const char* key, double* out) {
    const Ntw::Field* f = r.field(key);
    if (!f) {
        error(file, r.line, std::string(r.type) + " needs " + key + "=");
        return false;
    }
    if (!Ntw::toNumber(f->value, out)) {
        error(file, f->line, std::string(key) + "=" + f->value + " is not a number");
        return false;
    }
    return true;
}

struct LayerSpec {
    const char* id;
    const char* section;
    GridType type;
    bool found = false;
    std::vector<u8> bytes;
};

// world/terrain/terrain.ntw and its grids -> the terrain record and the four grid sections
bool compileTerrain(WorldBinWriter& out) {
    const std::string dir = gWorld + "/terrain/";
    const std::string file = dir + "terrain.ntw";
    std::vector<u8> text;
    if (!readFile(file, text)) {
        error(file, 0, "cannot read it");
        return false;
    }
    Ntw::Document doc;
    std::vector<Ntw::Error> errs;
    if (!Ntw::parse((const char*)text.data(), text.size(), noBlocks, doc, errs)) {
        for (const Ntw::Error& e : errs) error(file, e.line, e.message);
        return false;
    }
    if (doc.kind != "terrain" || doc.version != 1) {
        error(file, 1, "expected: ntw 1 terrain");
        return false;
    }
    double res = 0, cell = 0, coastRes = 0, coastCell = 0, hUnit = 0, cUnit = 0;
    vec2 origin(0.f);
    bool haveGrid = false;
    std::vector<std::pair<int, double>> levels;   // class, level
    LayerSpec layers[4] = {{"height", "terrain.height", GRID_I16}, {"water", "terrain.water", GRID_U8}, {"region", "terrain.region", GRID_U8},
                           {"coast", "terrain.coast", GRID_I16}};
    ByteWriter outlines, channels;
    u32 outlineCount = 0, channelCount = 0;
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "source") continue;
        if (r.type == "grid") {
            haveGrid = num(file, r, "res", &res) && num(file, r, "cell", &cell);
            const Ntw::Field* o = r.field("origin");
            if (!o || !Ntw::toPoint(o->value, &origin)) {
                error(file, r.line, "grid needs origin=x,y");
                haveGrid = false;
            }
        } else if (r.type == "layer") {
            LayerSpec* L = nullptr;
            for (LayerSpec& l : layers)
                if (r.id == l.id) L = &l;
            if (!L) {
                error(file, r.line, "unknown layer '" + r.id + "'");
                continue;
            }
            if (L->found) {
                error(file, r.line, "layer '" + r.id + "' given twice");
                continue;
            }
            L->found = true;
            const char* fname = r.value("file");
            const char* type = r.value("type");
            const char* hash = r.value("hash");
            if (!fname || !type || !hash) {
                error(file, r.line, "a layer needs file=, type= and hash=");
                continue;
            }
            if (strcmp(type, L->type == GRID_U8 ? "u8" : "i16") != 0) error(file, r.line, std::string("layer ") + L->id + " must be type=" + (L->type == GRID_U8 ? "u8" : "i16"));
            if (L->type == GRID_I16 && !num(file, r, "unit", r.id == "height" ? &hUnit : &cUnit)) continue;
            if (r.id == "coast" && !(num(file, r, "res", &coastRes) && num(file, r, "cell", &coastCell))) continue;
            const std::string path = dir + fname;
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
            if (strcmp(hex, hash) != 0) error(file, r.line, std::string("layer ") + L->id + ": the grid's hash is " + hex + ", the file says " + hash);
            const int want = r.id == "coast" ? (int)coastRes : (int)res;
            if (gi.width != want || gi.height != want) error(path, 0, "the grid is " + std::to_string(gi.width) + " x " + std::to_string(gi.height) + ", not " + std::to_string(want));
        } else if (r.type == "water") {
            if (r.id.size() < 2 || r.id[0] != 'c') {
                error(file, r.line, "water classes are c0, c1, ...");
                continue;
            }
            long long c = -1;
            if (!Ntw::toInt(r.id.substr(1), &c) || c < 0 || c > 254) {
                error(file, r.line, "bad water class '" + r.id + "'");
                continue;
            }
            const char* lv = r.value("level");
            if (!lv) {
                error(file, r.line, "water needs level=");
                continue;
            }
            if (c == 0) {
                if (strcmp(lv, "none") != 0) error(file, r.line, "class c0 is dry: level=none");
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
            const char* p = r.value("pts");
            if (r.id.empty() || !p || !Ntw::toPoints(p, pts) || pts.size() < 3) {
                error(file, r.line, "an outline needs an id and pts= with at least 3 points");
                continue;
            }
            outlines.putString(r.id);
            outlines.putPoints(pts);
            outlineCount++;
        } else if (r.type == "channel") {
            std::vector<vec2> pts;
            double w0 = 0, w1 = 0, depth = 0;
            const char* p = r.value("pts");
            if (!num(file, r, "width0", &w0) || !num(file, r, "width1", &w1) || !num(file, r, "depth", &depth)) continue;
            if (!p || !Ntw::toPoints(p, pts) || pts.size() < 2) {
                error(file, r.line, "a channel needs pts= with at least 2 points");
                continue;
            }
            channels.putString(r.id);
            channels.putF32((float)w0);
            channels.putF32((float)w1);
            channels.putF32((float)depth);
            channels.putPoints(pts);
            channelCount++;
        } else {
            error(file, r.line, "unknown record '" + r.type + "' in a terrain file");
        }
    }
    if (!haveGrid) error(file, 0, "no valid grid record");
    for (const LayerSpec& l : layers)
        if (!l.found) error(file, 0, std::string("no layer ") + l.id);
    // water classes must run 1..n without gaps
    std::sort(levels.begin(), levels.end());
    for (size_t i = 0; i < levels.size(); i++)
        if (levels[i].first != (int)i + 1) {
            error(file, 0, "water classes must be c1, c2, ... without gaps");
            break;
        }
    if (gErrors) return false;
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
    out.add("terrain", WSK_RAW, t.bytes);
    for (const LayerSpec& l : layers) out.add(l.section, WSK_GRID, l.bytes);
    printf("worldc: terrain %dx%d at %.0f m, %zu water levels, %u outlines, %u channels\n", (int)res, (int)res, cell, levels.size(), outlineCount, channelCount);
    return true;
}

int run() {
    WorldBinWriter out;
    if (!compileTerrain(out)) {
        fprintf(stderr, "worldc: %d error(s); %s not written\n", gErrors, gOut.c_str());
        return 1;
    }
    const std::vector<u8> bin = out.finish();
    // check what was written opens again
    WorldBinView view;
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
    std::string rcPath = gOut;
    const size_t slash = rcPath.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? rcPath : rcPath.substr(slash + 1);
    rcPath = (slash == std::string::npos ? std::string() : rcPath.substr(0, slash + 1)) + "world.rc";
    const std::string rc = "// written by tools/worldc.cpp: the hand-built world, embedded in the exe (src/world/handworld.cpp reads it)\n"
                           "NT_WORLD RCDATA \"" + base + "\"\n";
    if (!writeFile(rcPath, std::vector<u8>(rc.begin(), rc.end()))) {
        fprintf(stderr, "worldc: cannot write %s\n", rcPath.c_str());
        return 1;
    }
    printf("worldc: wrote %s (%zu bytes, %zu sections) and %s\n", gOut.c_str(), bin.size(), view.sections.size(), rcPath.c_str());
    return 0;
}

}  // namespace worldc

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a.rfind("--world=", 0) == 0) worldc::gWorld = a.substr(8);
        else if (a.rfind("--out=", 0) == 0) worldc::gOut = a.substr(6);
        else {
            fprintf(stderr, "usage: worldc [--world=world] [--out=build/gen/world.bin]\n");
            return 2;
        }
    }
    return worldc::run();
}
