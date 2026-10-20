// The NTW schema (ntwschema.h). docs/NTW_FORMAT.md describes the same kinds in prose; keep the two in step.
#include "ntwschema.h"
#include <cstring>

namespace World {
namespace Ntw {

namespace ntwschema_detail {

FieldDef field(const char* key, ValueType t, int decimals, bool required, const char* words) {
    FieldDef f;
    f.key = key;
    f.type = t;
    f.decimals = decimals;
    f.required = required;
    f.words = words;
    return f;
}
// F: an optional field, R: a required one (the same arguments: no bool among them, so a word list can't turn into one)
FieldDef F(const char* key, ValueType t, int decimals = -1, const char* words = nullptr) { return field(key, t, decimals, false, words); }
FieldDef R(const char* key, ValueType t, int decimals = -1, const char* words = nullptr) { return field(key, t, decimals, true, words); }

RecordDef Rec(const char* type, int id, bool block, std::vector<FieldDef> fields, std::vector<std::string> children = {}, bool topLevel = true) {
    RecordDef r;
    r.type = type;
    r.id = id;
    r.block = block;
    r.fields = std::move(fields);
    r.children = std::move(children);
    r.topLevel = topLevel;
    return r;
}

std::vector<KindDef> build() {
    std::vector<KindDef> kinds;
    {
        // world/terrain/terrain.ntw: the baked terrain (tools/terrainbake.cpp writes it, tools/worldc.cpp compiles it)
        KindDef k;
        k.kind = "terrain";
        k.version = 1;
        k.records.push_back(Rec("source", 0, false, {F("tool", VT_ID), F("generator", VT_ID), F("seed", VT_ANY), F("commit", VT_ANY)}));
        k.records.push_back(Rec("grid", 0, false, {R("res", VT_INT), R("cell", VT_NUM, 2), R("origin", VT_POINT, 2)}));
        k.records.push_back(Rec("layer", 2, false,
                                {R("file", VT_ANY), R("type", VT_WORD, -1, "i16|u8"), F("res", VT_INT), F("cell", VT_NUM, 2), F("unit", VT_NUM), R("hash", VT_HEX),
                                 F("range", VT_POINT, 2)}));
        k.records.push_back(Rec("water", 2, false, {R("level", VT_ANY, 4)}));
        k.records.push_back(Rec("outline", 2, false, {R("pts", VT_POINTS, 2)}));
        k.records.push_back(Rec("channel", 2, false, {R("width0", VT_NUM, 2), R("width1", VT_NUM, 2), R("depth", VT_NUM, 2), R("pts", VT_POINTS, 2)}));
        kinds.push_back(k);
    }
    return kinds;
}

bool wordAllowed(const char* words, const std::string& v) {
    if (!words) return false;
    const char* p = words;
    while (*p) {
        const char* e = strchr(p, '|');
        const size_t n = e ? (size_t)(e - p) : strlen(p);
        if (v.size() == n && v.compare(0, n, p, n) == 0) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

bool valueOk(const FieldDef& f, const std::string& v) {
    switch (f.type) {
        case VT_NUM: return toNumber(v, nullptr);
        case VT_INT: return toInt(v, nullptr);
        case VT_ID:
        case VT_REF: return isId(v);
        case VT_WORD: return wordAllowed(f.words, v);
        case VT_POINT: return toPoint(v, nullptr);
        case VT_POINT3: return toPoint3(v, nullptr);
        case VT_POINTS: {
            std::vector<vec2> pts;
            return toPoints(v, pts);
        }
        case VT_LIST: {
            std::vector<std::string> items;
            return toList(v, items);
        }
        case VT_HEX:
            if (v.empty()) return false;
            for (char c : v)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            return true;
        case VT_TEXT:
        case VT_ANY: return true;
    }
    return true;
}

const char* typeName(ValueType t) {
    switch (t) {
        case VT_NUM: return "a number";
        case VT_INT: return "an integer";
        case VT_ID: return "an id";
        case VT_REF: return "an id reference";
        case VT_WORD: return "one of its words";
        case VT_TEXT: return "text";
        case VT_POINT: return "a point x,y";
        case VT_POINT3: return "a point x,y,z";
        case VT_POINTS: return "points x,y;x,y;...";
        case VT_LIST: return "a list a,b,c";
        case VT_HEX: return "lowercase hex digits";
        case VT_ANY: return "a value";
    }
    return "a value";
}

void checkFields(const std::vector<FieldDef>& defs, const std::vector<Field>& fields, const std::string& what, int line, std::vector<Error>& errors) {
    for (const Field& f : fields) {
        const FieldDef* d = nullptr;
        for (const FieldDef& fd : defs)
            if (f.key == fd.key) d = &fd;
        if (!d) {
            errors.push_back({f.line, "unknown field '" + f.key + "' in " + what});
            continue;
        }
        if (!valueOk(*d, f.value)) {
            std::string msg = what + ": " + f.key + "=" + f.value + " should be " + typeName(d->type);
            if (d->type == VT_WORD && d->words) msg += std::string(" (") + d->words + ")";
            errors.push_back({f.line, msg});
        }
    }
    for (const FieldDef& fd : defs) {
        if (!fd.required) continue;
        bool found = false;
        for (const Field& f : fields) found |= f.key == fd.key;
        if (!found) errors.push_back({line, what + " needs " + fd.key + "="});
    }
}

void checkRecords(const KindDef& k, const std::vector<Record>& recs, const RecordDef* parent, std::vector<Error>& errors) {
    for (const Record& r : recs) {
        const RecordDef* d = findRecord(k, r.type);
        if (!d) {
            errors.push_back({r.line, "unknown record '" + r.type + "' in a " + std::string(k.kind) + " file"});
            continue;
        }
        if (parent) {
            bool allowed = false;
            for (const std::string& c : parent->children) allowed |= c == r.type;
            if (!allowed) errors.push_back({r.line, "a " + r.type + " record can't stand inside a " + std::string(parent->type) + " block"});
        } else if (!d->topLevel) {
            errors.push_back({r.line, "a " + r.type + " record belongs inside a block"});
        }
        if (d->id == 0 && !r.id.empty()) errors.push_back({r.line, "a " + r.type + " record has no id"});
        if (d->id == 2 && r.id.empty()) errors.push_back({r.line, "a " + r.type + " record needs an id"});
        checkFields(d->fields, r.fields, "the " + r.type + " record", r.line, errors);
        if (!r.children.empty()) checkRecords(k, r.children, d, errors);
    }
}

const std::vector<std::string>* fieldOrder(const std::string& kind, const std::string& type) {
    // the keys in schema order, per kind and type, built once
    static std::vector<std::pair<std::string, std::vector<std::string>>> cache;
    static bool built = false;
    if (!built) {
        for (const KindDef& k : schemaKinds()) {
            std::vector<std::string> head;
            for (const FieldDef& f : k.header) head.push_back(f.key);
            cache.push_back(std::make_pair(std::string(k.kind) + "/ntw", head));
            for (const RecordDef& r : k.records) {
                std::vector<std::string> keys;
                for (const FieldDef& f : r.fields) keys.push_back(f.key);
                cache.push_back(std::make_pair(std::string(k.kind) + "/" + r.type, keys));
            }
        }
        built = true;
    }
    const std::string key = kind + "/" + type;
    for (const auto& c : cache)
        if (c.first == key) return &c.second;
    return nullptr;
}

int decimals(const std::string& kind, const std::string& type, const std::string& key) {
    const KindDef* k = findKind(kind);
    if (!k) return -1;
    const std::vector<FieldDef>* defs = nullptr;
    if (type == "ntw") defs = &k->header;
    else if (const RecordDef* r = findRecord(*k, type)) defs = &r->fields;
    if (!defs) return -1;
    for (const FieldDef& f : *defs)
        if (key == f.key) return f.decimals;
    return -1;
}

}  // namespace ntwschema_detail

const std::vector<KindDef>& schemaKinds() {
    static const std::vector<KindDef> kinds = ntwschema_detail::build();
    return kinds;
}

const KindDef* findKind(const std::string& kind) {
    for (const KindDef& k : schemaKinds())
        if (kind == k.kind) return &k;
    return nullptr;
}

const RecordDef* findRecord(const KindDef& k, const std::string& type) {
    for (const RecordDef& r : k.records)
        if (type == r.type) return &r;
    return nullptr;
}

bool schemaOpensBlock(const std::string& kind, const std::string& type) {
    const KindDef* k = findKind(kind);
    if (!k) return false;
    const RecordDef* r = findRecord(*k, type);
    return r && r->block;
}

FormatRules schemaFormatRules() {
    FormatRules rules;
    rules.fieldOrder = ntwschema_detail::fieldOrder;
    rules.decimals = ntwschema_detail::decimals;
    return rules;
}

void checkDocument(const Document& doc, std::vector<Error>& errors) {
    using namespace ntwschema_detail;
    const KindDef* k = findKind(doc.kind);
    if (!k) {
        errors.push_back({1, "unknown file kind '" + doc.kind + "'"});
        return;
    }
    if (doc.version != k->version) errors.push_back({1, "a " + doc.kind + " file is version " + std::to_string(k->version)});
    checkFields(k->header, doc.header, "the ntw line", 1, errors);
    checkRecords(*k, doc.records, nullptr, errors);
}

}  // namespace Ntw
}  // namespace World
