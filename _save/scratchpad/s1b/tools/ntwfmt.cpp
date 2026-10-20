// ntwfmt: the NTW formatter (docs/NTW_FORMAT.md). Rewrites .ntw files in their canonical form (field order and number
// precision from the schema, two-space indents, records wrapped at 120 columns), or checks that they are in it. It also
// checks every file against the schema.
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/ntwfmt.cpp -o build/ntwfmt
// Usage:                     build/ntwfmt [--check] <file or directory> ...   (a directory: every .ntw file below it)
// Exit code 1 when a file has errors or, with --check, is not canonical.
#include "tools/native_stubs.cpp"
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>

using namespace World;

namespace ntwfmt {

bool readFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

// 0 canonical (or rewritten), 1 errors, 2 not canonical (--check)
int formatFile(const std::string& path, bool check) {
    std::string text;
    if (!readFile(path, text)) {
        fprintf(stderr, "%s: error: cannot read it\n", path.c_str());
        return 1;
    }
    Ntw::Document doc;
    std::vector<Ntw::Error> errors;
    if (!Ntw::parse(text.data(), text.size(), Ntw::schemaOpensBlock, doc, errors)) {
        for (const Ntw::Error& e : errors) fprintf(stderr, "%s:%d: error: %s\n", path.c_str(), e.line, e.message.c_str());
        return 1;
    }
    Ntw::checkDocument(doc, errors);
    for (const Ntw::Error& e : errors) fprintf(stderr, "%s:%d: error: %s\n", path.c_str(), e.line, e.message.c_str());
    const Ntw::FormatRules rules = Ntw::schemaFormatRules();
    const std::string out = Ntw::format(doc, rules);
    // the canonical form must parse again and be its own canonical form
    Ntw::Document again;
    std::vector<Ntw::Error> errors2;
    if (!Ntw::parse(out.data(), out.size(), Ntw::schemaOpensBlock, again, errors2) || Ntw::format(again, rules) != out) {
        fprintf(stderr, "%s: internal error: the formatted text does not format to itself\n", path.c_str());
        return 1;
    }
    if (!errors.empty()) return 1;
    if (out == text) return 0;
    if (check) {
        fprintf(stderr, "%s: not in canonical form (run ntwfmt on it)\n", path.c_str());
        return 2;
    }
    if (!writeFile(path, out)) {
        fprintf(stderr, "%s: error: cannot write it\n", path.c_str());
        return 1;
    }
    printf("formatted %s\n", path.c_str());
    return 0;
}

}  // namespace ntwfmt

int main(int argc, char** argv) {
    bool check = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--check") {
            check = true;
            continue;
        }
        std::error_code ec;
        if (std::filesystem::is_directory(a, ec)) {
            std::vector<std::string> found;
            for (const auto& e : std::filesystem::recursive_directory_iterator(a, ec))
                if (e.is_regular_file() && e.path().extension() == ".ntw") found.push_back(e.path().generic_string());
            std::sort(found.begin(), found.end());
            files.insert(files.end(), found.begin(), found.end());
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        fprintf(stderr, "usage: ntwfmt [--check] <file or directory> ...\n");
        return 2;
    }
    int bad = 0, unformatted = 0;
    for (const std::string& f : files) {
        const int r = ntwfmt::formatFile(f, check);
        bad += r == 1;
        unformatted += r == 2;
    }
    printf("ntwfmt: %zu file(s), %d with errors%s\n", files.size(), bad, check ? (", " + std::to_string(unformatted) + " not canonical").c_str() : "");
    return bad || unformatted ? 1 : 0;
}
