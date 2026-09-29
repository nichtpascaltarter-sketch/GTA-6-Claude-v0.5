// Save games: versioned binary files in %LOCALAPPDATA%\NeonTide\saves\slotN.sav
#include "gameworld.h"

namespace Game {

namespace save_detail {

const u32 kMagic = 0x4E54534Eu;  // "NSTN"
const u32 kVersion = 3;

struct Writer {
    std::vector<u8> buf;
    template <typename T> void pod(const T& v) {
        const u8* p = (const u8*)&v;
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    void str(const std::string& s) {
        u32 n = (u32)s.size();
        pod(n);
        buf.insert(buf.end(), s.begin(), s.end());
    }
    template <typename T> void vec(const std::vector<T>& v) {
        u32 n = (u32)v.size();
        pod(n);
        for (const T& x : v) pod(x);
    }
};

struct Reader {
    const u8* p;
    const u8* end;
    bool ok = true;
    template <typename T> void pod(T& v) {
        if (p + sizeof(T) > end) {
            ok = false;
            return;
        }
        memcpy(&v, p, sizeof(T));
        p += sizeof(T);
    }
    void str(std::string& s) {
        u32 n = 0;
        pod(n);
        if (!ok || p + n > end || n > 1u << 20) {
            ok = false;
            return;
        }
        s.assign((const char*)p, n);
        p += n;
    }
    template <typename T> void vec(std::vector<T>& v) {
        u32 n = 0;
        pod(n);
        if (!ok || n > 1u << 20) {
            ok = false;
            return;
        }
        v.resize(n);
        for (auto& x : v) pod(x);
    }
};

std::string slotPath(int slot) {
    std::string dir = Platform::userDataDir() + "saves\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + StrFormat("slot%d.sav", slot);
}

u32 checksum(const u8* d, size_t n) {
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

}  // namespace save_detail

using namespace save_detail;

float GameWorld::completion() const {
    // story 60%, collectibles 15%, side activities 25% (tracked via storyFlags ranges)
    float story = 0.f;
    int storyDone = 0, sideDone = 0;
    for (size_t i = 0; i < storyFlags.size(); i++) {
        if (i < 64 && storyFlags[i]) storyDone++;
        if (i >= 64 && storyFlags[i]) sideDone++;
    }
    story = Min(1.f, storyDone / 20.f);
    float coll = shellCount > 0 ? (float)pinfo.collectiblesFound / shellCount : 0.f;
    float side = Min(1.f, sideDone / 24.f);
    return story * 60.f + coll * 15.f + side * 25.f;
}

bool GameWorld::saveGame(int slot, const std::string& title) {
    Ped* pl = playerPed();
    if (!pl) return false;
    Writer w;
    w.pod(kMagic);
    w.pod(kVersion);
    w.str(title);
    SYSTEMTIME st;
    GetLocalTime(&st);
    w.str(StrFormat("%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute));
    w.str(StrFormat("%.1f%% complete  |  $%lld  |  Day %d, %02d:%02d", completion(), pinfo.money, gameDay, (int)env->timeOfDay,
                    (int)(fmodf(env->timeOfDay, 1.f) * 60.f)));
    // world state
    w.pod(env->timeOfDay);
    w.pod(gameDay);
    w.pod(env->cloudCover);
    w.pod(env->rain);
    // player
    dvec3 pos = pl->pos;
    if (pl->vehicle >= 0) pos = vehicles[pl->vehicle].sim.body.pos + dvec3(0, 0, 0.5);
    w.pod(pos);
    w.pod(pl->yaw);
    w.pod(pl->health);
    w.pod(pl->armor);
    for (int i = 0; i < WPN_COUNT; i++) {
        u8 has = pl->hasWeapon[i] ? 1 : 0;
        w.pod(has);
        w.pod(pl->ammo[i]);
        w.pod(pl->clip[i]);
    }
    u8 wpn = (u8)pl->weapon;
    w.pod(wpn);
    w.pod(pinfo.money);
    w.pod(pinfo.distanceWalked);
    w.pod(pinfo.distanceDriven);
    w.pod(pinfo.kills);
    w.pod(pinfo.copsKilled);
    w.pod(pinfo.vehiclesStolen);
    w.pod(pinfo.headshots);
    w.pod(pinfo.shotsFired);
    w.pod(pinfo.shotsHit);
    w.pod(pinfo.deaths);
    w.pod(pinfo.arrests);
    w.pod(pinfo.maxWanted);
    w.pod(pinfo.playTime);
    w.pod(pinfo.collectiblesFound);
    w.vec(pinfo.collectibleFlags);
    w.str(storyTitle);
    w.vec(storyFlags);
    w.vec(ownedVehicleModels);
    w.pod(protagonistIndex);
    u32 cs = checksum(w.buf.data(), w.buf.size());
    w.pod(cs);
    std::string path = slotPath(slot);
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    size_t wr = fwrite(w.buf.data(), 1, w.buf.size(), f);
    fclose(f);
    if (wr != w.buf.size()) return false;
    MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    LOG("Saved game to %s (%zu bytes)", path.c_str(), w.buf.size());
    return true;
}

namespace save_detail {
bool readFile(const std::string& path, std::vector<u8>& data) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 12 || n > (64 << 20)) {
        fclose(f);
        return false;
    }
    data.resize((size_t)n);
    size_t rd = fread(data.data(), 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) return false;
    u32 stored;
    memcpy(&stored, data.data() + n - 4, 4);
    return checksum(data.data(), (size_t)n - 4) == stored;
}
}  // namespace save_detail

bool GameWorld::readSlotInfo(int slot, UI::SaveSlotInfo& info) const {
    info = UI::SaveSlotInfo();
    std::vector<u8> data;
    if (!readFile(slotPath(slot), data)) return false;
    Reader r{data.data(), data.data() + data.size() - 4};
    u32 magic = 0, ver = 0;
    r.pod(magic);
    r.pod(ver);
    if (magic != kMagic || ver != kVersion) return false;
    r.str(info.title);
    r.str(info.timestamp);
    r.str(info.detail);
    info.used = r.ok;
    return r.ok;
}

bool GameWorld::loadGame(int slot) {
    std::vector<u8> data;
    if (!readFile(slotPath(slot), data)) return false;
    Reader r{data.data(), data.data() + data.size() - 4};
    u32 magic = 0, ver = 0;
    r.pod(magic);
    r.pod(ver);
    if (magic != kMagic || ver != kVersion) return false;
    std::string title, stamp, detail;
    r.str(title);
    r.str(stamp);
    r.str(detail);
    float tod, cloud, rain;
    int day;
    r.pod(tod);
    r.pod(day);
    r.pod(cloud);
    r.pod(rain);
    dvec3 pos;
    float yaw, health, armor;
    r.pod(pos);
    r.pod(yaw);
    r.pod(health);
    r.pod(armor);
    bool has[WPN_COUNT];
    int ammo[WPN_COUNT], clip[WPN_COUNT];
    for (int i = 0; i < WPN_COUNT; i++) {
        u8 h = 0;
        r.pod(h);
        has[i] = h != 0;
        r.pod(ammo[i]);
        r.pod(clip[i]);
    }
    u8 wpn = 0;
    r.pod(wpn);
    PlayerInfo pi;
    r.pod(pi.money);
    r.pod(pi.distanceWalked);
    r.pod(pi.distanceDriven);
    r.pod(pi.kills);
    r.pod(pi.copsKilled);
    r.pod(pi.vehiclesStolen);
    r.pod(pi.headshots);
    r.pod(pi.shotsFired);
    r.pod(pi.shotsHit);
    r.pod(pi.deaths);
    r.pod(pi.arrests);
    r.pod(pi.maxWanted);
    r.pod(pi.playTime);
    r.pod(pi.collectiblesFound);
    r.vec(pi.collectibleFlags);
    std::string story;
    r.str(story);
    std::vector<int> flags, owned;
    r.vec(flags);
    r.vec(owned);
    int proto = 0;
    r.pod(proto);
    if (!r.ok) {
        LOG("Save slot %d is corrupt", slot);
        return false;
    }
    // apply
    resetWorldForLoad();
    env->timeOfDay = tod;
    gameDay = day;
    env->cloudCover = cloud;
    env->rain = rain;
    pinfo = pi;
    storyTitle = story;
    storyFlags = flags;
    ownedVehicleModels = owned;
    protagonistIndex = proto;
    spawnPlayer(pos, yaw);
    Ped* pl = playerPed();
    if (pl) {
        pl->health = health;
        pl->armor = armor;
        for (int i = 0; i < WPN_COUNT; i++) {
            pl->hasWeapon[i] = has[i];
            pl->ammo[i] = ammo[i];
            pl->clip[i] = clip[i];
        }
        pl->hasWeapon[WPN_FISTS] = true;
        pl->weapon = (WeaponType)Clamp((int)wpn, 0, (int)WPN_COUNT - 1);
    }
    // collectibles already found are not re-placed
    for (auto& pk : pickups)
        if (pk.used && pk.type == PICK_COLLECTIBLE && pk.collectibleId >= 0 && pk.collectibleId < (int)pinfo.collectibleFlags.size() &&
            pinfo.collectibleFlags[pk.collectibleId])
            pk.used = false;
    LOG("Loaded save slot %d (%s)", slot, title.c_str());
    return true;
}

}  // namespace Game
