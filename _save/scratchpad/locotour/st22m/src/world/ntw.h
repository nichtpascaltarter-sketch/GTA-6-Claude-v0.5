// NTW: the text format of the hand-built world (docs/HANDBUILT_WORLD.md section 4.2, docs/NTW_FORMAT.md).
//
//   ntw 1 <kind> [key=value ...]          the first record of every file
//   <type> [<id>] [key=value ...]         one record per line; '#' starts a comment outside quotes
//     key=value ...                       a line that starts with key=value continues the record on the line before
//   end                                   closes the block that the last open block record started
//
// Which record types open a block is the schema's business: the caller passes a predicate. Values stay text here; the
// helpers below read numbers, points, polylines and lists out of them. The parser is used by the world compiler, the
// validator and the dev loader, never by the game at runtime (the game reads the compiled world.bin).
#pragma once
#include "../core/math.h"
#include <string>
#include <vector>

namespace World {
namespace Ntw {

struct Field {
    std::string key;
    std::string value;    // quoted values are unquoted and unescaped
    bool quoted = false;
    int line = 0;
};

struct Record {
    std::string type;
    std::string id;                  // empty when the record has none
    std::vector<Field> fields;
    std::vector<Record> children;    // the records inside its block, when its type opens one
    int line = 0;

    const Field* field(const char* key) const;
    const char* value(const char* key, const char* fallback = nullptr) const;
    bool has(const char* key) const { return field(key) != nullptr; }
};

struct Document {
    int version = 0;
    std::string kind;                // from "ntw <version> <kind>"
    std::vector<Field> header;       // the key=value fields of the first record
    std::vector<Record> records;

    const Field* headerField(const char* key) const;
};

struct Error {
    int line = 0;
    std::string message;
};

// Does a record of `type`, in a file of `kind`, open a block?
typedef bool (*BlockPredicate)(const std::string& kind, const std::string& type);

// Parses `size` bytes of UTF-8 text. Returns true when there were no errors; up to 50 errors are listed with their lines.
bool parse(const char* text, size_t size, BlockPredicate opensBlock, Document& out, std::vector<Error>& errors);

// Identifiers: [a-z0-9_.-]+ (record ids and references); types and keys: [a-z][a-z0-9_]*
bool isId(const std::string& s);
bool isName(const std::string& s);

// Strict readers of field values; false when the text isn't exactly that
bool toNumber(const std::string& v, double* out);    // -12, 3.25, 1e-3 (no hex, no leading '+', no spaces)
bool toInt(const std::string& v, long long* out);
bool toPoint(const std::string& v, vec2* out);       // x,y
bool toPoint3(const std::string& v, vec3* out);      // x,y,z
bool toPoints(const std::string& v, std::vector<vec2>& out);   // x,y;x,y;...  (at least one point)
bool toList(const std::string& v, std::vector<std::string>& out);   // a,b,c  (no empty items)

}  // namespace Ntw
}  // namespace World
