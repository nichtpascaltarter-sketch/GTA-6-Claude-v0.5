// The schema of the NTW files (docs/NTW_FORMAT.md): for every file kind, its record types, which of them open blocks
// and which records a block may hold, their fields with value types, the canonical field order and number precision.
// The parser takes block structure from it, the formatter its order and precision, and checkDocument() its types.
#pragma once
#include "ntw.h"

namespace World {
namespace Ntw {

enum ValueType : u8 {
    VT_NUM,      // a number: 3.25, -12, 1e-3
    VT_INT,      // an integer
    VT_ID,       // an identifier, [a-z0-9_.-]+
    VT_REF,      // a reference to another record's id (checked by the validator, not here)
    VT_WORD,     // one of a fixed set of words (`words` in the field)
    VT_TEXT,     // free text, usually quoted
    VT_POINT,    // x,y
    VT_POINT3,   // x,y,z
    VT_POINTS,   // x,y;x,y;...
    VT_LIST,     // a,b,c
    VT_HEX,      // hexadecimal digits (hashes)
    VT_COLOR,    // #rrggbb, lowercase
    VT_ANY,      // anything; the record's compiler reads it (compound values such as lanes=1/1 or size=11.60x9.40)
};

// A record's fields. The key "*" admits any other key, as VT_ANY: a building's config names the options of its plan.
struct FieldDef {
    const char* key;
    ValueType type;
    int decimals;          // numbers are written with this many places; -1 keeps them as written
    bool required;
    const char* words;     // VT_WORD: the allowed words, separated by '|'
};

struct RecordDef {
    const char* type;
    int id;                // 0 no id, 1 optional, 2 required
    bool block;            // opens a block closed by 'end'
    std::vector<FieldDef> fields;
    std::vector<std::string> children;   // the record types its block may hold
    bool topLevel;         // may appear outside any block
};

struct KindDef {
    const char* kind;
    int version;
    std::vector<FieldDef> header;
    std::vector<RecordDef> records;
};

const std::vector<KindDef>& schemaKinds();
const KindDef* findKind(const std::string& kind);
const RecordDef* findRecord(const KindDef& k, const std::string& type);
// For parse(): whether a record of `type` in a file of `kind` opens a block
bool schemaOpensBlock(const std::string& kind, const std::string& type);
// For format(): the schema's field order and precision
FormatRules schemaFormatRules();
// Checks a parsed document against the schema: the kind and version, record types and where they may stand, ids,
// required and unknown fields, value types. Appends errors with their lines.
void checkDocument(const Document& doc, std::vector<Error>& errors);

}  // namespace Ntw
}  // namespace World
