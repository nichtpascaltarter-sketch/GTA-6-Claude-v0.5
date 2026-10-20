// NTW text format parser (ntw.h).
#include "ntw.h"
#include <cstdlib>
#include <cstring>

namespace World {
namespace Ntw {

namespace ntw_detail {

struct Token {
    std::string text;     // a bare word, or the key of a field
    std::string value;
    bool isField = false;
    bool quoted = false;
};

struct FlatRecord {
    int depth = 0;
    bool opens = false;
    Record rec;
};

inline bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Splits one line (without its newline) into tokens. A '#' that starts a token starts a comment, so values such as
// colour=#2f4f4f keep theirs. Returns false with a message on a malformed line.
bool tokenize(const char* s, size_t n, std::vector<Token>& out, std::string& err) {
    out.clear();
    size_t i = 0;
    while (i < n) {
        while (i < n && isSpace(s[i])) i++;
        if (i >= n || s[i] == '#') break;
        if (s[i] == '"') {
            err = "a quoted value needs a key: key=\"...\"";
            return false;
        }
        Token t;
        const size_t start = i;
        size_t eq = (size_t)-1;
        while (i < n && !isSpace(s[i])) {
            const char c = s[i];
            if (c == '"') {
                if (eq == (size_t)-1 || i != eq + 1) {
                    err = "a quote may only open a value, right after '='";
                    return false;
                }
                // quoted value: up to the closing quote, with \" and \\ as escapes
                i++;
                std::string v;
                bool closed = false;
                while (i < n) {
                    const char q = s[i];
                    if (q == '\\' && i + 1 < n && (s[i + 1] == '"' || s[i + 1] == '\\')) {
                        v += s[i + 1];
                        i += 2;
                        continue;
                    }
                    if (q == '"') {
                        closed = true;
                        i++;
                        break;
                    }
                    v += q;
                    i++;
                }
                if (!closed) {
                    err = "unterminated quoted value";
                    return false;
                }
                if (i < n && !isSpace(s[i])) {
                    err = "a quoted value must end its field";
                    return false;
                }
                t.quoted = true;
                t.value = v;
                break;
            }
            if (c == '=' && eq == (size_t)-1) eq = i;
            if ((unsigned char)c < 32) {
                err = "control character in the line";
                return false;
            }
            i++;
        }
        if (eq != (size_t)-1) {
            t.isField = true;
            t.text.assign(s + start, eq - start);
            if (!t.quoted) t.value.assign(s + eq + 1, i - eq - 1);
            if (!t.quoted && t.value.empty()) {
                err = "field '" + t.text + "' has no value";
                return false;
            }
        } else {
            t.text.assign(s + start, i - start);
        }
        out.push_back(t);
    }
    return true;
}

void addError(std::vector<Error>& errors, int line, const std::string& msg) {
    if (errors.size() < 50) errors.push_back({line, msg});
}

bool addField(Record& r, const Token& t, int line, std::vector<Error>& errors) {
    if (!isName(t.text)) {
        addError(errors, line, "bad key '" + t.text + "' (keys are lowercase: [a-z][a-z0-9_]*)");
        return false;
    }
    if (r.field(t.text.c_str())) {
        addError(errors, line, "duplicate key '" + t.text + "' in the " + r.type + " record of line " + std::to_string(r.line));
        return false;
    }
    Field f;
    f.key = t.text;
    f.value = t.value;
    f.quoted = t.quoted;
    f.line = line;
    r.fields.push_back(f);
    return true;
}

void buildTree(std::vector<FlatRecord>& flat, size_t& i, int depth, std::vector<Record>& into) {
    while (i < flat.size() && flat[i].depth == depth) {
        FlatRecord& f = flat[i++];
        Record r = std::move(f.rec);
        if (f.opens) buildTree(flat, i, depth + 1, r.children);
        into.push_back(std::move(r));
    }
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace ntw_detail

const Field* Record::field(const char* key) const {
    for (const Field& f : fields)
        if (f.key == key) return &f;
    return nullptr;
}

const char* Record::value(const char* key, const char* fallback) const {
    const Field* f = field(key);
    return f ? f->value.c_str() : fallback;
}

const Field* Document::headerField(const char* key) const {
    for (const Field& f : header)
        if (f.key == key) return &f;
    return nullptr;
}

bool isId(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
    return true;
}

bool isName(const std::string& s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool parse(const char* text, size_t size, BlockPredicate opensBlock, Document& out, std::vector<Error>& errors) {
    using namespace ntw_detail;
    out = Document();
    errors.clear();
    std::vector<FlatRecord> flat;
    std::vector<int> openLines;   // the line of each open block record
    std::vector<Token> tokens;
    std::string err;
    bool header = false;
    int lineNo = 0;
    size_t pos = 0;
    if (size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) pos = 3;   // UTF-8 BOM
    while (pos < size) {
        size_t end = pos;
        while (end < size && text[end] != '\n') end++;
        size_t len = end - pos;
        if (len > 0 && text[pos + len - 1] == '\r') len--;
        lineNo++;
        const char* line = text + pos;
        pos = end + 1;
        if (!tokenize(line, len, tokens, err)) {
            addError(errors, lineNo, err);
            continue;
        }
        if (tokens.empty()) continue;
        if (!header) {
            long long v = 0;
            if (tokens.size() < 3 || tokens[0].isField || tokens[0].text != "ntw" || tokens[1].isField || !toInt(tokens[1].text, &v) || v < 1 ||
                tokens[2].isField || !isName(tokens[2].text)) {
                addError(errors, lineNo, "a file starts with: ntw <version> <kind>");
                return false;
            }
            out.version = (int)v;
            out.kind = tokens[2].text;
            Record h;
            h.type = "ntw";
            h.line = lineNo;
            for (size_t k = 3; k < tokens.size(); k++) {
                if (!tokens[k].isField) {
                    addError(errors, lineNo, "only key=value may follow the file kind");
                    continue;
                }
                addField(h, tokens[k], lineNo, errors);
            }
            out.header = h.fields;
            header = true;
            continue;
        }
        if (!tokens[0].isField && tokens[0].text == "end") {
            if (tokens.size() != 1) addError(errors, lineNo, "'end' stands alone on its line");
            if (openLines.empty()) addError(errors, lineNo, "'end' without an open block");
            else openLines.pop_back();
            continue;
        }
        if (tokens[0].isField) {   // continues the record on the line before
            if (flat.empty()) {
                addError(errors, lineNo, "a line of key=value fields needs a record before it");
                continue;
            }
            for (const Token& t : tokens) {
                if (!t.isField) {
                    addError(errors, lineNo, "a continuation line holds only key=value fields");
                    break;
                }
                addField(flat.back().rec, t, lineNo, errors);
            }
            continue;
        }
        FlatRecord f;
        f.depth = (int)openLines.size();
        f.rec.type = tokens[0].text;
        f.rec.line = lineNo;
        if (!isName(f.rec.type)) addError(errors, lineNo, "bad record type '" + f.rec.type + "' (types are lowercase: [a-z][a-z0-9_]*)");
        size_t k = 1;
        if (tokens.size() > 1 && !tokens[1].isField) {
            f.rec.id = tokens[1].text;
            if (!isId(f.rec.id)) addError(errors, lineNo, "bad id '" + f.rec.id + "' (ids are [a-z0-9_.-]+)");
            k = 2;
        }
        for (; k < tokens.size(); k++) {
            if (!tokens[k].isField) {
                addError(errors, lineNo, "only key=value may follow the id ('" + tokens[k].text + "')");
                continue;
            }
            addField(f.rec, tokens[k], lineNo, errors);
        }
        f.opens = opensBlock && opensBlock(out.kind, f.rec.type);
        flat.push_back(std::move(f));
        if (flat.back().opens) openLines.push_back(lineNo);
    }
    if (!header) addError(errors, lineNo, "empty file: a file starts with: ntw <version> <kind>");
    for (int l : openLines) addError(errors, l, "the block opened here has no 'end'");
    size_t i = 0;
    buildTree(flat, i, 0, out.records);
    return errors.empty();
}

bool toNumber(const std::string& v, double* out) {
    using namespace ntw_detail;
    const char* s = v.c_str();
    size_t i = 0;
    if (s[i] == '-') i++;
    if (!isDigit(s[i])) return false;
    while (isDigit(s[i])) i++;
    if (s[i] == '.') {
        i++;
        if (!isDigit(s[i])) return false;
        while (isDigit(s[i])) i++;
    }
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        if (s[i] == '-' || s[i] == '+') i++;
        if (!isDigit(s[i])) return false;
        while (isDigit(s[i])) i++;
    }
    if (s[i] != 0) return false;
    if (out) *out = strtod(s, nullptr);
    return true;
}

bool toInt(const std::string& v, long long* out) {
    using namespace ntw_detail;
    const char* s = v.c_str();
    size_t i = 0;
    if (s[i] == '-') i++;
    if (!isDigit(s[i])) return false;
    while (isDigit(s[i])) i++;
    if (s[i] != 0 || i > 18) return false;
    if (out) *out = strtoll(s, nullptr, 10);
    return true;
}

namespace ntw_detail {
bool splitNumbers(const std::string& v, size_t a, size_t b, double* vals, int count) {
    int got = 0;
    size_t s = a;
    for (size_t i = a; i <= b; i++) {
        if (i == b || v[i] == ',') {
            if (got >= count) return false;
            if (!toNumber(v.substr(s, i - s), &vals[got])) return false;
            got++;
            s = i + 1;
        }
    }
    return got == count;
}
}  // namespace ntw_detail

bool toPoint(const std::string& v, vec2* out) {
    double p[2];
    if (!ntw_detail::splitNumbers(v, 0, v.size(), p, 2)) return false;
    if (out) *out = vec2((float)p[0], (float)p[1]);
    return true;
}

bool toPoint3(const std::string& v, vec3* out) {
    double p[3];
    if (!ntw_detail::splitNumbers(v, 0, v.size(), p, 3)) return false;
    if (out) *out = vec3((float)p[0], (float)p[1], (float)p[2]);
    return true;
}

bool toPoints(const std::string& v, std::vector<vec2>& out) {
    out.clear();
    if (v.empty()) return false;
    size_t s = 0;
    for (size_t i = 0; i <= v.size(); i++) {
        if (i == v.size() || v[i] == ';') {
            double p[2];
            if (!ntw_detail::splitNumbers(v, s, i, p, 2)) return false;
            out.push_back(vec2((float)p[0], (float)p[1]));
            s = i + 1;
        }
    }
    return true;
}

bool toList(const std::string& v, std::vector<std::string>& out) {
    out.clear();
    size_t s = 0;
    for (size_t i = 0; i <= v.size(); i++) {
        if (i == v.size() || v[i] == ',') {
            if (i == s) return false;
            out.push_back(v.substr(s, i - s));
            s = i + 1;
        }
    }
    return !out.empty();
}

}  // namespace Ntw
}  // namespace World
