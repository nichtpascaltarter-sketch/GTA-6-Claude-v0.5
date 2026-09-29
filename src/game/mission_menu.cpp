// In-game menus used by shops, the phone and mission choices: one menu at a time, driven by the semantic menu
// controls (arrows / D-pad, Enter / A, Backspace / B). Drawn by drawMissionOverlay() when the app calls it; otherwise
// the list is shown in the HUD help box so it stays usable.
#include "missions.h"

namespace Game {
namespace mu {

enum MenuOwner : int { MO_NONE = 0, MO_SHOP_GUNS, MO_SHOP_CLOTHES, MO_SHOP_CARS, MO_WARDROBE, MO_GARAGE, MO_PHONE, MO_CHOICE,
                       MO_BUSINESS, MO_PROPERTY, MO_RESPRAY, MO_RACE, MO_REPLAY };

struct MenuItem {
    std::string label;
    std::string detail;       // description shown for the highlighted item
    long long price = -1;     // >= 0 shows "$price" on the right
    std::string right;        // custom right-aligned text (overrides price)
    bool enabled = true;
    bool checked = false;     // owned / equipped tick
    int id = 0;
};

struct GameMenu {
    bool open = false;
    int owner = MO_NONE;
    std::string title, subtitle;
    std::vector<MenuItem> items;
    int cursor = 0;
    int chosen = -1;          // id chosen this frame (-1 none)
    bool cancelled = false;   // back pressed this frame
    bool blocking = true;     // takes player control while open
    bool phoneStyle = false;  // drawn as the phone (bottom right)
    u32 accent = 0xff3aa0ffu;
    int openedFrame = 0;
    float anim = 0.f;
    bool restoreControl = false;
};

GameMenu gMenu;
int gMenuFrame = 0;
int gMenuInject = -1;   // test automation (--missiontest roam): item id to choose on the next menu update, -2 = back

void menuOpen(GameWorld& g, int owner, const std::string& title, const std::string& subtitle, const std::vector<MenuItem>& items,
              bool blocking = true, u32 accent = 0xff3aa0ffu, int cursor = 0) {
    gMenu.open = true;
    gMenu.owner = owner;
    gMenu.title = title;
    gMenu.subtitle = subtitle;
    gMenu.items = items;
    gMenu.cursor = Clamp(cursor, 0, Max((int)items.size() - 1, 0));
    gMenu.chosen = -1;
    gMenu.cancelled = false;
    gMenu.blocking = blocking;
    gMenu.phoneStyle = owner == MO_PHONE;
    gMenu.accent = accent;
    gMenu.openedFrame = gMenuFrame;
    gMenu.anim = 0.f;
    if (blocking && g.playerControl) {
        g.playerControl = false;
        gMenu.restoreControl = true;
        int pv = g.playerVehicle();
        if (pv >= 0 && g.peds[g.player].seat == 0) {
            g.vehicles[pv].ctl = Vehicles::VehicleControls();
            g.vehicles[pv].ctl.brake = 1.f;
        }
    } else {
        gMenu.restoreControl = false;
    }
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_UI_SELECT, 0.5f);
#endif
}

void menuClose(GameWorld& g) {
    if (!gMenu.open) return;
    gMenu.open = false;
    gMenu.owner = MO_NONE;
    if (gMenu.restoreControl) g.playerControl = true;
    gMenu.restoreControl = false;
    g.hudHelpTimer = 0.f;
}

// Replace the list while keeping the cursor (after a purchase the prices/ticks change).
void menuRefresh(const std::vector<MenuItem>& items, const std::string& subtitle) {
    gMenu.items = items;
    gMenu.subtitle = subtitle;
    gMenu.cursor = Clamp(gMenu.cursor, 0, Max((int)items.size() - 1, 0));
}

bool menuIs(int owner) { return gMenu.open && gMenu.owner == owner; }

void menuUpdate(GameWorld& g, float dt) {
    gMenuFrame++;
    gMenu.chosen = -1;
    gMenu.cancelled = false;
    if (!gMenu.open) return;
    gMenu.anim = Min(1.f, gMenu.anim + dt * 6.f);
    if (gMenuFrame - gMenu.openedFrame < 2) return;   // ignore the key that opened the menu
    int n = (int)gMenu.items.size();
    const Controls& c = g.ctl;
    if (n > 0 && c.menuNav.y != 0.f) {
        int step = c.menuNav.y > 0.f ? -1 : 1;
        for (int k = 0; k < n; k++) {
            gMenu.cursor = (gMenu.cursor + step + n) % n;
            if (!gMenu.items[gMenu.cursor].label.empty()) break;
        }
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_UI_MOVE, 0.4f);
#endif
    }
    if (c.confirm.pressed && n > 0) {
        const MenuItem& it = gMenu.items[gMenu.cursor];
        if (it.enabled) {
            gMenu.chosen = it.id;
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_UI_SELECT, 0.6f);
#endif
        } else {
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_UI_ERROR, 0.6f);
#endif
        }
    }
    if (c.back.pressed || c.pause.pressed) {
        gMenu.cancelled = true;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_UI_BACK, 0.5f);
