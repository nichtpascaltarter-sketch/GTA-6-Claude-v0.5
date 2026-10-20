// Updater logic without the operating system: versions, JSON, releases, checksums, PE headers, URLs (update_logic.h).
#include "update_logic.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Updater {
namespace logic_detail {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) { return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isHex(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!isDigit(c)) return false;
    return true;
}
bool allHex(const std::string& s) {
    for (char c : s)
        if (!isHex(c)) return false;
    return !s.empty();
}
std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return o;
}
bool startsWith(const std::string& s, const char* prefix) {
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}
bool endsWith(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Semver pre-release order: dot-separated identifiers; numeric ones compare as numbers and sort before the others,
// which compare as ASCII; a shorter list that matches so far sorts first
int compareIdentifiers(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    for (;;) {
        bool endA = i >= a.size(), endB = j >= b.size();
        if (endA || endB) return endA && endB ? 0 : (endA ? -1 : 1);
        size_t ea = a.find('.', i), eb = b.find('.', j);
        if (ea == std::string::npos) ea = a.size();
        if (eb == std::string::npos) eb = b.size();
        std::string x = a.substr(i, ea - i), y = b.substr(j, eb - j);
        bool nx = allDigits(x), ny = allDigits(y);
        int c = 0;
        if (nx && ny) {
            x.erase(0, std::min(x.find_first_not_of('0'), x.size() - 1));
            y.erase(0, std::min(y.find_first_not_of('0'), y.size() - 1));
            c = x.size() != y.size() ? (x.size() < y.size() ? -1 : 1) : x.compare(y);
        } else if (nx != ny) {
            c = nx ? -1 : 1;
        } else {
            c = x.compare(y);
        }
        if (c != 0) return c < 0 ? -1 : 1;
        i = ea + 1;
        j = eb + 1;
    }
}

// Semver identifiers: [0-9A-Za-z-] separated by single dots
bool validIdentifiers(const std::string& s) {
    if (s.empty() || s.front() == '.' || s.back() == '.' || s.find("..") != std::string::npos) return false;
    for (char c : s)
        if (!isAlnum(c) && c != '.' && c != '-') return false;
    return true;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = e + 1;
    }
    return lines;
}

// 64-digit hex tokens of a line (lower case), each a whole word: "sha256:<hex>", "`<hex>`", "<hex>  name"
std::vector<std::string> hexTokens(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0, n = line.size();
    while (i < n) {
        if (!isAlnum(line[i])) {
            i++;
            continue;
        }
        size_t s = i;
        while (i < n && isAlnum(line[i])) i++;
        if (i - s == 64) {
            std::string t = line.substr(s, 64);
            if (allHex(t)) out.push_back(lower(t));
        }
    }
    return out;
}

// The (lower-case) line names the (lower-case) file on its own, not as part of a longer name
bool lineNames(const std::string& line, const std::string& name) {
    if (name.empty()) return false;
    for (size_t at = line.find(name); at != std::string::npos; at = line.find(name, at + 1)) {
        size_t after = at + name.size();
        char b = at > 0 ? line[at - 1] : ' ';
        char c = after < line.size() ? line[after] : ' ';
        bool beforeOk = !isAlnum(b) && b != '.' && b != '_' && b != '-';
        bool afterOk = !isAlnum(c) && c != '_' && c != '-' && !(c == '.' && after + 1 < line.size() && isAlnum(line[after + 1]));
        if (beforeOk && afterOk) return true;
    }
    return false;
}

bool mentionsSha256(const std::string& lowerLine) {
    return lowerLine.find("sha256") != std::string::npos || lowerLine.find("sha-256") != std::string::npos ||
           lowerLine.find("sha 256") != std::string::npos;
}

