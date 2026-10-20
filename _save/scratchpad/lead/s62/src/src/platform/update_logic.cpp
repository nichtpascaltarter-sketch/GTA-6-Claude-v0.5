// Updater logic without the operating system (update_logic.h): JSON, GitHub's answers, pull requests, toolchains,
// commands, archive and exe checks, URLs and records.
#include "update_logic.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Updater {
namespace logic_detail {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
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
std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return o;
}
bool startsWith(const std::string& s, const char* prefix) {
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::string withoutTrailingSlashes(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}
std::string joinPath(const std::string& dir, const char* name) { return withoutTrailingSlashes(dir) + "\\" + name; }

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

// The name of an environment entry "NAME=value" ("=C:=C:\x": hidden entries start with '=')
std::string envName(const std::string& entry) {
    size_t eq = entry.find('=', 1);
    return eq == std::string::npos ? entry : entry.substr(0, eq);
}

// Latin-1 letters 0xC0..0xFF without their accents (asciiText)
const char* const kLatin1[64] = {"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
                                 "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
                                 "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
                                 "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y"};

// An ASCII stand-in for a code point above 0x7F, "" to drop it
const char* asciiFor(uint32_t cp) {
    if (cp >= 0xC0 && cp <= 0xFF) return kLatin1[cp - 0xC0];
    switch (cp) {
    case 0x00A0: case 0x2002: case 0x2003: case 0x2004: case 0x2005: case 0x2006: case 0x2007: case 0x2008:
    case 0x2009: case 0x200A: case 0x202F: case 0x205F: case 0x3000: return " ";
    case 0x00B7: case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2022:
    case 0x2212: return "-";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2026: return "...";
    case 0x2190: return "<-";
    case 0x2192: return "->";
    case 0x2194: return "<->";
    case 0x21D2: return "=>";
    case 0x2264: return "<=";
    case 0x2265: return ">=";
    case 0x2260: return "!=";
    case 0x00B1: return "+/-";
    default: return "";
    }
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
// Commits
bool isCommitSha(const std::string& s) {
    if (s.size() != 40) return false;
    for (char c : s)
        if (!isDigit(c) && !(c >= 'a' && c <= 'f')) return false;
    return true;
}

std::string shortCommit(const std::string& sha) { return isCommitSha(sha) ? sha.substr(0, 7) : std::string(); }

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
// GitHub
std::string compareUrl(const std::string& api, const std::string& repo, const std::string& commit) {
    return withoutTrailingSlashes(api) + "/repos/" + repo + "/compare/" + commit + "...main?per_page=100&page=100";
}

std::string commitsUrl(const std::string& api, const std::string& repo, int perPage) {
    return withoutTrailingSlashes(api) + "/repos/" + repo + "/commits?sha=main&per_page=" + std::to_string(perPage);
}

std::string archiveUrl(const std::string& web, const std::string& repo, const std::string& sha) {
    return withoutTrailingSlashes(web) + "/" + repo + "/archive/" + sha + ".tar.gz";
}

int commitsToList(int aheadBy) { return std::max(2, std::min(aheadBy + 1, 100)); }

bool parseCompare(int httpStatus, const std::string& json, Comparison& out) {
    out = Comparison();
    if (httpStatus == 404) {
        out.status = CMP_NOT_FOUND;
        return true;
    }
    Json j;
    if (httpStatus != 200 || !parseJson(json, j) || j.type != JSON_OBJECT) return false;
    std::string s = j.getString("status");
    long long a = j.getInt("ahead_by", -1), b = j.getInt("behind_by", -1);
    if (a < 0 || b < 0 || a > 1000000000 || b > 1000000000) return false;
    CompareStatus st = CMP_UNREADABLE;
    if (s == "ahead" && a > 0 && b == 0) st = CMP_AHEAD;
    else if (s == "identical" && a == 0 && b == 0) st = CMP_IDENTICAL;
    else if (s == "behind" && a == 0 && b > 0) st = CMP_BEHIND;
    else if (s == "diverged" && a > 0 && b > 0) st = CMP_DIVERGED;
    if (st == CMP_UNREADABLE) return false;   // an unknown status, or counts that do not fit it
    out.status = st;
    out.aheadBy = (int)a;
    out.behindBy = (int)b;
    return true;
}

const char* compareStatusName(CompareStatus s) {
    switch (s) {
    case CMP_AHEAD: return "ahead";
    case CMP_IDENTICAL: return "identical";
    case CMP_BEHIND: return "behind";
    case CMP_DIVERGED: return "diverged";
    case CMP_NOT_FOUND: return "not found";
    default: return "unreadable";
    }
}

bool parseCommitList(const std::string& json, std::vector<CommitInfo>& out) {
    out.clear();
    Json j;
    if (!parseJson(json, j) || j.type != JSON_ARRAY) return false;
    for (const Json& e : j.items) {
        if (e.type != JSON_OBJECT) continue;
        CommitInfo c;
        c.sha = lower(e.getString("sha"));
        if (!isCommitSha(c.sha)) continue;
        if (const Json* cm = e.get("commit")) {
            c.message = cm->getString("message");
            if (const Json* cc = cm->get("committer")) c.date = cc->getString("date");
        }
        const Json* ps = e.get("parents");
        if (ps && ps->type == JSON_ARRAY) c.parents = (int)ps->items.size();
        out.push_back(c);
    }
    return true;
}

bool pullFromMessage(const std::string& message, PullRequest& out) {
    out = PullRequest();
    std::vector<std::string> lines = splitLines(message);
    std::string first = lines.empty() ? std::string() : trim(lines[0]);
    static const char kMerge[] = "Merge pull request #";
    if (startsWith(first, kMerge)) {
        size_t s = sizeof(kMerge) - 1, i = s;
        while (i < first.size() && isDigit(first[i])) i++;
        if (i == s || i - s > 9) return false;
        std::string rest = first.substr(i), title;
        if (!rest.empty() && rest[0] != ':' && rest[0] != ' ') return false;   // "#53x" is no pull request number
        if (startsWith(rest, " from ")) {   // GitHub's default message: the title is the first line of the body
            for (size_t k = 1; k < lines.size() && title.empty(); k++) title = trim(lines[k]);
        } else {
            title = rest.empty() ? rest : rest.substr(1);
        }
        out.number = atoi(first.substr(s, i - s).c_str());
        out.title = asciiText(title);
        return out.number > 0;
    }
    // a squash merge: "title (#53)"
    size_t open = first.rfind(" (#");
    if (open == std::string::npos || first.size() < open + 5 || first.back() != ')') return false;
    std::string num = first.substr(open + 3, first.size() - open - 4);
    if (!allDigits(num) || num.size() > 9) return false;
    out.number = atoi(num.c_str());
    out.title = asciiText(first.substr(0, open));
    return out.number > 0;
}

bool newChangesSince(const std::vector<CommitInfo>& commits, const std::string& base, NewChanges& out) {
    out = NewChanges();
    if (commits.empty()) return false;
    out.head = commits[0].sha;
    out.headDate = commits[0].date;
    out.sinceBase = !base.empty();
    for (const CommitInfo& c : commits) {
        if (!base.empty() && c.sha == base) {
            out.complete = true;
            break;
        }
        out.commits++;
        PullRequest pr;
        if (!pullFromMessage(c.message, pr)) continue;
        bool seen = false;
        for (const PullRequest& q : out.pulls) seen = seen || q.number == pr.number;
        if (!seen) out.pulls.push_back(pr);
    }
    return true;
}

std::string changesSummary(const NewChanges& c, size_t maxChars) {
    std::string more = c.complete ? "" : "+";
    std::string line;
    if (!c.sinceBase) {
        if (c.pulls.empty()) return line;
        line = "Latest: ";
        for (size_t i = 0; i < c.pulls.size(); i++) {
            if (i) line += ", ";
            line += "#" + std::to_string(c.pulls[i].number);
            if (!c.pulls[i].title.empty()) line += " " + c.pulls[i].title;
        }
    } else if (c.pulls.empty()) {
        line = std::to_string(c.commits) + more + (c.commits == 1 && more.empty() ? " new commit" : " new commits");
    } else {
        size_t n = c.pulls.size();
        line = std::to_string(n) + more + (n == 1 && more.empty() ? " new update: " : " new updates: ");
        for (size_t i = 0; i < n; i++) {
            if (i) line += ", ";
            line += "#" + std::to_string(c.pulls[i].number);
            if (!c.pulls[i].title.empty()) line += " " + c.pulls[i].title;
        }
    }
    if (line.size() <= maxChars) return line;
    if (maxChars < 4) return std::string(maxChars, '.');
    // cut at a word boundary and mark the cut
    size_t cut = maxChars - 3, space = line.rfind(' ', cut);
    if (space != std::string::npos && space > cut / 2) cut = space;
    line.resize(cut);
    while (!line.empty() && (line.back() == ' ' || line.back() == ',' || line.back() == ':' || line.back() == '-')) line.pop_back();
    return line + "...";
}

std::string asciiText(const std::string& utf8) {
    std::string o;
    size_t i = 0, n = utf8.size();
    while (i < n) {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 0x80) {
            o.push_back(c < 0x20 || c == 0x7f ? ' ' : (char)c);
            i++;
            continue;
        }
        // one UTF-8 sequence; a broken one is dropped byte by byte
        size_t len = c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1));
        uint32_t cp = c & (0x7Fu >> len);
        bool ok = len > 1 && len <= 4 && i + len <= n;
        for (size_t k = 1; ok && k < len; k++) {
            unsigned char d = (unsigned char)utf8[i + k];
            ok = (d & 0xC0) == 0x80;
            cp = (cp << 6) | (d & 0x3Fu);
        }
        i += ok ? len : 1;
        if (ok) o += asciiFor(cp);
    }
    // one space between words, none at the ends
    std::string t;
    for (char ch : o)
        if (ch != ' ' || (!t.empty() && t.back() != ' ')) t.push_back(ch);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

