// Updater logic tests, natively without Windows: versions, JSON, release and asset picks, checksums, PE headers,
// URLs and the pending-update record. The fixtures are GitHub's real answers for this repository (2026-10-19): the
// release list, the v0.02.6 release, and the 404 of /releases/latest (v0.02.6 is a pre-release, which that skips).
// Build and run from the repository root:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/updater/update_logic_test.cpp -o /tmp/update_logic_test
//   /tmp/update_logic_test [--fixtures DIR] [program.exe ...]   (each extra file must pass the PE check)
#include "../../src/platform/update_logic.cpp"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Updater;

static int checks = 0, failures = 0;
#define EXPECT(cond) do { ++checks; if (!(cond)) { \
    ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
} } while (0)

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int cmp(const char* a, const char* b) {
    Version x, y;
    if (!parseVersion(a, x) || !parseVersion(b, y)) return 99;
    return compareVersions(x, y);
}

static void testVersions() {
    Version v;
    EXPECT(parseVersion("v0.02.6", v) && v.parts.size() == 3 && v.parts[0] == 0 && v.parts[1] == 2 && v.parts[2] == 6 && v.pre.empty());
    EXPECT(parseVersion("0.02.7-dev", v) && v.pre == "dev");
    EXPECT(parseVersion(" V1.0-rc.1+build.5 ", v) && v.parts.size() == 2 && v.pre == "rc.1");
    const char* invalid[] = {"", "v", "abc", "latest", "1..2", "1.2.", ".1", "1.2-", "-1", "1.0 beta", "1.0-rc..1", "1.0-rc.", "1.0+",
                             "1.0-r c", "1.2.3.4.5.6.7.8.9", "99999999999", "1,2", "v 1.0", "1.0-rc_1"};
    for (const char* s : invalid) EXPECT(!parseVersion(s, v));
    EXPECT(cmp("0.02.10", "0.02.9") > 0);
    EXPECT(cmp("0.02.9", "0.02.10") < 0);
    EXPECT(cmp("0.3", "0.02.99") > 0);
    EXPECT(cmp("0.02.7", "0.02.7-dev") > 0);
    EXPECT(cmp("0.02.7-dev", "0.02.6") > 0);
    EXPECT(cmp("0.02.7-rc1", "0.02.7-dev") > 0);
    EXPECT(cmp("0.02.7-rc.10", "0.02.7-rc.2") > 0);
    EXPECT(cmp("0.02.7-alpha", "0.02.7-alpha.1") < 0);
    EXPECT(cmp("0.02.7-1", "0.02.7-alpha") < 0);   // numeric identifiers sort before alphanumeric ones
    EXPECT(cmp("0.02.7-rc.01", "0.02.7-rc.1") == 0);
    EXPECT(cmp("1.0", "1.0.0") == 0);
    EXPECT(cmp("0.02", "0.2") == 0);
    EXPECT(cmp("v1.2.3+build5", "1.2.3") == 0);
    EXPECT(cmp("4294967295", "4294967294") > 0);
    // never an equal or older version, nor one that does not parse
    EXPECT(isNewerVersion("v0.02.7", "0.02.7-dev"));
    EXPECT(isNewerVersion("9.9.9", "0.02.7-dev"));
    EXPECT(!isNewerVersion("v0.02.6", "0.02.7-dev"));
    EXPECT(!isNewerVersion("0.02.7-dev", "0.02.7-dev"));
    EXPECT(!isNewerVersion("v9.9.9", "9.9.9"));
    EXPECT(!isNewerVersion("nightly", "0.02.7-dev"));
    EXPECT(!isNewerVersion("0.02.8", "custom build"));
    EXPECT(displayVersion("v0.02.7") == "0.02.7");
    EXPECT(displayVersion(" V1.0 ") == "1.0");
    EXPECT(displayVersion("0.3") == "0.3");
    EXPECT(displayVersion("v").empty());
}