// Tokens of line k, or of the next non-empty line when k has none ("SHA-256:" with the hash below it)
std::vector<std::string> tokensAtOrBelow(const std::vector<std::string>& lines, size_t k) {
    std::vector<std::string> t = hexTokens(lines[k]);
    if (!t.empty()) return t;
    for (size_t m = k + 1; m < lines.size(); m++) {
        if (lines[m].find_first_not_of(" \t") == std::string::npos) continue;
        return hexTokens(lines[m]);
    }
    return t;
}

// The version as a whole word of a (lower-case) file name: "neontide-0.02.7.exe" names 0.02.7, "-0.02.70" does not
bool namesVersion(const std::string& name, const std::string& version) {
    if (version.empty()) return false;
    for (size_t at = name.find(version); at != std::string::npos; at = name.find(version, at + 1)) {
        size_t after = at + version.size();
        bool beforeOk = at == 0 || (!isDigit(name[at - 1]) && name[at - 1] != '.');
        bool afterOk = after >= name.size() || (!isDigit(name[after]) && !(name[after] == '.' && after + 1 < name.size() && isDigit(name[after + 1])));
        if (beforeOk && afterOk) return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------------------------
// JSON reader
struct JsonReader {
    const char* p = nullptr;
    const char* end = nullptr;
    std::string error;

    bool fail(const char* what) {
        if (error.empty()) error = what;
        return false;
    }
    void skipSpace() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool literal(const char* word) {
        size_t n = strlen(word);
        if ((size_t)(end - p) < n || memcmp(p, word, n) != 0) return fail("unknown literal");
        p += n;
        return true;
    }
    static void putUtf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s.push_back((char)cp);
        } else if (cp < 0x800) {
            s.push_back((char)(0xC0 | (cp >> 6)));
            s.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            s.push_back((char)(0xE0 | (cp >> 12)));
            s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            s.push_back((char)(0xF0 | (cp >> 18)));
            s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    bool hex4(uint32_t& v) {
        if (end - p < 4) return fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    // At the opening quote
    bool readString(std::string& s) {
        p++;
        s.clear();
        for (;;) {
            if (p >= end) return fail("unterminated string");
            unsigned char c = (unsigned char)*p++;
            if (c == '"') return true;
            if (c < 0x20) return fail("control character in a string");
            if (c != '\\') {
                s.push_back((char)c);
                continue;
            }
            if (p >= end) return fail("unterminated escape");
            char e = *p++;
            switch (e) {
            case '"': s.push_back('"'); break;
            case '\\': s.push_back('\\'); break;
            case '/': s.push_back('/'); break;
            case 'b': s.push_back('\b'); break;
            case 'f': s.push_back('\f'); break;
            case 'n': s.push_back('\n'); break;
            case 'r': s.push_back('\r'); break;
            case 't': s.push_back('\t'); break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // a high surrogate pairs with a following \u low surrogate; alone it becomes U+FFFD
                    uint32_t lo = 0;
                    const char* back = p;
                    if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        if (!hex4(lo)) return false;
                    }
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        p = back;
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                putUtf8(s, cp);
                break;
            }
            default: return fail("unknown escape");
            }
        }
    }
    bool number(Json& v) {
        const char* s = p;
        if (p < end && *p == '-') p++;
        if (p >= end || !isDigit(*p)) return fail("unexpected character");
        if (*p == '0') {
            p++;
        } else {
            while (p < end && isDigit(*p)) p++;
        }
        if (p < end && *p == '.') {
            p++;
            if (p >= end || !isDigit(*p)) return fail("bad number");
            while (p < end && isDigit(*p)) p++;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            p++;
            if (p < end && (*p == '+' || *p == '-')) p++;
            if (p >= end || !isDigit(*p)) return fail("bad number");
            while (p < end && isDigit(*p)) p++;
        }
        v.type = JSON_NUMBER;
        v.str.assign(s, (size_t)(p - s));
        return true;
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return fail("nested too deep");
        skipSpace();
        if (p >= end) return fail("unexpected end");
        char c = *p;
        if (c == '{' || c == '[') {
            bool object = c == '{';
            char closer = object ? '}' : ']';
            v.type = object ? JSON_OBJECT : JSON_ARRAY;
            p++;
            skipSpace();
            if (p < end && *p == closer) {
                p++;
                return true;
            }
            for (;;) {
                skipSpace();
                if (object) {
                    if (p >= end || *p != '"') return fail("expected a member name");
                    std::string key;
                    if (!readString(key)) return false;
                    skipSpace();
                    if (p >= end || *p != ':') return fail("expected ':'");
                    p++;
                    v.keys.push_back(key);
                }
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
                skipSpace();
                if (p < end && *p == ',') {
                    p++;
                    continue;
                }
                if (p < end && *p == closer) {
                    p++;
                    return true;
                }
                return fail(object ? "expected ',' or '}'" : "expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = JSON_STRING;
            return readString(v.str);
        }
        if (c == 't' || c == 'f') {
            v.type = JSON_BOOL;
            v.b = c == 't';
            return literal(v.b ? "true" : "false");
        }
        if (c == 'n') {
            v.type = JSON_NULL;
            return literal("null");
        }
        return number(v);
    }
};

}  // namespace logic_detail

using namespace logic_detail;

// ------------------------------------------------------------------------------------------------------------------
// Versions
bool parseVersion(const std::string& text, Version& out) {
    out = Version();
    size_t i = text.find_first_not_of(" \t\r\n"), n = text.find_last_not_of(" \t\r\n");
    if (i == std::string::npos) return false;
    n++;
    if (text[i] == 'v' || text[i] == 'V') i++;
    for (;;) {
        size_t start = i;
        uint64_t v = 0;
        while (i < n && isDigit(text[i])) {
            v = v * 10 + (uint64_t)(text[i] - '0');
            if (v > 0xffffffffull) return false;
            i++;
        }
        if (i == start || out.parts.size() >= 8) return false;
        out.parts.push_back((uint32_t)v);
        if (i < n && text[i] == '.') {
            i++;
            continue;
        }
        break;
    }
    if (i < n && text[i] == '-') {
        size_t start = ++i;
        while (i < n && text[i] != '+') i++;
        out.pre = text.substr(start, i - start);
        if (!validIdentifiers(out.pre)) return false;
    }
    if (i < n && text[i] == '+') {
        if (!validIdentifiers(text.substr(i + 1, n - i - 1))) return false;
        i = n;
    }
    return i == n;
}

int compareVersions(const Version& a, const Version& b) {
    size_t n = std::max(a.parts.size(), b.parts.size());
    for (size_t k = 0; k < n; k++) {
        uint32_t x = k < a.parts.size() ? a.parts[k] : 0, y = k < b.parts.size() ? b.parts[k] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.pre.empty() != b.pre.empty()) return a.pre.empty() ? 1 : -1;
    return compareIdentifiers(a.pre, b.pre);
}

bool isNewerVersion(const std::string& candidate, const std::string& current) {
    Version a, b;
    return parseVersion(candidate, a) && parseVersion(current, b) && compareVersions(a, b) > 0;
}

std::string displayVersion(const std::string& tag) {
    size_t i = tag.find_first_not_of(" \t\r\n"), e = tag.find_last_not_of(" \t\r\n");
    if (i == std::string::npos) return std::string();
    if (tag[i] == 'v' || tag[i] == 'V') i++;
    return i <= e ? tag.substr(i, e - i + 1) : std::string();
}

// ------------------------------------------------------------------------------------------------------------------
// JSON
const Json* Json::get(const char* key) const {
    if (type != JSON_OBJECT) return nullptr;
    for (size_t i = 0; i < keys.size() && i < items.size(); i++)
        if (keys[i] == key) return &items[i];
    return nullptr;
}

std::string Json::getString(const char* key) const {
    const Json* v = get(key);
    return v && v->type == JSON_STRING ? v->str : std::string();
}

long long Json::getInt(const char* key, long long def) const {
    const Json* v = get(key);
    if (!v || v->type != JSON_NUMBER) return def;
    const std::string& t = v->str;
    if (!allDigits(t[0] == '-' ? t.substr(1) : t) || t.size() > 18) return def;
    return strtoll(t.c_str(), nullptr, 10);
}

bool Json::getBool(const char* key, bool def) const {
    const Json* v = get(key);
    return v && v->type == JSON_BOOL ? v->b : def;
}

bool parseJson(const std::string& text, Json& out, std::string* error) {
    JsonReader r;
    r.p = text.data();
    r.end = text.data() + text.size();
    out = Json();
    bool ok = r.value(out, 0);
    if (ok) {
        r.skipSpace();
        if (r.p != r.end) ok = r.fail("text after the value");
    }
    if (!ok) {
        if (error) *error = r.error + " at byte " + std::to_string((size_t)(r.p - text.data()));
        out = Json();
    }
    return ok;
}

std::string jsonQuote(const std::string& s) {
    std::string o = "\"";
    for (char ch : s) {
        unsigned char c = (unsigned char)ch;
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back(ch);
        } else if (c < 0x20) {
            static const char kHex[] = "0123456789abcdef";
            o += "\\u00";
            o.push_back(kHex[c >> 4]);
            o.push_back(kHex[c & 15]);
        } else {
            o.push_back(ch);
        }
    }
    return o + "\"";
}

// ------------------------------------------------------------------------------------------------------------------
// Releases
bool parseReleases(const std::string& json, std::vector<Release>& out) {
    out.clear();
    Json root;
    if (!parseJson(json, root) || (root.type != JSON_ARRAY && root.type != JSON_OBJECT)) return false;
    auto read = [&](const Json& j) {
        if (j.type != JSON_OBJECT) return;
        Release r;
        r.tag = j.getString("tag_name");
        if (r.tag.empty()) return;
        r.htmlUrl = j.getString("html_url");
        r.body = j.getString("body");
        r.draft = j.getBool("draft", false);
        r.prerelease = j.getBool("prerelease", false);
        const Json* assets = j.get("assets");
        if (assets && assets->type == JSON_ARRAY) {
            for (const Json& a : assets->items) {
                if (a.type != JSON_OBJECT) continue;
                ReleaseAsset ra;
                ra.name = a.getString("name");
                ra.url = a.getString("browser_download_url");
                ra.size = a.getInt("size", -1);
                ra.digest = a.getString("digest");
                if (!ra.name.empty()) r.assets.push_back(ra);
            }
        }
        out.push_back(r);
    };
    if (root.type == JSON_ARRAY) {
        for (const Json& j : root.items) read(j);
    } else {
        read(root);
    }
    return true;
}

int pickExeAsset(const Release& r) {
    std::string version = lower(displayVersion(r.tag));
    int best = -1, bestScore = -1;
    for (size_t i = 0; i < r.assets.size(); i++) {
        const ReleaseAsset& a = r.assets[i];
        std::string n = lower(a.name);
        if (!startsWith(n, "neontide") || !endsWith(n, ".exe") || a.size <= 0 || a.url.empty()) continue;
        // NeonTide-<version>.exe first, then a name with the version (NeonTide-<version>-debug.exe), then any
        int score = n == "neontide-" + version + ".exe" ? 2 : (namesVersion(n, version) ? 1 : 0);
        if (score > bestScore) {
            best = (int)i;
            bestScore = score;
        }
    }
    return best;
}

int pickUpdate(const std::vector<Release>& releases, const std::string& current, int* exeAsset, std::string* newestTag) {
    int best = -1, bestAsset = -1, newest = -1;
    Version bestV, newestV, cur;
    bool haveCurrent = parseVersion(current, cur);
    for (size_t i = 0; i < releases.size(); i++) {
        const Release& r = releases[i];
        Version v;
        if (r.draft || !parseVersion(r.tag, v)) continue;
        if (newest < 0 || compareVersions(v, newestV) > 0) {
            newest = (int)i;
            newestV = v;
        }
        if (!haveCurrent || compareVersions(v, cur) <= 0) continue;
        int a = pickExeAsset(r);
        if (a < 0) continue;
        if (best < 0 || compareVersions(v, bestV) > 0) {
            best = (int)i;
            bestAsset = a;
            bestV = v;
        }
    }
    if (exeAsset) *exeAsset = bestAsset;
    if (newestTag) *newestTag = newest >= 0 ? releases[(size_t)newest].tag : std::string();
    return best;
}

// ------------------------------------------------------------------------------------------------------------------
// Checksums
std::string checksumFromDigest(const std::string& digest) {
    std::string d = lower(digest);
    if (!startsWith(d, "sha256:")) return std::string();
    std::string hex = d.substr(7);
    return hex.size() == 64 && allHex(hex) ? hex : std::string();
}

int pickChecksumAsset(const Release& r, const std::string& exeName) {
    std::string own = lower(exeName) + ".sha256";
    int sums = -1;
    for (size_t i = 0; i < r.assets.size(); i++) {
        std::string n = lower(r.assets[i].name);
        if (n == own) return (int)i;
        if (sums < 0 && startsWith(n, "sha256sums")) sums = (int)i;
    }
    return sums;
}

std::string checksumFromFile(const std::string& text, const std::string& exeName, const std::string& fileName) {
    std::string name = lower(exeName), first;
    bool dedicated = lower(fileName) == name + ".sha256";
    for (const std::string& line : splitLines(text)) {
        std::vector<std::string> t = hexTokens(line);
        if (t.empty()) continue;
        if (lineNames(lower(line), name)) return t[0];
        if (first.empty()) first = t[0];
    }
    return dedicated ? first : std::string();
}

std::string checksumFromNotes(const std::string& body, const std::string& exeName) {
    std::vector<std::string> lines = splitLines(body);
    std::string name = lower(exeName);
    for (size_t k = 0; k < lines.size(); k++) {
        if (!lineNames(lower(lines[k]), name)) continue;
        std::vector<std::string> t = tokensAtOrBelow(lines, k);
        if (!t.empty()) return t[0];
    }
    std::vector<std::string> found;
    for (size_t k = 0; k < lines.size(); k++) {
        if (!mentionsSha256(lower(lines[k]))) continue;
        for (const std::string& t : tokensAtOrBelow(lines, k))
            if (std::find(found.begin(), found.end(), t) == found.end()) found.push_back(t);
    }
    return found.size() == 1 ? found[0] : std::string();
}

// ------------------------------------------------------------------------------------------------------------------
// PE image
bool peImageValid(const uint8_t* head, size_t headSize, uint64_t fileSize, std::string* why) {
    auto bad = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    auto rd16 = [&](size_t o) { return (uint32_t)head[o] | ((uint32_t)head[o + 1] << 8); };
    auto rd32 = [&](size_t o) { return rd16(o) | (rd16(o + 2) << 16); };
    if (!head || headSize < 64 || fileSize < 64) return bad("too small for a program");
    if (head[0] != 'M' || head[1] != 'Z') return bad("no MZ header");
    size_t pe = rd32(0x3C);
    if (pe < 64 || pe + 24 > headSize || pe + 24 > fileSize) return bad("PE header outside the file");
    if (head[pe] != 'P' || head[pe + 1] != 'E' || head[pe + 2] != 0 || head[pe + 3] != 0) return bad("no PE signature");
    uint32_t machine = rd16(pe + 4), sections = rd16(pe + 6), optSize = rd16(pe + 20), flags = rd16(pe + 22);
    if (machine != 0x8664) return bad("not an x64 program");
    if (sections == 0 || sections > 96) return bad("bad section count");
    if ((flags & 0x0002) == 0 || (flags & 0x2000) != 0) return bad("not an executable image");
    size_t opt = pe + 24;
    if (optSize < 112 || opt + optSize > headSize) return bad("optional header outside the headers read");
    if (rd16(opt) != 0x20B) return bad("not a PE32+ image");
    if (rd32(opt + 16) == 0) return bad("no entry point");
    uint32_t subsystem = rd16(opt + 68);
    if (subsystem != 2 && subsystem != 3) return bad("not a Windows program");
    if (rd32(opt + 56) == 0 || rd32(opt + 60) > fileSize) return bad("bad image or header size");
    size_t table = opt + optSize;
    if (table + (size_t)sections * 40 > headSize) return bad("section table outside the headers read");
    for (uint32_t s = 0; s < sections; s++) {
        size_t o = table + (size_t)s * 40;
        uint64_t rawSize = rd32(o + 16), rawPos = rd32(o + 20);
        if (rawSize != 0 && rawPos + rawSize > fileSize) return bad("a section ends past the end of the file");
    }
    return true;
}

// ------------------------------------------------------------------------------------------------------------------
// URLs
bool parseUrl(const std::string& text, Url& out) {
    out = Url();
    for (char c : text)
        if ((unsigned char)c <= 0x20 || c == 0x7f) return false;
    std::string scheme = lower(text.substr(0, 8));
    size_t p = 0;
    if (startsWith(scheme, "https://")) {
        out.https = true;
        p = 8;
    } else if (startsWith(scheme, "http://")) {
        p = 7;
    } else {
        return false;
    }
    size_t stop = text.find_first_of("/?#", p);
    std::string auth = text.substr(p, stop == std::string::npos ? std::string::npos : stop - p), host, port;
    if (auth.empty() || auth.find('@') != std::string::npos) return false;
    if (auth[0] == '[') {
        size_t rb = auth.find(']');
        if (rb == std::string::npos) return false;
        host = auth.substr(1, rb - 1);
        if (rb + 1 < auth.size()) {
            if (auth[rb + 1] != ':') return false;
            port = auth.substr(rb + 2);
            if (port.empty()) return false;
        }
    } else {
        size_t colon = auth.find(':');
        host = auth.substr(0, colon);
        if (colon != std::string::npos) {
            port = auth.substr(colon + 1);
            if (port.empty()) return false;
        }
    }
    if (host.empty()) return false;
    out.port = out.https ? 443 : 80;
    if (!port.empty()) {
        if (port.size() > 5 || !allDigits(port)) return false;
        out.port = (int)strtol(port.c_str(), nullptr, 10);
        if (out.port < 1 || out.port > 65535) return false;
    }
    out.host = lower(host);
    out.path = stop == std::string::npos ? std::string("/") : text.substr(stop);
    size_t frag = out.path.find('#');
    if (frag != std::string::npos) out.path.erase(frag);
    if (out.path.empty() || out.path[0] != '/') out.path = "/" + out.path;
    return true;
}

bool urlAllowed(const std::string& text, bool allowLocalHttp) {
    Url u;
    if (!parseUrl(text, u)) return false;
    if (u.https) return true;
    return allowLocalHttp && (u.host == "127.0.0.1" || u.host == "localhost" || u.host == "::1");
}

// ------------------------------------------------------------------------------------------------------------------
// Pending update
std::string pendingToJson(const PendingInfo& p) {
    return "{\"version\": " + jsonQuote(p.version) + ", \"size\": " + std::to_string(p.size) + ", \"sha256\": " + jsonQuote(p.sha256) + "}\n";
}

bool pendingFromJson(const std::string& text, PendingInfo& out) {
    out = PendingInfo();
    Json j;
    if (!parseJson(text, j) || j.type != JSON_OBJECT) return false;
    out.version = j.getString("version");
    out.sha256 = lower(j.getString("sha256"));
    out.size = j.getInt("size", -1);
    Version v;
    return parseVersion(out.version, v) && out.size > 0 && out.sha256.size() == 64 && allHex(out.sha256);
}

}  // namespace Updater