// ------------------------------------------------------------------------------------------------------------------
// Toolchains
Toolchain pickToolchain(const ToolchainProbe& p) {
    Toolchain t;
    if (!p.vsInstall.empty() && p.vcvars) {
        t.kind = TC_MSVC;
        t.label = "MSVC (" + p.vsInstall + ")";
    } else if (p.clOnPath) {
        t.kind = TC_MSVC_PATH;
        t.label = "MSVC (cl.exe on PATH)";
    } else if (p.gxxOnPath && p.windresOnPath) {
        t.kind = TC_MINGW_PATH;
        t.label = "MinGW-w64 (g++ on PATH)";
    } else if (!p.mingwDirs.empty()) {
        t.kind = TC_MINGW_DIR;
        t.pathPrefix = p.mingwDirs[0];
        t.label = "MinGW-w64 (" + t.pathPrefix + ")";
    }
    return t;
}

std::vector<std::string> mingwCandidateDirs(const std::string& systemDrive, const std::string& userProfile) {
    std::vector<std::string> d;
    std::string drive = withoutTrailingSlashes(systemDrive), home = withoutTrailingSlashes(userProfile);
    if (!drive.empty()) d.push_back(drive + "\\w64devkit\\bin");
    if (!home.empty()) {
        d.push_back(home + "\\w64devkit\\bin");
        d.push_back(home + "\\scoop\\apps\\w64devkit\\current\\bin");
    }
    if (!drive.empty()) {
        d.push_back(drive + "\\msys64\\ucrt64\\bin");
        d.push_back(drive + "\\msys64\\mingw64\\bin");
        d.push_back(drive + "\\mingw64\\bin");
    }
    return d;
}