static void testJson() {
    Json j;
    EXPECT(parseJson("\"a\\\"b\\\\c\\/d\\n\\t\\b\\f\\r\"", j) && j.type == JSON_STRING && j.str == "a\"b\\c/d\n\t\b\f\r");
    EXPECT(parseJson("\"\\u00e9\\u20AC\"", j) && j.str == "\xC3\xA9\xE2\x82\xAC");                  // e acute, euro sign
    EXPECT(parseJson("\"\\ud83c\\udf34\"", j) && j.str == "\xF0\x9F\x8C\xB4");                     // a surrogate pair
    EXPECT(parseJson("\"\\ud83cx\"", j) && j.str == "\xEF\xBF\xBDx");                               // lone high surrogate
    EXPECT(parseJson("\"\\udf34\"", j) && j.str == "\xEF\xBF\xBD");                                 // lone low surrogate
    EXPECT(parseJson("\"\\ud83c\\u0041\"", j) && j.str == "\xEF\xBF\xBD" "A");                     // high + non-surrogate
    EXPECT(parseJson("\"Porto Sol \xF0\x9F\x8C\xB4\"", j) && j.str == "Porto Sol \xF0\x9F\x8C\xB4"); // raw UTF-8 passes
    EXPECT(parseJson(" { \"size\" : 12882944 , \"neg\":-5, \"f\":1.5e3, \"big\":123456789012345678901, \"t\":true, \"n\":null,"
                     " \"a\":[1,[2,{}],[]], \"o\":{\"k\":\"v\"} } ", j) && j.type == JSON_OBJECT);
    EXPECT(j.getInt("size", -1) == 12882944);
    EXPECT(j.getInt("neg", 0) == -5);
    EXPECT(j.getInt("f", -1) == -1);
    EXPECT(j.getInt("big", -1) == -1);
    EXPECT(j.getInt("missing", 7) == 7);
    EXPECT(j.getBool("t", false) && !j.getBool("n", false) && j.getBool("size", true));
    EXPECT(j.get("a") && j.get("a")->items.size() == 3 && j.get("a")->items[1].items[1].type == JSON_OBJECT);
    EXPECT(j.get("o") && j.get("o")->getString("k") == "v" && j.getString("o").empty());
    EXPECT(j.get("n") && j.get("n")->type == JSON_NULL && !j.get("nope"));
    const char* invalid[] = {"", "{} x", "\"open", "\"a\nb\"", "\"\\x\"", "\"\\u12\"", "\"\\u12G4\"", "[1,]", "{\"a\" 1}", "{\"a\":}",
                             "{a:1}", "01", "-", "1.", "1e", "+1", "tru", "nul", "[1 2]", "{\"a\":1,}", "\xEF\xBB\xBF{}"};
    for (const char* s : invalid) EXPECT(!parseJson(s, j));
    std::string err;
    EXPECT(!parseJson("[1, 2", j, &err) && !err.empty());
    EXPECT(parseJson(std::string(64, '[') + std::string(64, ']'), j));
    EXPECT(!parseJson(std::string(66, '[') + std::string(66, ']'), j));
    std::string odd = "a\"b\\c\n\x01" "\xC3\xA9";
    EXPECT(parseJson(jsonQuote(odd), j) && j.str == odd);
}

static Release release(const char* tag, std::vector<ReleaseAsset> assets, bool draft = false, bool pre = false) {
    Release r;
    r.tag = tag;
    r.assets = assets;
    r.draft = draft;
    r.prerelease = pre;
    return r;
}

static ReleaseAsset asset(const char* name, long long size = 1000, const char* url = "https://example.com/a") {
    ReleaseAsset a;
    a.name = name;
    a.size = size;
    a.url = url;
    return a;
}

