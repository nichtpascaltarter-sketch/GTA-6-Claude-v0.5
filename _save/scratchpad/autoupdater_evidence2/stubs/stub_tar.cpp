// Test stand-in for Windows' tar.exe under Wine (which has none): "tar.exe -xf ARCHIVE -C DIR" for uncompressed
// POSIX tar archives (ustar, with pax headers skipped), refusing absolute paths and ".." like bsdtar does.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool makeDirs(const std::string& path) {
    for (size_t i = 0; i < path.size(); i++)
        if ((path[i] == '\\' || path[i] == '/') && i > 2) CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
    return CreateDirectoryA(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

int main(int argc, char** argv) {
    const char* archive = nullptr;
    const char* dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-xf") && i + 1 < argc) archive = argv[++i];
        else if (!strcmp(argv[i], "-C") && i + 1 < argc) dir = argv[++i];
        else {
            fprintf(stderr, "stub tar: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!archive) {
        fprintf(stderr, "stub tar: no archive\n");
        return 2;
    }
    FILE* f = fopen(archive, "rb");
    if (!f) {
        fprintf(stderr, "stub tar: cannot open %s\n", archive);
        return 2;
    }
    std::vector<unsigned char> data;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    if (data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
        fprintf(stderr, "stub tar: gzip is not supported by the test stand-in\n");
        return 2;
    }
    size_t pos = 0, files = 0;
    while (pos + 512 <= data.size()) {
        const unsigned char* h = &data[pos];
        bool zero = true;
        for (int k = 0; k < 512; k++) zero = zero && h[k] == 0;
        if (zero) break;
        if (memcmp(h + 257, "ustar", 5) != 0) {
            fprintf(stderr, "stub tar: not a ustar header at %zu\n", pos);
            return 2;
        }
        char sizeText[13] = {};
        memcpy(sizeText, h + 124, 12);
        size_t size = (size_t)strtoull(sizeText, nullptr, 8);
        char type = (char)h[156];
        std::string name((const char*)h, strnlen((const char*)h, 100)), prefix((const char*)h + 345, strnlen((const char*)h + 345, 155));
        if (!prefix.empty()) name = prefix + "/" + name;
        pos += 512;
        if (pos + size > data.size()) {
            fprintf(stderr, "stub tar: truncated archive\n");
            return 2;
        }
        if (type == 'g' || type == 'x') {   // pax headers
            pos += (size + 511) / 512 * 512;
            continue;
        }
        if (name.empty() || name[0] == '/' || name.find("..") != std::string::npos) {
            fprintf(stderr, "stub tar: refused path %s\n", name.c_str());
            return 2;
        }
        std::string path = std::string(dir) + "\\" + name;
        for (char& c : path)
            if (c == '/') c = '\\';
        if (type == '5') {
            while (!path.empty() && path.back() == '\\') path.pop_back();
            makeDirs(path);
        } else if (type == '0' || type == 0) {
            size_t cut = path.find_last_of('\\');
            if (cut != std::string::npos) makeDirs(path.substr(0, cut));
            FILE* o = fopen(path.c_str(), "wb");
            if (!o || fwrite(&data[pos], 1, size, o) != size) {
                fprintf(stderr, "stub tar: cannot write %s\n", path.c_str());
                return 2;
            }
            fclose(o);
            files++;
        }
        pos += (size + 511) / 512 * 512;
    }
    printf("stub tar: %zu files into %s\n", files, dir);
    return 0;
}
