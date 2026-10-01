// In-game HUD (minimap/radar, status, weapon, wanted level, messages, subtitles, reticle), full-screen map,
// front-end + pause menus, settings screens, loading screen, phone. The game fills HudState every frame and calls
// drawHud(); menus are driven by Menus::update() which returns actions for the game to execute.
#pragma once
#include "draw2d.h"
#include "../platform/platform.h"

namespace UI {

enum BlipIcon : u8 {
    BLIP_DOT = 0,        // generic small dot (peds, pickups)
    BLIP_PLAYER,         // arrow
    BLIP_WAYPOINT,
    BLIP_MISSION,        // story mission start (letter of the contact drawn inside, see Blip::letter)
    BLIP_OBJECTIVE,      // current objective (yellow)
    BLIP_ENEMY,          // red
    BLIP_FRIEND,         // blue
    BLIP_POLICE,         // flashing red/blue
    BLIP_POLICE_HELI,
    BLIP_VEHICLE,        // target vehicle
    BLIP_SAFEHOUSE,
    BLIP_GUN_SHOP,
    BLIP_CLOTHES_SHOP,
    BLIP_CAR_SHOP,
    BLIP_GARAGE,         // mod shop / garage
    BLIP_HOSPITAL,
    BLIP_POLICE_STATION,
    BLIP_RACE,
    BLIP_TAXI_JOB,
    BLIP_DELIVERY_JOB,
    BLIP_VIGILANTE,
    BLIP_STUNT_JUMP,
    BLIP_COLLECTIBLE,
    BLIP_BOAT,
    BLIP_HELI,
    BLIP_PLANE,
    BLIP_BAR,
    BLIP_CONVENIENCE_STORE,
    BLIP_BANK,
    BLIP_AIRPORT,
    BLIP_HIDEOUT,        // gang hideout / criminal activity
    BLIP_COUNT
};

struct Blip {
    vec2 pos;                // world XY
    float heightDiff = 0.f;  // blip z - player z (renders up/down indicators when |d| > 4 m)
    BlipIcon icon = BLIP_DOT;
    u32 color = 0xffffffff;  // tint for DOT/ENEMY/FRIEND/VEHICLE; icons have their own colors when 0. For the semantic
                             // icons (ENEMY, FRIEND, POLICE*, OBJECTIVE, WAYPOINT, MISSION) the default white also means
                             // "use the icon color" (red enemies, blue friends, flashing police, yellow objective...)
    float scale = 1.f;
    char letter = 0;         // mission contact initial for BLIP_MISSION
    bool flash = false;
    bool shortRange = false; // only on the minimap when close (< 250 m); always on the full map
    bool edge = true;        // clamp to minimap edge when outside the radar
    const char* label = nullptr;  // name in the full-map legend / hover (original names, e.g. "Palmetto Arms")
};

struct Subtitle {
    std::string speaker;     // may be empty
    std::string text;
    u32 speakerColor = 0xff66ccff;
};

struct HudState {
    // Player / camera
    vec2 playerPos;          // world XY
    float playerZ = 0.f;
    float playerHeading = 0.f;   // radians, 0 = facing +Y (north), counter-clockwise positive
    float cameraHeading = 0.f;   // radians (radar rotates with the camera)
    float health = 1.f;          // 0..1
    float armor = 0.f;           // 0..1
    float breath = 1.f;          // under water 0..1 (bar shown when < 1)
    float stamina = 1.f;         // sprint stamina (shown briefly when < 1)
    float special = 1.f;         // protagonist Focus ability meter 0..1 (third bar under the radar, GTA-style)
    bool specialActive = false;  // Focus active: bar pulses, subtle screen treatment
    bool dead = false;           // "WASTED" screen handled by game via bigMessage
    // Money
    long long money = 0;
    long long moneyDelta = 0;    // non-zero -> animated "+$500"/"-$200" popup (game sets once per change)
    // Wanted
    int wanted = 0;              // 0..5 stars
    bool wantedSearching = false;   // stars flash while police lost sight (cooldown)
    float wantedCooldown = 0.f;     // 0..1 progress of evasion
    std::vector<vec2> searchAreaCenters;   // police search zones (minimap red/blue circles)
    std::vector<float> searchAreaRadii;
    // Weapon
    bool showWeapon = false;
    std::string weaponName;
    int weaponIcon = 0;          // 0 fists, 1 knife, 2 bat, 3 pistol, 4 revolver, 5 smg, 6 rifle, 7 shotgun, 8 sniper,
                                 // 9 rpg, 10 grenade, 11 molotov, 12 minigun
    int ammoClip = 0, ammoTotal = 0;
    bool reloading = false;
    // Weapon wheel (opened while the key/button is held): slot names and selection
    bool weaponWheelOpen = false;
    std::vector<std::string> wheelSlots;   // up to 8 entries, "" = empty slot
    std::vector<int> wheelIcons;
    int wheelSelected = -1;
    // Aiming
    bool aiming = false;
    float reticleSpread = 0.f;   // pixels of spread at 1080p
    bool reticleOnEnemy = false; // red reticle
    bool reticleOnFriendly = false;
    float hitMarker = 0.f;       // > 0 shows a hit marker (fades, game sets 1 on hit)
    bool killMarker = false;
    // Lock-on target marker (chevron above the target's head with a health bar)
    bool lockOn = false;         // soft lock (guns, pad) or melee lock-on active
    vec2 lockScreen;             // target head position in pixels (backbuffer coordinates)
    float lockHealth = 1.f;      // target health 0..1 (the bar shows below 1; drops pulse the marker)
    bool lockHostile = false;    // target is fighting the player (red) vs neutral (white)
    bool lockMelee = false;      // melee lock (fighting stance) vs gun lock
    std::vector<float> damageDirections;   // screen-space angles (rad, 0 = up/ahead, counter-clockwise positive like
                                           // headings: +pi/2 = attacker on the left) of recent hits taken. An entry may
                                           // be pushed for one frame or kept while fresh; the UI matches entries across
                                           // frames and fades them out itself.
    // Vehicle
    bool inVehicle = false;
    std::string vehicleName;     // shown briefly on entering (game sets vehicleNameTimer)
    float vehicleNameTimer = 0.f;
    float speedKmh = 0.f;
    bool showSpeedometer = false;   // aircraft show altitude instead
    bool aircraft = false;
    float altitude = 0.f;
    // Radio (shown briefly on change)
    std::string radioStation;    // "" = radio off
    std::string radioTrack;      // "Artist - Title"
    float radioTimer = 0.f;      // > 0 shows the station banner
    // Location (shown on change: district + street, game sets locationTimer)
    std::string zoneName, streetName;
    float locationTimer = 0.f;
    // Time/weather (shown on the pause map and in the top-right corner when the phone is out)
    float timeOfDay = 12.f;
    int day = 1;
    // Messages
    // Text markup (help, objective, subtitle text, notification): color codes ~r~ ~g~ ~b~ ~y~ ~o~ ~p~ (pink) ~c~ (cyan)
    // ~m~ (purple) ~w~ ~l~ (grey) ~s~ (reset), ~#RRGGBB~ custom color, ~n~ new line; input prompts ~k:E~ (keycap,
    // also ~k:LMB~ ~k:RMB~ ~k:WHEEL~), ~p:A~ (gamepad glyph: A B X Y LB RB LT RT LS RS START BACK UP DOWN LEFT RIGHT),
    // ~i:E|A~ (keyboard|pad, chosen by padPrompts). Example: "Press ~i:F|Y~ to enter the ~b~vehicle~s~."
    std::string helpText;        // top-left help box ("Press E to enter the vehicle"), empty = hidden
    std::string objective;       // mission objective line (bottom center, above subtitles) e.g. "Go to the ~y~marina~s~."
    Subtitle subtitle;           // dialogue line (empty text = hidden)
    std::string bigMessage;      // "MISSION PASSED", "WASTED", "BUSTED" (big center text), empty = hidden
    std::string bigMessageSub;   // e.g. "$12,500" or mission name
    u32 bigMessageColor = 0xff33ccff;
    float bigMessageTime = 0.f;  // seconds since it appeared (drives the animation)
    std::string notification;    // phone-style notification top-left (text message, "Saved"), empty = hidden
    std::string notificationTitle;
    float missionTimer = -1.f;   // >= 0 shows a mission countdown (seconds)
    std::string missionCounterLabel;   // e.g. "PACKAGES" with missionCounter/missionCounterMax, empty hidden
    int missionCounter = 0, missionCounterMax = 0;
    // Map data
    std::vector<Blip> blips;
    std::vector<vec2> gpsRoute;  // polyline (world XY) from player to waypoint/objective, drawn on radar + map
    u32 gpsColor = 0xffff66cc;
    bool hasWaypoint = false;
    vec2 waypoint;
    bool radarVisible = true;
    float radarZoom = 1.f;       // 1 = default (~220 m radius); game raises it with vehicle speed / aircraft altitude
    bool interior = false;
    // Additions
    bool padPrompts = false;     // show gamepad glyphs for ~i:~ prompts (set from InputState::lastInputWasPad)
    bool metricUnits = true;     // speedometer km/h vs mph, altimeter m vs ft (GameSettings::metricUnits)
    bool rendererScreenFx = false;  // true when the renderer's post-processing already grades the frame for low
                                    // health / damage, Focus, the weapon wheel and wasted/busted: the HUD then skips
                                    // its own full-screen vignettes and blur backdrops (widgets, arcs, banners stay)
};

void hudInit();                        // builds the map textures from World::gMap/gRoads/gBuildings (call after world gen)
void drawHud(const HudState& s, float dt);
// Frame order: UI::beginFrame -> drawHud (in gameplay) -> Menus::update (while a menu is open) -> UI::endFrame.
// drawHud / Menus::update only record draw calls; the 3D frame must already be in the back buffer (menus and the
// weapon wheel blur it). hudInit takes ~0.3-0.7 s (icon atlas + 2560^2 map texture + road/building/site grids); the
// icon atlas is also created lazily so menus and the loading screen work before hudInit. Measured CPU cost per frame
// (1080p): drawHud 0.05-0.09 ms natively (0.5 ms under Wine on a loaded machine); pause map ~0.3-0.8 ms.
// Layout: designed at 1920x1080 and scaled by height (by width on screens narrower than 16:9); HUD anchors stay
// inside the centered 16:9 region on ultrawide screens.
void hudReset();                       // forget HUD animation state (after loading a save / respawn teleport)

// ------------------------------------------------------------------------------------------------------------------
// Settings shared with the game (menus edit them; the game applies them).
struct GameSettings {
    // Display
    int resolutionIndex = -1;    // index into the display mode list, -1 = native desktop
    bool fullscreen = true;      // borderless fullscreen
    bool vsync = true;
    int quality = 2;             // 0 low, 1 medium, 2 high, 3 ultra
    float renderScale = 1.f;     // 0.5 .. 1.0 (upscaled with TAA)
    float fov = 60.f;            // vertical degrees 50..90
    bool motionBlur = true;
    float brightness = 0.f;      // exposure bias -1..1
    // Audio (0..1)
    float masterVolume = 1.f, sfxVolume = 0.9f, musicVolume = 0.7f, radioVolume = 0.8f, dialogueVolume = 1.f;
    bool subtitles = true;
    // Controls
    float mouseSensitivity = 1.f, padSensitivity = 1.f;
    bool invertY = false;
    bool vibration = true;
    bool aimAssist = true;
    // Gameplay
    bool showRadar = true, showHud = true;
    bool metricUnits = true;
};

enum MenuScreen : u8 { MENU_NONE = 0, MENU_MAIN, MENU_PAUSE, MENU_MAP, MENU_SETTINGS, MENU_LOAD, MENU_SAVE, MENU_STATS,
                       MENU_BRIEF, MENU_CONFIRM_QUIT, MENU_LOADING };

enum MenuActionType : u8 {
    MA_NONE = 0,
    MA_NEW_GAME, MA_CONTINUE, MA_LOAD_SLOT, MA_SAVE_SLOT, MA_RESUME, MA_QUIT_TO_MENU, MA_QUIT_GAME,
    MA_SETTINGS_CHANGED,   // settings were edited (apply live)
    MA_SET_WAYPOINT, MA_CLEAR_WAYPOINT,
};

struct MenuAction {
    MenuActionType type = MA_NONE;
    int slot = 0;
    vec2 pos;
};

struct SaveSlotInfo {
    bool used = false;
    std::string title;       // e.g. "Chapter 3 - Low Tide"
    std::string detail;      // e.g. "63.2% complete  |  $148,200  |  Day 12, 21:40"
    std::string timestamp;   // real-world save time
};

struct StatsEntry {
    std::string name, value;
};

// Front-end state (the game owns the instance and passes it every frame).
struct MenuState {
    MenuScreen screen = MENU_NONE;
    GameSettings settings;
    std::vector<SaveSlotInfo> slots;       // filled by the game before opening load/save
    std::vector<StatsEntry> stats;
    std::vector<Blip> mapBlips;            // blips for the full-screen map
    vec2 playerPos;
    float playerHeading = 0.f;
    bool hasWaypoint = false;
    vec2 waypoint;
    std::vector<vec2> gpsRoute;
    std::string briefTitle, briefText;     // mission log (MENU_BRIEF)
    float loadingProgress = 0.f;           // MENU_LOADING 0..1
    std::string loadingTip;
    bool canContinue = false;              // main menu shows "Continue"
    bool canSave = true;                   // pause menu offers "Save Game" (false during missions)
    // internal UI state (owned by the menu code)
    int cursor = 0, tab = 0;
    float anim = 0.f;
    vec2 mapCenter, mapPrevMouse;
    float mapZoom = 1.f;
    int confirmSlot = -1;
    bool dragging = false;
    MenuScreen prevScreen = MENU_NONE;
    // Additions (filled by the game, shown in the pause menu header)
    long long money = 0;
    float timeOfDay = 12.f;
    int day = 1;
    std::string playerName;            // optional, shown in the pause header
};

namespace Menus {
// Process input and draw the active screen. Call every frame while state.screen != MENU_NONE (after rendering the
// world/HUD, before UI::endFrame). Returns the action chosen this frame (MA_NONE most frames).
// Navigation: MENU_MAIN is the front-end root; MENU_PAUSE opens the tabbed pause menu (MAP, BRIEF, STATS, SETTINGS,
// SAVE when canSave, QUIT). Setting screen to MENU_MAP / MENU_BRIEF / MENU_STATS / MENU_SAVE from the game opens the
// pause menu on that tab. MENU_SETTINGS / MENU_LOAD opened from MENU_MAIN are shown as front-end pages. On MA_RESUME
// the menu sets screen = MENU_NONE itself; for the other actions the game decides what to show next.
MenuAction update(MenuState& state, const InputState& in, float dt);
// Display modes offered by the settings screen; GameSettings::resolutionIndex indexes this list (-1 = native desktop).
struct DisplayMode { int width, height; };
const std::vector<DisplayMode>& displayModes();
}

// ------------------------------------------------------------------------------------------------------------------
// Phone: handset in the bottom-right corner with built-in apps (Contacts, Messages, Tidegram social feed, Camera with
// photo mode, Map, Quick Save) plus apps the game adds (stores, realty, jobs, mission replay, character switch).
// The game owns a PhoneState, fills its data (contacts, messages, apps, call state, context) and calls Phone::update
// EVERY gameplay frame, after drawHud and before Menus::update (the Tidegram feed and incoming calls tick while the
// phone is closed; nothing is drawn then). Open the phone by setting open = true (phone key: Up arrow / D-pad up);
// the phone clears it and returns PA_CLOSED when the player backs out of the home screen.
// Input while open: arrows / D-pad navigate, Enter / A select, Backspace / B / right mouse back, wheel scrolls;
// capturingInput tells the game to ignore those inputs for gameplay. In photo mode the phone takes all input.
enum PhoneGlyph : u8 {
    PG_STAR = 0, PG_CAR, PG_HOUSE, PG_BRIEFCASE, PG_REPLAY, PG_SWITCH, PG_DOLLAR, PG_TROPHY, PG_BOAT, PG_PLANE, PG_MUSIC,
    PG_GEAR, PG_MAP, PG_USER, PG_COUNT
};

struct PhoneContact {
    int id = 0;                  // returned in PhoneAction::id
    std::string name;            // "Tomas"
    std::string subtitle;        // shown under the name ("New job: Low Tide", "Mechanic")
    bool mission = false;        // calling starts a story mission / story call (JOB badge)
    bool enabled = true;         // false: greyed out, cannot be called right now
    u32 color = 0;               // avatar color, 0 = derived from the name
};

struct PhoneMessage {
    int id = 0;                  // returned in PhoneAction::id
    int contactId = -1;          // >= 0 adds a "Call back" button (PA_CALL with that contact id)
    std::string from;
    std::string text;            // markup allowed (~y~ ~b~ ... see HudState)
    std::string time;            // "17:32", "Day 11"
    bool unread = true;          // the phone clears it when the message is opened (and returns PA_READ_MESSAGE)
    bool mission = false;        // mission text (JOB badge in the list)
    bool hasLocation = false;    // adds "Mark on map" (PA_SET_WAYPOINT with location)
    vec2 location;
    std::string actionLabel;     // non-empty adds a button with this label (PA_MESSAGE_ACTION, e.g. "Accept job")
};

struct PhoneListItem {           // one row in a game-provided app
    int id = 0;
    std::string label;
    std::string detail;          // second line (small)
    std::string right;           // short status at the right ("OWNED", "LOCKED", "+$400/day", "MARI")
    long long price = -1;        // >= 0 shows the price at the right (red when the player cannot afford it)
    bool enabled = true;
};

struct PhoneListApp {            // app tile after the built-in ones (store, realty, jobs, replay, switch...)
    int id = 0;                  // returned in PhoneAction::app
    std::string name;            // tile label + app title ("Wheels.ps")
    std::string subtitle;        // line under the title inside the app ("Cash $12,400")
    PhoneGlyph glyph = PG_STAR;
    u32 color = 0;               // tile color, 0 = palette color picked from the id
    bool action = false;         // no list: selecting the tile returns PA_APP_ACTION (e.g. "Switch to Dex")
    bool enabled = true;
    int badge = 0;               // number badge on the tile
    std::vector<PhoneListItem> items;
};

enum PhoneCallState : u8 { CALL_NONE = 0, CALL_OUTGOING, CALL_INCOMING, CALL_ACTIVE };

// Photo mode (Camera app): free camera, depth of field, time freeze, filters. While photo.active the phone flies the
// camera (camPos/Yaw/Pitch/Roll/Fov, Render::Camera conventions, within 30 m of playerPos and above ground/water) and
// the game must render from it, pause the simulation when freeze is set, hide the player model when hidePlayer is set
// and skip drawHud. DOF, filters and grading are applied by the UI on top of the frame (see UI::setSceneDepth).
struct PhotoMode {
    bool active = false;
    bool freeze = true;
    bool hidePlayer = false;
    vec3 camPos;
    float camYaw = 0.f, camPitch = 0.f, camRoll = 0.f;   // radians
    float camFov = 0.87f;                                // vertical, radians
    float autoFocusDistance = 0.f;  // game: distance to the surface under the screen center (raycast), 0 = unknown
    // Look settings (edited in the photo mode panel, kept between sessions)
    bool autoFocus = true, dof = true;
    float focusDistance = 8.f;   // meters (used when autoFocus is off or no distance is known)
    float aperture = 2.8f;       // f-stop 1.4 .. 16 (lower = shallower depth of field)
    int filter = 0;              // index into Phone::filterNames()
    float filterStrength = 1.f;
    float exposure = 0.f;        // EV
    float contrast = 1.f, saturation = 1.f;
    float temperature = 0.f;     // -1 cool .. +1 warm
    float vignette = 0.25f, grain = 0.f;
    int grid = 0;                // 0 off, 1 rule of thirds
    int frame = 0;               // 0 none, 1 polaroid, 2 film strip, 3 neon
};

struct PhoneState {
    bool open = false;
    // Context filled by the game every frame
    float timeOfDay = 12.f;
    int day = 1;
    int weather = 0;             // 0 clear, 1 cloudy, 2 rain, 3 storm, 4 fog (Tidegram chatter)
    int signal = 4;              // bars 0..4
    float battery = 0.8f;
    std::string owner;           // protagonist name ("Mari"): player's Tidegram handle and name
    long long money = 0;         // prices the player cannot afford are shown in red
    vec3 playerPos;              // photo mode range center
    vec3 cameraPos;              // gameplay camera: photo mode starts from it
    float cameraYaw = 0.f, cameraPitch = 0.f, cameraFov = 1.f;
    bool canQuickSave = true;
    std::string quickSaveNote;   // why quick save is unavailable ("Not available during missions")
    std::vector<PhoneContact> contacts;
    std::vector<PhoneMessage> messages;   // newest last
    std::vector<PhoneListApp> apps;
    // Calls: the game drives the state. Setting CALL_INCOMING opens the phone on the call screen; after PA_CALL the
    // phone shows CALL_OUTGOING until the game sets CALL_ACTIVE (connected) or CALL_NONE (no answer / ended).
    PhoneCallState call = CALL_NONE;
    std::string callName;
    int callContactId = -1;
    float callSeconds = 0.f;     // connected time shown during CALL_ACTIVE
    std::string toast;           // one-line status shown on the phone ("Game saved"); the phone clears it after display
    PhotoMode photo;
    // Output (read by the game)
    bool capturingInput = false; // phone is open and uses the navigation keys (arrows/Enter/Backspace, D-pad/A/B)
};

enum PhoneActionType : u8 {
    PA_NONE = 0,
    PA_CLOSED,               // phone put away
    PA_CALL,                 // id = contact id (the game plays the call / starts the story mission)
    PA_ANSWER, PA_DECLINE,   // incoming call
    PA_HANG_UP,              // outgoing or active call ended by the player
    PA_READ_MESSAGE,         // id = message id (opened for the first time)
    PA_MESSAGE_ACTION,       // id = message id (its actionLabel button)
    PA_SET_WAYPOINT,         // pos
    PA_OPEN_MAP,             // open the pause-menu map (MENU_MAP)
    PA_QUICK_SAVE,           // the game saves and sets toast ("Game saved") or an error
    PA_APP_ITEM,             // app = PhoneListApp::id, id = PhoneListItem::id
    PA_APP_ACTION,           // app = PhoneListApp::id (action tile)
    PA_ENTER_PHOTO_MODE,     // photo.active is now true (camera initialized from cameraPos/Yaw/Pitch/Fov)
    PA_EXIT_PHOTO_MODE,      // photo.active is now false
    PA_TAKE_PHOTO,           // this frame shows the finished photo without UI: save the back buffer after UI::endFrame
    PA_LIKE_POST,            // id = Tidegram post id (informational)
};

struct PhoneAction {
    PhoneActionType type = PA_NONE;
    int id = -1;
    int app = -1;
    vec2 pos;
};

namespace Phone {
PhoneAction update(PhoneState& st, const HudState& hud, const InputState& in, float dt);
const std::vector<std::string>& filterNames();   // photo mode filters
bool isOpen();                                   // phone visible on screen (the HUD hides its bottom-right widgets)
void reset();                                    // back to the home screen, closed (after loading a save / switching)
}

// Tidegram social feed: the game reports what just happened around the player; a little later NPC accounts (locals,
// news, traffic, police, tourists...) post about it with generated text. Cheap; call at the moment of the event.
enum TideEvent : u8 {
    TE_WANTED = 0,        // magnitude = wanted stars
    TE_ESCAPED,           // lost the police
    TE_BUSTED, TE_WASTED,
    TE_CAR_STOLEN,        // subject = vehicle name
    TE_CRASH,             // magnitude = speed km/h
    TE_EXPLOSION, TE_SHOOTING,
    TE_STUNT_JUMP,        // magnitude = distance m
    TE_SPEEDING,          // magnitude = speed km/h
    TE_MISSION_PASSED,    // subject = mission title
    TE_PURCHASE,          // subject = item (vehicle, property)
    TE_LOW_FLYBY,         // aircraft
    TE_RACE_WON,
    TE_WEATHER,           // subject = "rain" / "storm" / "fog"
    TE_COUNT
};
void tidegramReport(TideEvent ev, vec2 pos, const char* subject = nullptr, float magnitude = 0.f);
int tidegramUnread();    // posts added since the feed was last viewed

}  // namespace UI