static void testReleases() {
    // the highest version by tag, not the list order: drafts skipped, pre-releases count, releases without the exe skipped
    std::vector<Release> rs = {release("v0.02.9", {asset("NeonTide-0.02.9.exe")}),
                               release("v9.0.0", {asset("NeonTide-9.0.0.exe")}, true),
                               release("v0.03.0", {asset("NeonTide-0.03.0.exe")}, false, true),
                               release("v0.04.0", {asset("NeonTide-source.zip")}),
                               release("nightly", {asset("NeonTide-nightly.exe")})};
    int a = -2;
    std::string newest;
    EXPECT(pickUpdate(rs, "0.02.7-dev", &a, &newest) == 2 && a == 0 && newest == "v0.04.0");
    EXPECT(pickUpdate(rs, "0.03.0", &a) == -1 && a == -1);
    EXPECT(pickUpdate(rs, "0.03.1-dev", &a) == -1);
    EXPECT(pickUpdate(rs, "0.02.9", &a) == 2);
    EXPECT(pickUpdate(rs, "not a version", &a) == -1);
    std::vector<Release> none;
    EXPECT(pickUpdate(none, "0.02.7-dev", &a, &newest) == -1 && newest.empty());
    // the game exe: NeonTide*.exe in any case, preferring the one that names the version
    Release r = release("v0.03.0", {asset("NeonTide-source.zip"), asset("neontide.EXE"), asset("NeonTide-0.03.0.exe"),
                                    asset("NeonTide-0.03.0.exe.sha256")});
    EXPECT(pickExeAsset(r) == 2);
    r.assets[2].name = "NeonTide-0.03.01.exe";   // does not name 0.03.0: the first exe stays
    EXPECT(pickExeAsset(r) == 1);
    r = release("v0.03.0", {asset("NeonTide-0.03.0-debug.exe"), asset("NEONTIDE-0.03.0.EXE"), asset("NeonTide.exe")});
    EXPECT(pickExeAsset(r) == 1);                 // the exact name before another that names the version
    r.assets.erase(r.assets.begin() + 1);
    EXPECT(pickExeAsset(r) == 0);
    r = release("v0.03.0", {asset("Setup-NeonTide.exe"), asset("NeonTide-0.03.0.zip"), asset("NeonTide-0.03.0.exe", 0),
                            asset("NeonTide-0.03.0.exe", 10, "")});
    EXPECT(pickExeAsset(r) == -1);   // wrong prefix, not an exe, empty file, no URL
    // shapes: an error object reads as no releases, other JSON is refused
    std::vector<Release> out;
    EXPECT(parseReleases("{\"message\":\"Not Found\",\"status\":\"404\"}", out) && out.empty());
    EXPECT(!parseReleases("not json", out) && out.empty());
    EXPECT(!parseReleases("\"v1.0\"", out));
    EXPECT(parseReleases("[1, \"x\", {\"tag_name\": \"v1.0\", \"body\": null, \"assets\": [{\"name\": \"NeonTide.exe\", \"size\": 5,"
                         " \"browser_download_url\": \"https://x/y\"}, 3, {\"size\": 1}]}]", out) &&
           out.size() == 1 && out[0].body.empty() && out[0].assets.size() == 1 && out[0].assets[0].size == 5);
}

