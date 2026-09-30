// Game settings support: key binding table helpers, key names, colour-blind correction matrices, text persistence
// (settings.ini) and the options the UI applies itself.
#include "ui_internal.h"

namespace UI {
namespace settings_detail {

using namespace uix;

struct ActionInfo {
    const char* name;
    const char* key;        // settings.ini key
    InputContext ctx;
};
const ActionInfo kActions[IA_COUNT] = {
    {"Move Forward", "move_forward", ICTX_FOOT},     {"Move Back", "move_back", ICTX_FOOT},
    {"Move Left", "move_left", ICTX_FOOT},           {"Move Right", "move_right", ICTX_FOOT},
    {"Sprint", "sprint", ICTX_FOOT},                 {"Jump", "jump", ICTX_FOOT},
    {"Walk", "walk", ICTX_FOOT},                     {"Crouch", "crouch", ICTX_FOOT},
    {"Take Cover", "cover", ICTX_FOOT},              {"Enter / Exit Vehicle", "enter_vehicle", ICTX_ANY},
    {"Attack", "attack", ICTX_ANY},                  {"Aim", "aim", ICTX_ANY},
    {"Reload", "reload", ICTX_ANY},                  {"Weapon Wheel", "weapon_wheel", ICTX_ANY},
    {"Interact / Vehicle Ability", "interact", ICTX_ANY},
    {"Accelerate", "accelerate", ICTX_VEHICLE},      {"Brake / Reverse", "brake", ICTX_VEHICLE},
    {"Steer Left", "steer_left", ICTX_VEHICLE},      {"Steer Right", "steer_right", ICTX_VEHICLE},
    {"Handbrake", "handbrake", ICTX_VEHICLE},        {"Horn", "horn", ICTX_VEHICLE},
    {"Headlights", "headlights", ICTX_VEHICLE},      {"Look Behind", "look_behind", ICTX_VEHICLE},
    {"Next Radio Station", "radio_next", ICTX_VEHICLE},
    {"Previous Radio Station", "radio_prev", ICTX_VEHICLE}, {"Camera View", "camera_view", ICTX_ANY},
    {"Focus", "focus", ICTX_ANY},                    {"Phone", "phone", ICTX_ANY},
    {"Map", "map", ICTX_ANY},
};

// UI options before the settings file is applied: the default bindings, so prompts are right from the first frame
UiOptions defaultOptions() {
    UiOptions o;
    GameSettings d;
    memcpy(o.keyBinds, d.keyBinds, sizeof(o.keyBinds));
    return o;
}
UiOptions g_opts = defaultOptions();

// One persisted field: key, type (b bool, i int, f float) and address inside a GameSettings
struct Field {
    const char* key;
    char type;
    void* ptr;
};

std::vector<Field> fieldsOf(GameSettings& s) {
    return {
        {"resolution", 'i', &s.resolutionIndex}, {"fullscreen", 'b', &s.fullscreen}, {"vsync", 'b', &s.vsync},
        {"quality", 'i', &s.quality}, {"render_scale", 'f', &s.renderScale}, {"fov", 'f', &s.fov},
        {"motion_blur", 'b', &s.motionBlur}, {"brightness", 'f', &s.brightness}, {"frame_rate_cap", 'i', &s.frameRateCap},
        {"master_volume", 'f', &s.masterVolume}, {"sfx_volume", 'f', &s.sfxVolume}, {"music_volume", 'f', &s.musicVolume},
        {"radio_volume", 'f', &s.radioVolume}, {"dialogue_volume", 'f', &s.dialogueVolume}, {"subtitles", 'b', &s.subtitles},
        {"mouse_sensitivity", 'f', &s.mouseSensitivity}, {"pad_sensitivity", 'f', &s.padSensitivity},
        {"mouse_sensitivity_y", 'f', &s.mouseSensitivityY}, {"pad_sensitivity_y", 'f', &s.padSensitivityY},
        {"invert_y", 'b', &s.invertY}, {"vibration", 'b', &s.vibration}, {"aim_assist", 'b', &s.aimAssist},
        {"aim_toggle", 'b', &s.aimToggle}, {"sprint_toggle", 'b', &s.sprintToggle}, {"crouch_toggle", 'b', &s.crouchToggle},
        {"pad_layout", 'i', &s.padLayout},
        {"first_person_on_foot", 'b', &s.firstPersonOnFoot}, {"first_person_vehicle", 'b', &s.firstPersonVehicle},
        {"fov_first_person", 'f', &s.fovFirstPerson}, {"camera_shake", 'f', &s.cameraShake},
        {"vehicle_auto_center", 'b', &s.vehicleAutoCenter}, {"head_bob", 'b', &s.headBob},
        {"show_radar", 'b', &s.showRadar}, {"show_hud", 'b', &s.showHud}, {"metric_units", 'b', &s.metricUnits},
        {"subtitle_size", 'i', &s.subtitleSize}, {"subtitle_background", 'f', &s.subtitleBackground},
        {"speaker_colors", 'b', &s.speakerColors}, {"hud_scale", 'f', &s.hudScale},
        {"high_contrast_reticle", 'b', &s.highContrastReticle}, {"colorblind_mode", 'i', &s.colorblindMode},
        {"reduce_flashing", 'b', &s.reduceFlashing}, {"music_ducking", 'f', &s.musicDucking},
    };
}

std::string trim(const std::string& t) {
    size_t a = t.find_first_not_of(" \t\r\n"), b = t.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

// Keeps every value inside the range the menus offer (hand-edited or older files)
void sanitize(GameSettings& s) {
    s.quality = Clamp(s.quality, 0, 3);
    s.renderScale = Clamp(s.renderScale, 0.5f, 1.f);
    s.fov = Clamp(s.fov, 50.f, 90.f);
    s.brightness = Clamp(s.brightness, -1.f, 1.f);
    if (s.frameRateCap != 30 && s.frameRateCap != 60 && s.frameRateCap != 120) s.frameRateCap = 0;
    float* vols[5] = {&s.masterVolume, &s.sfxVolume, &s.musicVolume, &s.radioVolume, &s.dialogueVolume};
    for (float* v : vols) *v = Saturate(*v);
    s.mouseSensitivity = Clamp(s.mouseSensitivity, 0.1f, 3.f);
    s.padSensitivity = Clamp(s.padSensitivity, 0.1f, 3.f);
    s.mouseSensitivityY = Clamp(s.mouseSensitivityY, 0.1f, 3.f);
    s.padSensitivityY = Clamp(s.padSensitivityY, 0.1f, 3.f);
    s.padLayout = Clamp(s.padLayout, 0, 2);
    s.fovFirstPerson = Clamp(s.fovFirstPerson, 55.f, 90.f);
    s.cameraShake = Saturate(s.cameraShake);
    s.subtitleSize = Clamp(s.subtitleSize, 0, 3);
    s.subtitleBackground = Saturate(s.subtitleBackground);
    s.hudScale = Clamp(s.hudScale, 0.75f, 1.25f);
    s.colorblindMode = Clamp(s.colorblindMode, 0, 3);
    s.musicDucking = Saturate(s.musicDucking);
    for (int a = 0; a < IA_COUNT; a++)
        for (int k = 0; k < 2; k++)
            if (s.keyBinds[a][k] >= 256) s.keyBinds[a][k] = 0;
}

// Key code from a settings.ini key name ("W", "PAGE UP", "LMB"; a plain number is taken as a key code); 0 = none
int keyFromName(const std::string& raw) {
    std::string t = trim(raw);
    if (t.empty()) return 0;
    if (t.find_first_not_of("0123456789") == std::string::npos) return Clamp(atoi(t.c_str()), 0, 255);
    std::string u = t;
    for (char& c : u) c = (char)toupper((unsigned char)c);
    if (u == "NONE") return 0;
    for (int vk = 1; vk < 256; vk++)
        if (keyName(vk) == u) return vk;
    return 0;
}

}  // namespace settings_detail

const char* inputActionName(InputAction a) { return (int)a < IA_COUNT ? settings_detail::kActions[a].name : ""; }
InputContext inputActionContext(InputAction a) { return (int)a < IA_COUNT ? settings_detail::kActions[a].ctx : ICTX_ANY; }

std::string keyName(int vk) {
    if (vk <= 0) return "";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x70 + 1);
    if (vk >= 0x60 && vk <= 0x69) return "NUM " + std::to_string(vk - 0x60);
    switch (vk) {
        case 0x01: return "LMB";
        case 0x02: return "RMB";
        case 0x04: return "MMB";
        case 0x05: return "MOUSE 4";
        case 0x06: return "MOUSE 5";
        case 0x08: return "BACKSPACE";
        case 0x09: return "TAB";
        case 0x0D: return "ENTER";
        case 0x10: return "SHIFT";
        case 0x11: return "CTRL";
        case 0x12: return "ALT";
        case 0x14: return "CAPS LOCK";
        case 0x1B: return "ESC";
        case 0x20: return "SPACE";
        case 0x21: return "PAGE UP";
        case 0x22: return "PAGE DOWN";
        case 0x23: return "END";
        case 0x24: return "HOME";
        case 0x25: return "LEFT";
        case 0x26: return "UP";
        case 0x27: return "RIGHT";
        case 0x28: return "DOWN";
        case 0x2D: return "INSERT";
        case 0x2E: return "DELETE";
        case 0x6A: return "NUM *";
        case 0x6B: return "NUM +";
        case 0x6D: return "NUM -";
        case 0x6E: return "NUM .";
        case 0x6F: return "NUM /";
        case 0xA0: return "LEFT SHIFT";
        case 0xA1: return "RIGHT SHIFT";
        case 0xA2: return "LEFT CTRL";
        case 0xA3: return "RIGHT CTRL";
        case 0xA4: return "LEFT ALT";
        case 0xA5: return "RIGHT ALT";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: break;
    }
    char b[16];
    snprintf(b, sizeof(b), "KEY %02X", vk);
    return b;
}

bool bindingDown(const GameSettings& s, const InputState& in, InputAction a) {
    if ((int)a >= IA_COUNT) return false;
    for (int k = 0; k < 2; k++) {
        int vk = s.keyBinds[a][k];
        if (vk > 0 && vk < KEY_COUNT && in.down(vk)) return true;
    }
    return false;
}

bool bindingPressed(const GameSettings& s, const InputState& in, InputAction a) {
    if ((int)a >= IA_COUNT) return false;
    bool pressed = false;
    for (int k = 0; k < 2; k++) {
        int vk = s.keyBinds[a][k];
        if (vk > 0 && vk < KEY_COUNT && in.pressed(vk)) pressed = true;
    }
    // a second key of the same action already held does not re-trigger
    for (int k = 0; k < 2 && pressed; k++) {
        int vk = s.keyBinds[a][k];
        if (vk > 0 && vk < KEY_COUNT && in.down(vk) && !in.pressed(vk) && in.prevKeys[vk]) return false;
    }
    return pressed;
}

bool bindingReleased(const GameSettings& s, const InputState& in, InputAction a) {
    if ((int)a >= IA_COUNT) return false;
    bool released = false;
    for (int k = 0; k < 2; k++) {
        int vk = s.keyBinds[a][k];
        if (vk > 0 && vk < KEY_COUNT && in.released(vk)) released = true;
    }
    return released && !bindingDown(s, in, a);
}

void colorblindMatrix(int mode, float m[9]) {
    static const float kM[3][9] = {
        // protanopia
        {1.0000f, 0.0000f, 0.0000f, 0.4789f, 0.4769f, 0.0442f, 0.5973f, -0.6887f, 1.0914f},
        // deuteranopia
        {1.0000f, 0.0000f, 0.0000f, 0.1628f, 0.7250f, 0.1122f, 0.4547f, -0.6454f, 1.1907f},
        // tritanopia
        {0.7412f, -0.4072f, 0.6660f, 0.0751f, 0.5852f, 0.3397f, 0.0000f, 0.0000f, 1.0000f},
    };
    if (mode < 1 || mode > 3) {
        for (int i = 0; i < 9; i++) m[i] = (i % 4 == 0) ? 1.f : 0.f;
        return;
    }
    for (int i = 0; i < 9; i++) m[i] = kM[mode - 1][i];
}

std::string settingsToText(const GameSettings& src) {
    using namespace settings_detail;
    GameSettings s = src;
    std::string out = "# NEON TIDE settings (key=value; delete the file to restore every default)\n";
    char b[96];
    for (const Field& f : fieldsOf(s)) {
        if (f.type == 'b') snprintf(b, sizeof(b), "%s=%d\n", f.key, *(bool*)f.ptr ? 1 : 0);
        else if (f.type == 'i') snprintf(b, sizeof(b), "%s=%d\n", f.key, *(int*)f.ptr);
        else snprintf(b, sizeof(b), "%s=%.4g\n", f.key, *(float*)f.ptr);
        out += b;
    }
    out += "# key bindings: primary | secondary (key names as shown in the menu, NONE when unbound)\n";
    for (int a = 0; a < IA_COUNT; a++) {
        std::string k0 = s.keyBinds[a][0] ? keyName(s.keyBinds[a][0]) : "NONE", k1 = s.keyBinds[a][1] ? keyName(s.keyBinds[a][1]) : "NONE";
        out += StrFormat("bind.%s=%s | %s\n", kActions[a].key, k0.c_str(), k1.c_str());
    }
    return out;
}

void settingsFromText(const std::string& text, GameSettings& s) {
    using namespace settings_detail;
    std::vector<Field> fields = fieldsOf(s);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key.compare(0, 5, "bind.") == 0) {
            std::string an = key.substr(5);
            for (int a = 0; a < IA_COUNT; a++)
                if (an == kActions[a].key) {
                    size_t bar = val.find('|');
                    s.keyBinds[a][0] = (u16)keyFromName(val.substr(0, bar));
                    s.keyBinds[a][1] = bar == std::string::npos ? 0 : (u16)keyFromName(val.substr(bar + 1));
                }
            continue;
        }
        for (const Field& f : fields)
            if (key == f.key) {
                if (f.type == 'b') *(bool*)f.ptr = atoi(val.c_str()) != 0;
                else if (f.type == 'i') *(int*)f.ptr = atoi(val.c_str());
                else *(float*)f.ptr = (float)atof(val.c_str());
                break;
            }
    }
    sanitize(s);
}