#endif
    }
    if (gMenuInject != -1) {
        if (gMenuInject == -2) gMenu.cancelled = true;
        else gMenu.chosen = gMenuInject;
        gMenuInject = -1;
    }
}

std::string menuRightText(const MenuItem& it) {
    if (!it.right.empty()) return it.right;
    if (it.price >= 0) return it.price == 0 ? std::string("FREE") : StrFormat("$%lld", it.price);
    return std::string();
}

// HUD help box version (no overlay hook): title, a window of items around the cursor, the detail line.
void menuFallback(GameWorld& g) {
    if (!gMenu.open) return;
    std::string s = "~b~" + gMenu.title + "~s~";
    if (!gMenu.subtitle.empty()) s += "  " + gMenu.subtitle;
    int n = (int)gMenu.items.size();
    int first = Clamp(gMenu.cursor - 3, 0, Max(0, n - 7));
    for (int i = first; i < Min(n, first + 7); i++) {
        const MenuItem& it = gMenu.items[i];
        std::string r = menuRightText(it);
        s += "~n~";
        s += i == gMenu.cursor ? "~y~> " : "   ";
        if (!it.enabled) s += "~l~";
        s += it.label;
        if (it.checked) s += " ~g~*~s~";
        if (!r.empty()) s += "  " + r;
        s += "~s~";
    }
    if (n > 0 && !gMenu.items[gMenu.cursor].detail.empty()) s += "~n~~c~" + gMenu.items[gMenu.cursor].detail + "~s~";
    s += "~n~~i:ENTER|A~ Select   ~i:BACKSPACE|B~ Back";
    g.help(s, 0.2f);
}