static void testFixtures(const std::string& dir) {
    const char* kHash = "e8b78ce907eefb21a3d1a53514fe4fb18598da375d90333944192e8bd9f935a4";
    const char* kUrl = "https://github.com/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/releases/download/v0.02.6/NeonTide-0.02.6.exe";
    for (const char* name : {"releases.json", "release_v0.02.6.json"}) {
        std::string text = readFile(dir + name);
        EXPECT(!text.empty());
        std::vector<Release> rs;
        EXPECT(parseReleases(text, rs) && rs.size() == 1);
        if (rs.size() != 1) continue;
        const Release& r = rs[0];
        EXPECT(r.tag == "v0.02.6" && r.prerelease && !r.draft && r.body == "Neon Tide 0.02.6");
        EXPECT(r.htmlUrl == "https://github.com/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/releases/tag/v0.02.6");
        int ex = pickExeAsset(r);
        EXPECT(ex == 0 && r.assets.size() == 1);
        if (ex != 0) continue;
        EXPECT(r.assets[0].name == "NeonTide-0.02.6.exe" && r.assets[0].size == 12882944 && r.assets[0].url == kUrl);
        EXPECT(checksumFromDigest(r.assets[0].digest) == kHash);
        EXPECT(pickChecksumAsset(r, r.assets[0].name) == -1 && checksumFromNotes(r.body, r.assets[0].name).empty());
        EXPECT(urlAllowed(r.assets[0].url, false) && urlAllowed(r.htmlUrl, false));
        int a = -1;
        std::string newest;
        EXPECT(pickUpdate(rs, "0.02.7-dev", &a, &newest) == -1 && newest == "v0.02.6");   // this source is newer
        EXPECT(pickUpdate(rs, "0.02.6-dev", &a) == 0 && a == 0);
        EXPECT(pickUpdate(rs, "0.02.5", &a) == 0);
        EXPECT(pickUpdate(rs, "0.02.6", &a) == -1);
    }
    std::vector<Release> rs;
    EXPECT(parseReleases(readFile(dir + "releases_latest_404.json"), rs) && rs.empty());
}

static void testChecksums() {
    const std::string h1(64, 'a'), h2 = std::string(63, 'b') + "c", up(64, 'D');
    EXPECT(checksumFromDigest("sha256:" + up) == std::string(64, 'd'));
    EXPECT(checksumFromDigest("SHA256:" + h1) == h1);
    EXPECT(checksumFromDigest("sha512:" + h1 + h1).empty());
    EXPECT(checksumFromDigest("md5:" + h1).empty());
    EXPECT(checksumFromDigest("sha256:abc").empty() && checksumFromDigest("").empty() && checksumFromDigest(h1).empty());
    Release r = release("v0.03.0", {asset("NeonTide-0.03.0.exe"), asset("SHA256SUMS.txt"), asset("neontide-0.03.0.EXE.sha256")});
    EXPECT(pickChecksumAsset(r, "NeonTide-0.03.0.exe") == 2);
    r.assets.pop_back();
    EXPECT(pickChecksumAsset(r, "NeonTide-0.03.0.exe") == 1);
    r.assets.pop_back();
    EXPECT(pickChecksumAsset(r, "NeonTide-0.03.0.exe") == -1);
    const std::string exe = "NeonTide-0.03.0.exe";
    EXPECT(checksumFromFile(h2 + "  NeonTide-source.zip\n" + h1 + "  NeonTide-0.03.0.exe\n", exe, "SHA256SUMS") == h1);
    EXPECT(checksumFromFile(h1 + " *NeonTide-0.03.0.exe\r\n", exe, "SHA256SUMS.txt") == h1);   // sha256sum binary mode
    EXPECT(checksumFromFile(up + "\n", exe, "NeonTide-0.03.0.exe.SHA256") == std::string(64, 'd'));   // the exe's own file
    EXPECT(checksumFromFile(h1 + "\n", exe, "SHA256SUMS").empty());                                  // a list must name it
    EXPECT(checksumFromFile(h2 + "  NeonTide-0.03.0.exe\n", exe, "NeonTide-0.03.0.exe.sha256") == h2);
    EXPECT(checksumFromFile(h1 + "  NeonTide-0.03.0.exe.zip\n", exe, "SHA256SUMS").empty());
    EXPECT(checksumFromFile(h1 + "  xNeonTide-0.03.0.exe\n", exe, "SHA256SUMS").empty());
    EXPECT(checksumFromNotes("Downloads:\n- NeonTide-0.03.0.exe (SHA-256 `" + h1 + "`)\n- source: " + h2 + "\n", exe) == h1);
    EXPECT(checksumFromNotes("Neon Tide 0.03.0\n\nSHA-256: " + h1, exe) == h1);
    EXPECT(checksumFromNotes("**SHA256**\n\n    " + h1 + "\n", exe) == h1);                       // the line below
    EXPECT(checksumFromNotes("NeonTide-0.03.0.exe\n" + h1 + "\n", exe) == h1);
    EXPECT(checksumFromNotes("sha256 of the exe: " + h1 + "\nsha256 of the zip: " + h2, exe).empty());   // ambiguous
    EXPECT(checksumFromNotes("sha256: " + h1 + "\nsame again, sha256: " + h1, exe) == h1);
    EXPECT(checksumFromNotes("Commit " + h1 + " fixes the radio", exe).empty());
    EXPECT(checksumFromNotes("SHA-512: " + h1 + h1, exe).empty());                                   // not 64 digits
    EXPECT(checksumFromNotes("SHA-256: " + std::string(63, 'a') + "g", exe).empty());                // not hex
}