std::vector<std::string> splitPathList(const std::string& path) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= path.size()) {
        size_t e = path.find(';', i);
        if (e == std::string::npos) e = path.size();
        std::string s = path.substr(i, e - i);
        s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
        s = trim(s);
        if (!s.empty()) out.push_back(s);
        i = e + 1;
    }
    return out;
}

// ------------------------------------------------------------------------------------------------------------------
// Processes
std::string quoteArgument(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string o = "\"";
    for (size_t i = 0;; i++) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            i++;
            slashes++;
        }
        if (i == arg.size()) {   // before the closing quote every backslash doubles
            o.append(slashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            o.append(slashes * 2 + 1, '\\');
        } else {
            o.append(slashes, '\\');
        }
        o.push_back(arg[i]);
    }
    return o + "\"";
}

ProcessCommand buildCommand(const std::string& systemDir, const std::string& sourceDir) {
    ProcessCommand c;
    c.application = joinPath(systemDir, "cmd.exe");
    // (.\ so that cmd.exe finds it also where NoDefaultCurrentDirectoryInExePath is set)
    c.commandLine = quoteArgument(c.application) + " /d /c .\\build.bat";
    c.directory = sourceDir;
    return c;
}

ProcessCommand unpackCommand(const std::string& systemDir, const std::string& archive, const std::string& folder) {
    ProcessCommand c;
    c.application = joinPath(systemDir, "tar.exe");
    c.commandLine = quoteArgument(c.application) + " -xf " + quoteArgument(archive) + " -C " + quoteArgument(withoutTrailingSlashes(folder));
    c.directory = folder;
    return c;
}

std::vector<std::string> buildEnvironment(const std::vector<std::string>& env, const std::string& commit, const std::string& pathPrefix) {
    std::vector<std::string> out;
    bool havePath = false;
    for (const std::string& e : env) {
        std::string name = envName(e), key = upper(name);
        if (name.size() == e.size() || key == "VERSION" || key == "NT_COMMIT") continue;
        if (key == "PATH") {
            if (havePath) continue;
            havePath = true;
            std::string value = e.substr(name.size() + 1);
            if (!pathPrefix.empty()) value = value.empty() ? pathPrefix : pathPrefix + ";" + value;
            out.push_back(name + "=" + value);
            continue;
        }
        out.push_back(e);
    }
    if (!havePath && !pathPrefix.empty()) out.push_back("PATH=" + pathPrefix);
    out.push_back("NT_COMMIT=" + commit);
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return upper(envName(a)) < upper(envName(b)); });
    return out;
}