void menuDraw(GameWorld& g, float W, float H) {
    (void)g;
    if (!gMenu.open) return;
    float e = gMenu.anim * gMenu.anim * (3.f - 2.f * gMenu.anim);
    float u = H / 1080.f;
    int n = (int)gMenu.items.size();
    int visible = Min(n, gMenu.phoneStyle ? 7 : 10);
    int first = Clamp(gMenu.cursor - visible / 2, 0, Max(0, n - visible));
    UI::TextStyle ts;
    if (gMenu.phoneStyle) {
        // phone: rounded device bottom-right
        float pw = 330.f * u, ph = 560.f * u;
        float px = W - pw - 60.f * u, py = H - ph * e - 40.f * u + (1.f - e) * 60.f * u;
        UI::roundRect(px - 8 * u, py - 8 * u, pw + 16 * u, ph + 16 * u, 34 * u, UI::rgba(0.02f, 0.02f, 0.03f, 0.95f), 2.f * u, UI::rgba(0.3f, 0.3f, 0.35f, 1.f));
        UI::roundRectGradient(px, py, pw, ph, 28 * u, UI::rgba(0.1f, 0.05f, 0.2f, 0.97f), UI::rgba(0.02f, 0.1f, 0.18f, 0.97f));
        // status bar
        ts.size = 18.f * u;
        ts.color = UI::rgba(1, 1, 1, 0.85f);
        float tod = g.env ? g.env->timeOfDay : 12.f;
        UI::text(px + 22 * u, py + 14 * u, StrFormat("%02d:%02d", (int)tod, (int)(fmodf(tod, 1.f) * 60.f)).c_str(), ts);
        ts.align = UI::ALIGN_RIGHT;
        UI::text(px + pw - 22 * u, py + 14 * u, StrFormat("$%lld", g.pinfo.money).c_str(), ts);
        ts.align = UI::ALIGN_LEFT;
        ts.font = UI::FONT_HEADING;
        ts.size = 30.f * u;
        ts.color = 0xffffffffu;
        UI::text(px + 24 * u, py + 52 * u, gMenu.title.c_str(), ts);
        ts.font = UI::FONT_BODY;
        ts.size = 17.f * u;
        ts.color = UI::rgba(0.8f, 0.85f, 1.f, 0.7f);
        UI::text(px + 24 * u, py + 92 * u, gMenu.subtitle.c_str(), ts);
        float rowH = 50.f * u, y = py + 124 * u;
        for (int i = first; i < first + visible; i++) {
            const MenuItem& it = gMenu.items[i];
            bool sel = i == gMenu.cursor;
            if (sel) UI::roundRect(px + 12 * u, y, pw - 24 * u, rowH - 6 * u, 12 * u, UI::withAlpha(gMenu.accent, 0.9f));
            else UI::roundRect(px + 12 * u, y, pw - 24 * u, rowH - 6 * u, 12 * u, UI::rgba(1, 1, 1, 0.06f));
            ts.size = 21.f * u;
            ts.color = it.enabled ? 0xffffffffu : UI::rgba(1, 1, 1, 0.35f);
            UI::text(px + 28 * u, y + 11 * u, it.label.c_str(), ts);
            std::string r = menuRightText(it);
            if (!r.empty() || it.checked) {
                UI::TextStyle rs = ts;
                rs.align = UI::ALIGN_RIGHT;
                rs.size = 18.f * u;
                UI::text(px + pw - 28 * u, y + 13 * u, it.checked ? "ON" : r.c_str(), rs);
            }
            y += rowH;
        }
        if (n > 0 && !gMenu.items[gMenu.cursor].detail.empty()) {
            ts.size = 16.f * u;
            ts.color = UI::rgba(1, 1, 1, 0.75f);
            UI::textWrapped(px + 24 * u, py + ph - 92 * u, pw - 48 * u, gMenu.items[gMenu.cursor].detail.c_str(), ts);
        }
        return;
    }
    // shop / choice panel: left side
    float pw = 520.f * u;
    float px = 70.f * u - (1.f - e) * 80.f * u, py = 120.f * u;
    float headH = 96.f * u;
    UI::gradientRectH(px, py, pw, headH, gMenu.accent, UI::withAlpha(gMenu.accent, 0.55f));
    ts.font = UI::FONT_TITLE;
    ts.size = 50.f * u;
    ts.color = 0xffffffffu;
    ts.shadow = 2.f * u;
    ts.align = UI::ALIGN_CENTER;
    UI::text(px + pw * 0.5f, py + 18 * u, gMenu.title.c_str(), ts);
    ts = UI::TextStyle();
    float y = py + headH;
    UI::rect(px, y, pw, 40 * u, UI::rgba(0, 0, 0, 0.92f));
    ts.size = 20.f * u;
    ts.color = UI::withAlpha(gMenu.accent, 1.f);
    UI::text(px + 16 * u, y + 9 * u, gMenu.subtitle.c_str(), ts);
    ts.align = UI::ALIGN_RIGHT;
    ts.color = UI::rgba(1, 1, 1, 0.8f);
    UI::text(px + pw - 16 * u, y + 9 * u, StrFormat("%d / %d", gMenu.cursor + 1, Max(n, 1)).c_str(), ts);
    ts.align = UI::ALIGN_LEFT;
    y += 40 * u;
    float rowH = 42.f * u;
    UI::rect(px, y, pw, rowH * visible, UI::rgba(0, 0, 0, 0.72f));
    for (int i = first; i < first + visible; i++) {
        const MenuItem& it = gMenu.items[i];
        bool sel = i == gMenu.cursor;
        if (sel) UI::rect(px, y, pw, rowH, UI::rgba(0.95f, 0.95f, 0.95f, 0.95f));
        u32 col = sel ? UI::rgba(0.05f, 0.05f, 0.06f, 1.f) : 0xffffffffu;
        if (!it.enabled) col = sel ? UI::rgba(0.3f, 0.3f, 0.3f, 1.f) : UI::rgba(1, 1, 1, 0.35f);
        ts.size = 22.f * u;
        ts.color = col;
        UI::text(px + 16 * u, y + 9 * u, it.label.c_str(), ts);
        std::string r = it.checked ? std::string("OWNED") : menuRightText(it);
        if (!r.empty()) {
            UI::TextStyle rs = ts;
            rs.align = UI::ALIGN_RIGHT;
            if (it.checked) rs.color = sel ? UI::rgba(0.1f, 0.5f, 0.2f, 1.f) : UI::rgba(0.4f, 0.95f, 0.5f, 1.f);
            UI::text(px + pw - 16 * u, y + 9 * u, r.c_str(), rs);
        }
        y += rowH;
    }
    if (n > 0 && !gMenu.items[gMenu.cursor].detail.empty()) {
        float dh = 76.f * u;
        UI::rect(px, y + 6 * u, pw, dh, UI::rgba(0, 0, 0, 0.8f));
        UI::rect(px, y + 6 * u, 4.f * u, dh, gMenu.accent);
        ts.size = 18.f * u;
        ts.color = UI::rgba(1, 1, 1, 0.9f);
        UI::textWrapped(px + 16 * u, y + 16 * u, pw - 32 * u, gMenu.items[gMenu.cursor].detail.c_str(), ts);
        y += dh + 6 * u;
    }
    ts.size = 17.f * u;
    ts.color = UI::rgba(1, 1, 1, 0.7f);
    UI::text(px + 4 * u, y + 12 * u, g.ctl.usingPad ? "(A) Select    (B) Back" : "[Enter] Select    [Backspace] Back", ts);
}

}  // namespace mu
}  // namespace Game