// A minimal x64 PE header: MZ at 0, PE at 0x80, three sections inside a 0x5000-byte file
static std::vector<uint8_t> fakePe() {
    std::vector<uint8_t> h(0x400, 0);
    auto put16 = [&](size_t o, uint32_t v) { h[o] = (uint8_t)(v & 0xff); h[o + 1] = (uint8_t)(v >> 8); };
    auto put32 = [&](size_t o, uint32_t v) { put16(o, v & 0xffff); put16(o + 2, v >> 16); };
    h[0] = 'M';
    h[1] = 'Z';
    put32(0x3C, 0x80);
    h[0x80] = 'P';
    h[0x81] = 'E';
    put16(0x84, 0x8664);   // AMD64
    put16(0x86, 3);        // sections
    put16(0x94, 240);      // optional header size
    put16(0x96, 0x0022);   // executable image, large address aware
    size_t opt = 0x98;
    put16(opt, 0x20B);           // PE32+
    put32(opt + 16, 0x1000);     // entry point
    put32(opt + 56, 0x6000);     // size of image
    put32(opt + 60, 0x400);      // size of headers
    put16(opt + 68, 2);          // Windows GUI
    for (uint32_t s = 0; s < 3; s++) {
        size_t o = opt + 240 + s * 40;
        put32(o + 16, 0x1000);              // raw size
        put32(o + 20, 0x400 + s * 0x1000);  // raw position
    }
    return h;
}

static void testPe() {
    std::vector<uint8_t> h = fakePe();
    std::string why;
    EXPECT(peImageValid(h.data(), h.size(), 0x5000, &why));
    EXPECT(!peImageValid(h.data(), h.size(), 0x3000, &why) && !why.empty());   // truncated: a section past the end
    std::vector<uint8_t> b = h;
    b[0] = 'P';
    b[1] = 'K';
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // a zip
    b = h;
    b[0x84] = 0x4c;
    b[0x85] = 0x01;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // x86
    b = h;
    b[0x97] |= 0x20;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // a DLL
    b = h;
    b[0x98] = 0x0B;
    b[0x99] = 0x01;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // PE32
    b = h;
    b[0x3C] = 0xF0;
    b[0x3D] = 0xFF;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // PE header outside the file
    b = h;
    b[0x81] = 'X';
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // no PE signature
    b = h;
    b[0x98 + 68] = 1;
    EXPECT(!peImageValid(b.data(), b.size(), 0x5000));   // a driver, not a program
    EXPECT(!peImageValid(h.data(), 0x100, 0x5000));       // section table not in the bytes read
    const char html[] = "<!DOCTYPE html><html><body>Not Found</body></html>";
    EXPECT(!peImageValid((const uint8_t*)html, sizeof(html), sizeof(html)));
    EXPECT(!peImageValid(nullptr, 0, 0));
}