void applyUiSettings(const GameSettings& s) {
    uix::UiOptions& o = settings_detail::g_opts;
    static const float kSubScale[4] = {0.82f, 1.f, 1.22f, 1.48f};
    o.subtitleScale = kSubScale[Clamp(s.subtitleSize, 0, 3)];
    o.subtitleBackground = Saturate(s.subtitleBackground);
    o.speakerColors = s.speakerColors;
    o.hudScale = Clamp(s.hudScale, 0.75f, 1.25f);
    o.highContrastReticle = s.highContrastReticle;
    o.reduceFlashing = s.reduceFlashing;
    o.padLayout = Clamp(s.padLayout, 0, 2);
    memcpy(o.keyBinds, s.keyBinds, sizeof(o.keyBinds));
    float m[9];
    colorblindMatrix(s.colorblindMode, m);
    setColorMatrix(s.colorblindMode > 0 ? m : nullptr);
}

namespace uix {
const UiOptions& uiOptions() { return settings_detail::g_opts; }

int actionFromKey(const std::string& iniKey) {
    for (int a = 0; a < IA_COUNT; a++)
        if (iniKey == settings_detail::kActions[a].key) return a;
    return -1;
}

int legacyPromptAction(const std::string& kb, const std::string& pad) {
    static const struct { const char* kb; const char* pad; InputAction a; } kPairs[] = {
        {"F", "Y", IA_ENTER_VEHICLE}, {"TAB", "LB", IA_WEAPON_WHEEL}, {"RMB", "LT", IA_AIM},        {"LMB", "RT", IA_ATTACK},
        {"SHIFT", "A", IA_SPRINT},    {"SPACE", "X", IA_JUMP},        {"Q", "RB", IA_COVER},         {"G", "UP", IA_INTERACT},
        {"UP", "UP", IA_PHONE},       {"PGUP", "RIGHT", IA_RADIO_NEXT}, {"PGDN", "LEFT", IA_RADIO_PREV}, {"H", "DOWN", IA_HEADLIGHTS},
        {"E", "LS", IA_HORN},         {"V", "BACK", IA_CAMERA_VIEW},  {"CTRL", "LS", IA_CROUCH},     {"R", "B", IA_RELOAD},
        {"C", "RS", IA_LOOK_BEHIND},
    };
    for (const auto& p : kPairs)
        if (kb == p.kb && pad == p.pad) return p.a;
    return -1;
}

std::string actionPromptKey(int a, bool pad) {
    const UiOptions& o = settings_detail::g_opts;
    if (a < 0 || a >= IA_COUNT) return "?";
    if (pad) {
        bool alt = o.padLayout == 1, south = o.padLayout == 2;
        const char* moveStick = south ? "RS" : "LS";
        const char* lookStick = south ? "LS" : "RS";
        switch (a) {
        case IA_MOVE_FORWARD: case IA_MOVE_BACK: case IA_MOVE_LEFT: case IA_MOVE_RIGHT: case IA_WALK:
        case IA_STEER_LEFT: case IA_STEER_RIGHT: return moveStick;
        case IA_SPRINT: return alt ? "X" : "A";
        case IA_JUMP: return alt ? "A" : "X";
        case IA_CROUCH: case IA_HORN: return moveStick;
        case IA_COVER: case IA_HANDBRAKE: return "RB";
        case IA_ENTER_VEHICLE: return "Y";
        case IA_ATTACK: case IA_ACCELERATE: return "RT";
        case IA_AIM: case IA_BRAKE: return "LT";
        case IA_RELOAD: return "B";
        case IA_WEAPON_WHEEL: return "LB";
        case IA_INTERACT: case IA_PHONE: return "UP";
        case IA_HEADLIGHTS: return "DOWN";
        case IA_LOOK_BEHIND: case IA_FOCUS: return lookStick;
        case IA_RADIO_NEXT: return "RIGHT";
        case IA_RADIO_PREV: return "LEFT";
        case IA_CAMERA_VIEW: return "BACK";
        case IA_MAP: return "START";
        default: return "?";
        }
    }
    int vk = o.keyBinds[a][0] ? o.keyBinds[a][0] : o.keyBinds[a][1];
    return vk ? keyName(vk) : std::string("UNBOUND");
}
}  // namespace uix

}  // namespace UI
