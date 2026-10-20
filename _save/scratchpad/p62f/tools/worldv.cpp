// worldv: the world validator (docs/HANDBUILT_WORLD.md section 9.1). It reads every .ntw file under world/ and checks:
// - each file against its kind's schema, and that it is in canonical form;
// - ids against their patterns and for uniqueness, and each file's path against its ids;
// - that every reference resolves to a record of the right kind.
// What's wrong is listed as file:line diagnostics with a check id (--checks lists them). Errors fail the run (exit
// code 1); warnings are only listed.
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/worldv.cpp -o build/worldv
// Run from the repo root:    build/worldv [--world=world] [--only=<path or id>] [--incremental] [--jobs=<n>]
//                                         [--cache=build/worldv.cache | --no-cache]
//                            build/worldv <file or directory> ...    (each file on its own: no checks across files)
//                            build/worldv --checks                   (the check ids)
// --incremental reuses the cache's results for every file whose content is unchanged; each world run rewrites the
// cache. --only reports one file, the files under a directory, or the file that defines an id.
#include "tools/native_stubs.cpp"
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include "src/world/validate.cpp"
#include "src/world/validate_layout.cpp"
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>

using namespace World;

int main(int argc, char** argv) {
    Validate::Options opt;
    opt.cache = "build/worldv.cache";
    const unsigned cores = std::thread::hardware_concurrency();
    opt.jobs = cores == 0 ? 1 : cores > 4 ? 4 : (int)cores;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a.rfind("--world=", 0) == 0) opt.world = a.substr(8);
        else if (a.rfind("--only=", 0) == 0) opt.only = a.substr(7);
        else if (a == "--incremental") opt.incremental = true;
        else if (a.rfind("--cache=", 0) == 0) opt.cache = a.substr(8);
        else if (a == "--no-cache") opt.cache.clear();
        else if (a.rfind("--jobs=", 0) == 0 && atoi(a.c_str() + 7) > 0) opt.jobs = atoi(a.c_str() + 7);
        else if (a == "--checks") {
            for (const Validate::CheckInfo& c : Validate::checkList()) printf("%-16s %s\n", c.id, c.what);
            return 0;
        } else if (a.size() > 1 && a[0] == '-') {
            fprintf(stderr,
                    "usage: worldv [--world=world] [--only=<path or id>] [--incremental] [--jobs=<n>] [--cache=<file> | --no-cache]\n"
                    "       worldv <file or directory> ...\n"
                    "       worldv --checks\n");
            return 2;
        } else {
            opt.files.push_back(a);
        }
    }
    if (opt.incremental && opt.cache.empty()) {
        fprintf(stderr, "worldv: --incremental needs the cache (drop --no-cache)\n");
        return 2;
    }
    std::vector<Validate::Diagnostic> diags;
    Validate::Summary sum;
    Validate::run(opt, diags, sum);
    for (const Validate::Diagnostic& d : diags) fprintf(stderr, "%s\n", Validate::formatDiagnostic(d).c_str());
    if (!sum.onlyMatched) {
        fprintf(stderr, "worldv: --only=%s names no file, directory or id\n", opt.only.c_str());
        return 2;
    }
    if (!sum.cacheWritten) fprintf(stderr, "worldv: couldn't write the cache %s\n", opt.cache.c_str());
    printf("%s\n", Validate::formatSummary(sum).c_str());
    return sum.errors ? 1 : 0;
}