static void testUrls() {
    Url u;
    EXPECT(parseUrl("https://api.github.com/repos/o/r/releases?per_page=10", u) && u.https && u.host == "api.github.com" && u.port == 443 &&
           u.path == "/repos/o/r/releases?per_page=10");
    EXPECT(parseUrl("http://127.0.0.1:8765/latest.json", u) && !u.https && u.host == "127.0.0.1" && u.port == 8765 && u.path == "/latest.json");
    EXPECT(parseUrl("HTTPS://Example.COM", u) && u.host == "example.com" && u.path == "/");
    EXPECT(parseUrl("https://example.com?x=1#frag", u) && u.path == "/?x=1");
    EXPECT(parseUrl("https://[::1]:8443/x", u) && u.host == "::1" && u.port == 8443);
    const char* invalid[] = {"ftp://x/y", "file:///C:/x", "https://", "https:///x", "https://user@host/", "https://host:0/", "https://host:99999/",
                             "https://host:/", "https://ho st/", "javascript:alert(1)", "https://[::1/", "https://host:12a/", "//host/x", "mailto:x@y"};
    for (const char* s : invalid) EXPECT(!parseUrl(s, u));
    EXPECT(urlAllowed("https://github.com/x", false) && urlAllowed("https://github.com/x", true));
    EXPECT(urlAllowed("http://127.0.0.1:8765/a", true) && urlAllowed("http://localhost/a", true) && urlAllowed("http://[::1]:80/a", true));
    EXPECT(!urlAllowed("http://127.0.0.1:8765/a", false));
    EXPECT(!urlAllowed("http://example.com/a", true) && !urlAllowed("http://127.0.0.2/a", true) && !urlAllowed("http://localhost.evil.com/", true));
    EXPECT(!urlAllowed("file:///C:/Windows/notepad.exe", true));
}

static void testPending() {
    PendingInfo p;
    p.version = "9.9.9";
    p.size = 12882944;
    p.sha256 = std::string(64, 'a');
    PendingInfo q;
    EXPECT(pendingFromJson(pendingToJson(p), q) && q.version == "9.9.9" && q.size == 12882944 && q.sha256 == p.sha256);
    EXPECT(!pendingFromJson("{\"version\": \"9.9.9\", \"size\": 0, \"sha256\": \"" + p.sha256 + "\"}", q));
    EXPECT(!pendingFromJson("{\"version\": \"latest\", \"size\": 5, \"sha256\": \"" + p.sha256 + "\"}", q));
    EXPECT(!pendingFromJson("{\"version\": \"9.9.9\", \"size\": 5, \"sha256\": \"abc\"}", q));
    EXPECT(!pendingFromJson("{\"version\": \"9.9.9\", \"size\": 5", q));
}

int main(int argc, char** argv) {
    std::string dir = __FILE__;
    size_t slash = dir.find_last_of("/\\");
    dir = (slash == std::string::npos ? std::string() : dir.substr(0, slash + 1)) + "fixtures/";
    std::vector<std::string> programs;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--fixtures" && i + 1 < argc) {
            dir = std::string(argv[++i]) + "/";
        } else {
            programs.push_back(a);
        }
    }
    testVersions();
    testJson();
    testReleases();
    testFixtures(dir);
    testChecksums();
    testPe();
    testUrls();
    testPending();
    for (const std::string& path : programs) {
        std::string data = readFile(path), why;
        bool ok = peImageValid((const uint8_t*)data.data(), std::min<size_t>(data.size(), 65536), data.size(), &why);
        std::printf("%s: %s%s\n", path.c_str(), ok ? "valid x64 PE" : "not a valid program: ", why.c_str());
        EXPECT(ok);
    }
    if (failures) {
        std::fprintf(stderr, "Updater logic tests: %d of %d checks failed.\n", failures, checks);
        return 1;
    }
    std::printf("Updater logic tests passed (%d checks).\n", checks);
    return 0;
}
