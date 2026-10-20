// Updater logic that needs no operating system (tested natively by tests/updater/update_logic_test.cpp): version
// numbers, a small JSON reader, GitHub's release list, the choice of release, exe and checksum, the PE header check
// of a downloaded file and the URL rules. Downloads, hashing and the install are in updater.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Updater {

// ------------------------------------------------------------------------------------------------------------------
// Versions: "v0.02.10", "0.02.7-dev", "1.0-rc.1". A leading v is ignored and the dot parts compare as numbers
// (0.02.10 > 0.02.9; a missing part counts as 0). A pre-release suffix sorts before its release
// (0.02.7-dev < 0.02.7-rc1 < 0.02.7), suffixes compare like semver identifiers, and build metadata after + is ignored.
struct Version {
    std::vector<uint32_t> parts;
    std::string pre;   // after '-', empty for a release
};
bool parseVersion(const std::string& text, Version& out);
int compareVersions(const Version& a, const Version& b);   // < 0, 0 or > 0
// True only when both parse and `candidate` is strictly newer than `current`
bool isNewerVersion(const std::string& candidate, const std::string& current);
// A tag as players see it: "v0.02.7" -> "0.02.7"
std::string displayVersion(const std::string& tag);

// ------------------------------------------------------------------------------------------------------------------
// JSON (RFC 8259): objects, arrays, strings (escapes, \uXXXX with surrogate pairs, to UTF-8), numbers and literals.
// Strict: anything else, trailing text or nesting deeper than 64 fails.
enum JsonType : uint8_t { JSON_NULL = 0, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT };
struct Json {
    JsonType type = JSON_NULL;
    bool b = false;
    std::string str;                 // STRING; NUMBER keeps its source text (exact for large sizes)
    std::vector<Json> items;         // ARRAY elements, or OBJECT values (keys[i] names items[i])
    std::vector<std::string> keys;   // OBJECT member names
    const Json* get(const char* key) const;   // nullptr when missing or not an object
    std::string getString(const char* key) const;   // "" when missing or not a string
    long long getInt(const char* key, long long def) const;   // def unless an integer number
    bool getBool(const char* key, bool def) const;
};
bool parseJson(const std::string& text, Json& out, std::string* error = nullptr);
std::string jsonQuote(const std::string& s);   // a JSON string literal, quotes included

// ------------------------------------------------------------------------------------------------------------------
// Releases, as GitHub's REST API lists them (/repos/{owner}/{repo}/releases) or returns one of them
struct ReleaseAsset {
    std::string name, url;   // url: browser_download_url
    long long size = -1;
    std::string digest;      // "sha256:<hex>" when GitHub computed one
};
struct Release {
    std::string tag, htmlUrl, body;
    bool draft = false, prerelease = false;
    std::vector<ReleaseAsset> assets;
};
// Reads a release list (array) or a single release object. Entries without a tag are skipped, so an error object
// such as {"message": "Not Found"} reads as no releases. False when the text is not JSON or not of that shape.
bool parseReleases(const std::string& json, std::vector<Release>& out);
// The game exe of a release: a .exe whose name starts with NeonTide (any case), preferring NeonTide-<version>.exe,
// then one that names the release's version; -1 when there is none
int pickExeAsset(const Release& r);
// The update to offer: the highest version (by tag, not date) newer than `current` among the published releases
// (drafts skipped, pre-releases count) that has a game exe; -1 when there is none. `newestTag` receives the highest
// version tag listed at all (for the log).
int pickUpdate(const std::vector<Release>& releases, const std::string& current, int* exeAsset, std::string* newestTag = nullptr);

// ------------------------------------------------------------------------------------------------------------------
// Published SHA-256 checksums: lower-case 64-digit hex, "" when there is none. The updater takes the first it finds:
// GitHub's asset digest, then a checksum asset, then the release notes.
// GitHub's asset digest: "sha256:<hex>" (another algorithm gives "")
std::string checksumFromDigest(const std::string& digest);
// The checksum asset for `exeName`: "<exeName>.sha256" first, then SHA256SUMS*; -1 when there is none
int pickChecksumAsset(const Release& r, const std::string& exeName);
// The checksum of `exeName` in the checksum file `fileName`: a line that names the exe, or in the exe's own file
// (<exeName>.sha256) the first hex token
std::string checksumFromFile(const std::string& text, const std::string& exeName, const std::string& fileName);
// The checksum of `exeName` in release notes: a hex token on a line that names the file, else the only token on
// lines that mention SHA-256 (or on the line right after one)
std::string checksumFromNotes(const std::string& body, const std::string& exeName);

// ------------------------------------------------------------------------------------------------------------------
// A downloaded file is a 64-bit Windows executable: MZ, PE signature, AMD64, PE32+ optional header, not a DLL and
// every section inside the file. `head` holds the start of the file (64 KB covers the headers), `fileSize` its size.
bool peImageValid(const uint8_t* head, size_t headSize, uint64_t fileSize, std::string* why = nullptr);

// ------------------------------------------------------------------------------------------------------------------
// URLs: https only, except plain http to this machine (127.0.0.1, localhost, [::1]) when the test override allows it
struct Url {
    bool https = false;
    std::string host;
    int port = 0;
    std::string path;   // with the query, "/" at least
};
bool parseUrl(const std::string& text, Url& out);
bool urlAllowed(const std::string& text, bool allowLocalHttp);

// ------------------------------------------------------------------------------------------------------------------
// <exe>.pending.json: what the next start checks again before installing <exe>.pending
struct PendingInfo {
    std::string version, sha256;
    long long size = -1;
};
std::string pendingToJson(const PendingInfo& p);
bool pendingFromJson(const std::string& text, PendingInfo& out);

}  // namespace Updater