// ------------------------------------------------------------------------------------------------------------------
// Checks
bool archiveLooksValid(const uint8_t* head, size_t size) {
    if (!head) return false;
    if (size >= 2 && head[0] == 0x1f && head[1] == 0x8b) return true;     // gzip
    return size >= 512 && memcmp(head + 257, "ustar", 5) == 0;           // POSIX tar
}

bool archiveFolderMatches(const std::string& folder, const std::string& sha) {
    if (!isCommitSha(sha)) return false;
    std::string f = lower(folder);
    std::string full = "-" + sha, short7 = "-" + sha.substr(0, 7);
    return (f.size() > full.size() && endsWith(f, full)) || (f.size() > short7.size() && endsWith(f, short7));
}

std::string withCrlf(const std::string& text) {
    std::string o;
    o.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) o.push_back('\r');
        o.push_back(text[i]);
    }
    return o;
}

StreamSearch::StreamSearch(const std::string& n) : needle(n), hit(n.empty()) {}

void StreamSearch::add(const void* data, size_t n) {
    if (hit || n == 0) return;
    std::string buf = tail;
    buf.append((const char*)data, n);
    if (buf.find(needle) != std::string::npos) {
        hit = true;
        tail.clear();
        return;
    }
    size_t keep = std::min(buf.size(), needle.size() - 1);
    tail = buf.substr(buf.size() - keep);
}

std::vector<std::string> foldersToRemove(const std::vector<std::string>& names, const std::string& keep) {
    std::vector<std::string> out;
    for (const std::string& n : names)
        if (isCommitSha(n) && n != keep) out.push_back(n);
    return out;
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
    if (u.https) return u.host == "github.com" || endsWith(u.host, ".github.com");
    return allowLocalHttp && (u.host == "127.0.0.1" || u.host == "localhost" || u.host == "::1");
}

// ------------------------------------------------------------------------------------------------------------------
// Records
std::string pendingToJson(const PendingInfo& p) {
    std::string pulls;
    for (const PullRequest& pr : p.pulls)
        pulls += std::string(pulls.empty() ? "" : ", ") + "{\"number\": " + std::to_string(pr.number) + ", \"title\": " + jsonQuote(pr.title) + "}";
    return "{\"commit\": " + jsonQuote(p.commit) + ", \"from\": " + jsonQuote(p.from) + ", \"size\": " + std::to_string(p.size) +
           ", \"sha256\": " + jsonQuote(p.sha256) + ", \"toolchain\": " + jsonQuote(p.toolchain) + ", \"commits\": " +
           std::to_string(p.commits) + ", \"pulls\": [" + pulls + "]}\n";
}

bool pendingFromJson(const std::string& text, PendingInfo& out) {
    out = PendingInfo();
    Json j;
    if (!parseJson(text, j) || j.type != JSON_OBJECT) return false;
    out.commit = j.getString("commit");
    out.from = j.getString("from");
    out.sha256 = lower(j.getString("sha256"));
    out.size = j.getInt("size", -1);
    out.toolchain = j.getString("toolchain");
    out.commits = (int)std::max(0LL, std::min(j.getInt("commits", 0), 1000000LL));
    if (const Json* ps = j.get("pulls")) {
        for (const Json& e : ps->items) {
            PullRequest pr;
            pr.number = (int)std::max(0LL, std::min(e.getInt("number", 0), 1000000000LL));
            pr.title = asciiText(e.getString("title"));
            if (pr.number > 0) out.pulls.push_back(pr);
        }
    }
    bool ok = isCommitSha(out.commit) && (isCommitSha(out.from) || out.from == "unknown") && out.commit != out.from &&
              out.size > 0 && out.sha256.size() == 64 && allHex(out.sha256);
    if (!ok) out = PendingInfo();
    return ok;
}

std::string failedRecord(const std::string& toolchain, const std::string& why) {
    return "toolchain=" + asciiText(toolchain) + "\nwhy=" + asciiText(why) + "\n";
}

std::string failedToolchain(const std::string& record) {
    for (const std::string& line : splitLines(record))
        if (startsWith(line, "toolchain=") && line.size() > 10) return line.substr(10);
    return std::string();
}

}  // namespace Updater
