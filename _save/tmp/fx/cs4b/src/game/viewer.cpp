// Model viewer mode for content iteration: `--viewer vehicles` or `--viewer characters`.
// Vehicles are lined up along +X starting at kViewerOrigin, spaced 8 m apart, facing +Y (north).
// Characters are lined up 1.6 m apart at kViewerOrigin + (0, 20), each playing a different clip.
// Use --shot to place the camera, e.g. to look at vehicle i from the front-left:
//   --shot "X,Y,Z,yawDeg,pitchDeg,hour,name" with X = -300 + 8*i - 4, Y = 1500 + 6, Z = height + 1.6
// --protagonists puts Mari and Dex (protagonists.h) in the first two character slots; --clod 1|2 shows the
// characters' crowd LODs.
// Portrait close-ups (characters mode): --facecam i,dist,yawDeg, one per --shot in the same order. For the shot the
// camera is on, character i (and only it) is moved in front of the camera, facing it turned by yawDeg (positive turns
// the face to its own left), with its eyes dist metres away on the camera axis; beyond 2.5 m its feet stay on the
// ground instead. A close-up then works wherever the --shot puts the camera (a lit street at night, a side-lit plaza)
// and does not depend on body or eye height. Only the characters named by --facecam are built. --viewerlamp hangs
// a street lamp over the framed character (as roadmesh.cpp's: 8.6 m pole, 2.2 m arm, 7000 cd, warm;
// --viewerlamp=cool for the cool LED kind), 3 m towards the camera and 1.2 m to the side.
#include "protagonists.h"
namespace Game {

static const vec2 kViewerOrigin(-300.f, 1500.f);

struct Viewer {
    std::string mode;
    std::vector<Render::Model*> bodies, wheels, rotors, tailRotors, calipers, steerWheels;
    std::vector<std::vector<Render::Model*>> doorParts;   // full detail: the side doors (cut out of the body)
#ifdef HAVE_VEHICLE_MODELS
    std::vector<Vehicles::VehicleModel> vmodels;
    int vehicleLod = 0;  // --vlod 1|2 shows the distant levels of detail (buildVehicleLods)
#endif
#ifdef HAVE_CHARACTERS
    struct Ch {
        Anim::CharacterDesc desc;
        Anim::Skeleton skel;
        Render::Model* model = nullptr;
        Anim::Animator anim;
        std::vector<mat4> skin;
        int clip = 0;
    };
    std::vector<Ch> chars;
    struct FaceCam {
        int index;
        float dist, yawDeg;
    };
    std::vector<FaceCam> faceCams;
    struct ShotKey {
        dvec3 pos;
        float hour;
    };
    std::vector<ShotKey> shotKeys;   // the --shot cameras, to tell which --facecam applies
    int lampKind = -1;               // --viewerlamp: 0 warm, 1 cool
#endif
    float t = 0;

    void init(Render::Renderer& r, World::WorldMap& map) {
        const char* m = Platform::argValue("viewer");
        if (!m) return;
        mode = m;
        (void)map;
#ifdef HAVE_VEHICLE_MODELS
        if (mode == "vehicles") {
            int n = Vehicles::modelCount();
            if (const char* lv = Platform::argValue("vlod")) vehicleLod = Clamp(atoi(lv), 0, 2);
            for (int i = 0; i < n; i++) {
                Vehicles::VehicleModel vm;
                Vehicles::buildModel(i, vm);
                if (vehicleLod > 0) {
                    // LOD1 body + LOD1 wheel; LOD2 body has the wheels merged in (no wheel draws)
                    MeshData lods[2], wheel1;
                    Vehicles::buildVehicleLods(i, lods, &wheel1);
                    vm.body = std::move(lods[vehicleLod - 1]);
                    vm.wheel = std::move(wheel1);
                }
                bodies.push_back(r.dynamic->createModel(vm.body));
                wheels.push_back(vm.wheel.indices.empty() || vehicleLod == 2 ? nullptr : r.dynamic->createModel(vm.wheel));
                rotors.push_back(vm.rotor.indices.empty() ? nullptr : r.dynamic->createModel(vm.rotor));
                tailRotors.push_back(vm.tailRotor.indices.empty() ? nullptr : r.dynamic->createModel(vm.tailRotor));
                // brake calipers: wheel transform without the spin (the viewer's wheels do not spin anyway)
                calipers.push_back(vm.caliper.indices.empty() || vehicleLod > 0 ? nullptr : r.dynamic->createModel(vm.caliper));
                // the steering wheel is a part of its own at full detail (distant levels keep it in the body)
                steerWheels.push_back(vm.steerWheel.indices.empty() || vehicleLod > 0 ? nullptr : r.dynamic->createModel(vm.steerWheel));
                doorParts.emplace_back();
                for (const Vehicles::DoorSpec& ds : vm.doors)
                    doorParts.back().push_back(vehicleLod > 0 || ds.mesh.empty() ? nullptr : r.dynamic->createModel(ds.mesh));
                LOG("Vehicle %d: %s %s (%zu tris)", i, vm.maker.c_str(), vm.name.c_str(), vm.body.indices.size() / 3);
                vmodels.push_back(std::move(vm));
            }
        }
#endif
#ifdef HAVE_CHARACTERS
        if (mode == "characters") {
            int n = 16;
            if (const char* c = Platform::argValue("count")) n = atoi(c);
            for (int a = 1; a + 1 < Platform::argCount(); a++) {
                if (strcmp(Platform::arg(a), "--facecam") == 0) {
                    FaceCam f = {};
                    if (sscanf(Platform::arg(a + 1), "%d,%f,%f", &f.index, &f.dist, &f.yawDeg) == 3 && f.index >= 0) {
                        faceCams.push_back(f);
                        n = Max(n, f.index + 1);
                    }
                } else if (strcmp(Platform::arg(a), "--shot") == 0) {
                    ShotKey k = {};
                    float yaw, pitch;
                    if (sscanf(Platform::arg(a + 1), "%lf,%lf,%lf,%f,%f,%f", &k.pos.x, &k.pos.y, &k.pos.z, &yaw, &pitch, &k.hour) == 6)
                        shotKeys.push_back(k);
                }
            }
            if (Platform::hasArg("viewerlamp")) {
                const char* lk = Platform::argValue("viewerlamp");
                lampKind = lk && strcmp(lk, "cool") == 0 ? 1 : 0;
            }
            chars.resize(n);
            for (int i = 0; i < n; i++) {
                Ch& c = chars[i];
                bool framed = faceCams.empty();
                for (const FaceCam& f : faceCams) framed = framed || f.index == i;
                if (!framed) continue;
                c.desc = Anim::randomCharacter(1000 + i * 7919, i % 7);
                if (Platform::hasArg("protagonists") && i < 2) c.desc = protagonistDesc(i);
                Anim::buildSkeleton(c.desc, c.skel);
                SkinnedMeshData mesh;
                // --clod 1|2: the crowd LODs (LOD1 ~4.5k triangles, LOD2 ~1.5k) instead of the full mesh
                const int clod = Platform::argValue("clod") ? Clamp(atoi(Platform::argValue("clod")), 0, 2) : 0;
                if (clod > 0) Anim::buildCharacterMeshLod(c.desc, c.skel, clod, mesh);
                else Anim::buildCharacterMesh(c.desc, c.skel, mesh);
                c.model = r.dynamic->createSkinnedModel(mesh);
                c.anim.init(&c.skel, (u32)i);
                c.clip = i % Anim::CLIP_COUNT;
                if (const char* cl = Platform::argValue("clip")) c.clip = atoi(cl);
                c.skin.resize(Anim::B_COUNT);
            }
        }
#endif
#ifdef HAVE_GAME_UI
        // UI test modes: --viewer hud | --viewer menu <screen> | --viewer map  (see drawOverlay)
        if (mode == "hud" || mode == "menu" || mode == "map") {
            // hudInit is called by App::finishLoading; --uifast trims expensive passes for quick software-rendered tests
            if (Platform::hasArg("uifast")) {
                r.settings.clouds = false;
                r.settings.taa = false;
                r.settings.ssao = false;
                r.settings.ssr = false;
                r.settings.volumetrics = false;
                // smaller streaming radius so shots settle quickly on software rendering
                r.world->nearRadius = 240.f;
                r.world->farRadius = 1100.f;
                // quarter-area 3D rendering (the UI stays at full output resolution)
                r.settings.renderScale = 0.5f;
                int ow = r.outWidth, oh = r.outHeight;
                r.resize(ow - 2, oh);
                r.resize(ow, oh);
            }
        }
#endif
    }

    // 2D overlay for UI test modes (called between UI::beginFrame/endFrame after the world is rendered).
    void drawOverlay(Render::Renderer& r, float dt) {
        (void)r;
        (void)dt;
#ifdef HAVE_GAME_UI
        if (mode != "hud" && mode != "menu" && mode != "map") return;
        // Test harness for the game UI. Scenario per --shot name (keywords: onfoot, drive, wheel, passed, wasted,
        // busted, air, night...). Shot progress is detected from the screenshot files this process has written.
        //   --viewer hud  [--hudpos x,y] [--menupad]
        //   --viewer menu <main|pause|map|brief|stats|settings|save|load|quit|loading> [--menukeys k1,k2,..]
        //                 [--menumouse x,y] [--menupad]
        //   --viewer map
        struct UiTest {
            bool inited = false;
            std::vector<std::string> shotNames;
            std::string shotDir = "Z:\\tmp\\";
            int shot = 0;
            float shotT = 0.f;
            FILETIME startFt = {};
            UI::HudState hs;
            UI::MenuState ms;
            std::vector<vec2> route;
            int frame = 0, shotFrame = 0;
            double uiMs = 0.0, uiMaxMs = 0.0;
            int uiN = 0;
            std::vector<double> uiSamples;   // per-frame UI build cost of the current shot (median is robust to preemption)
            int verts = 0;
            std::vector<std::string> keys;
            std::string menuScreen = "main";
            bool pad = false;
            bool hasMouse = false;
            vec2 mouse;
            vec2 playerPos;
            bool hasPos = false;
            InputState prevIn;

            // Test GPS route: A* over the road graph from the node nearest the player to the node nearest the
            // target (or the reachable node closest to it when the target is on an unconnected island).
            static std::vector<vec2> makeRoute(vec2 from, vec2 to) {
                std::vector<vec2> out;
                if (!World::gRoads) return out;
                const World::RoadNetwork& net = *World::gRoads;
                int N = (int)net.nodes.size();
                int s = -1, goal = -1;
                float bs = 1e30f, bg = 1e30f;
                for (int i = 0; i < N; i++) {
                    if (net.nodes[i].edges.empty()) continue;
                    float ds = length2(net.nodes[i].p - from), dg = length2(net.nodes[i].p - to);
                    if (ds < bs && !net.nodes[i].highway) { bs = ds; s = i; }
                    if (dg < bg) { bg = dg; goal = i; }
                }
                if (s < 0 || goal < 0) return out;
                std::vector<float> gcost(N, 1e30f);
                std::vector<int> prevE(N, -1), prevN(N, -1);
                std::vector<char> closed(N, 0);
                struct QE { float f; int n; };
                std::vector<QE> heap;
                auto cmp = [](const QE& x, const QE& y) { return x.f > y.f; };
                gcost[s] = 0.f;
                heap.push_back({length(net.nodes[s].p - net.nodes[goal].p), s});
                int reached = s;
                float reachedD = length(net.nodes[s].p - to);
                while (!heap.empty()) {
                    std::pop_heap(heap.begin(), heap.end(), cmp);
                    int n = heap.back().n;
                    heap.pop_back();
                    if (closed[n]) continue;
                    closed[n] = 1;
                    float dn = length(net.nodes[n].p - to);
                    if (dn < reachedD) { reachedD = dn; reached = n; }
                    if (n == goal) break;
                    for (int ei : net.nodes[n].edges) {
                        const World::RoadEdge& e = net.edges[ei];
                        int o = e.n0 == n ? e.n1 : e.n0;
                        if (o < 0 || closed[o]) continue;
                        float ng = gcost[n] + Max(e.length, 1.f);
                        if (ng < gcost[o]) {
                            gcost[o] = ng;
                            prevE[o] = ei;
                            prevN[o] = n;
                            heap.push_back({ng + length(net.nodes[o].p - net.nodes[goal].p), o});
                            std::push_heap(heap.begin(), heap.end(), cmp);
                        }
                    }
                }
                std::vector<int> chain;
                for (int n = reached; n != s && n >= 0; n = prevN[n]) chain.push_back(n);
                out.push_back(from);
                int cur = s;
                for (int k = (int)chain.size() - 1; k >= 0; k--) {
                    int n = chain[k];
                    const World::RoadEdge& e = net.edges[prevE[n]];
                    if (e.n0 == cur) for (const vec3& p : e.pts) out.push_back(p.xy());
                    else for (int j = (int)e.pts.size() - 1; j >= 0; j--) out.push_back(e.pts[j].xy());
                    cur = n;
                }
                out.push_back(to);
                return out;
            }

            bool shotDone(int k) {
                if (k >= (int)shotNames.size()) return false;
                std::string path = shotDir + shotNames[k] + ".bmp";
                WIN32_FILE_ATTRIBUTE_DATA fa;
                if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa)) return false;
                return CompareFileTime(&fa.ftLastWriteTime, &startFt) > 0;
            }

            bool has(const char* kw) const {
                return shot < (int)shotNames.size() && shotNames[shot].find(kw) != std::string::npos;
            }
            // Shot name conventions: "..menu_<screen>.." opens a menu screen, "_k<key>-<key>-.." scripts key presses.
            std::string shotMenu() const {
                if (shot >= (int)shotNames.size()) return "";
                const std::string& n = shotNames[shot];
                size_t p = n.find("menu_");
                if (p == std::string::npos) return "";
                size_t e = n.find('_', p + 5);
                return n.substr(p + 5, e == std::string::npos ? std::string::npos : e - p - 5);
            }
            std::vector<std::string> shotKeys() const {
                std::vector<std::string> out;
                if (shot >= (int)shotNames.size()) return out;
                const std::string& n = shotNames[shot];
                size_t p = n.find("_k");
                if (p == std::string::npos) return out;
                std::string cur;
                for (size_t i = p + 2; i < n.size() && n[i] != '_'; i++) {
                    if (n[i] == '-') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
                    else cur.push_back(n[i]);
                }
                if (!cur.empty()) out.push_back(cur);
                return out;
            }

            void init(const Render::Renderer& rr) {
                inited = true;
                SYSTEMTIME st;
                GetSystemTime(&st);
                SystemTimeToFileTime(&st, &startFt);
                for (int i = 1; i < Platform::argCount(); i++) {
                    const char* a = Platform::arg(i);
                    if (strcmp(a, "--shot") == 0 && i + 1 < Platform::argCount()) {
                        std::string sarg = Platform::arg(i + 1);
                        size_t c = sarg.rfind(',');
                        shotNames.push_back(c == std::string::npos ? sarg : sarg.substr(c + 1));
                        i++;
                    }
                    if (strcmp(a, "menu") == 0 && i + 1 < Platform::argCount() && Platform::arg(i + 1)[0] != '-') menuScreen = Platform::arg(i + 1);
                }
                if (const char* d = Platform::argValue("shotdir")) shotDir = d;
                if (const char* sc = Platform::argValue("screen")) menuScreen = sc;
                pad = Platform::hasArg("menupad");
                if (const char* m = Platform::argValue("menumouse")) {
                    float x, y;
                    if (sscanf(m, "%f,%f", &x, &y) == 2) { mouse = vec2(x, y); hasMouse = true; }
                }
                if (const char* p = Platform::argValue("hudpos")) {
                    float x, y;
                    if (sscanf(p, "%f,%f", &x, &y) == 2) { playerPos = vec2(x, y); hasPos = true; }
                }
                if (const char* k = Platform::argValue("menukeys")) {
                    std::string ks = k, cur;
                    for (char ch : ks) {
                        if (ch == ',') { if (!cur.empty()) keys.push_back(cur); cur.clear(); }
                        else cur.push_back(ch);
                    }
                    if (!cur.empty()) keys.push_back(cur);
                }
                (void)rr;
            }

            void fillHud(const Render::Renderer& rr) {
                UI::HudState& h = hs;
                h = UI::HudState();
                vec2 cam((float)rr.camera.pos.x, (float)rr.camera.pos.y);
                vec2 pp = hasPos ? playerPos : cam;
                h.playerPos = pp;
                h.playerZ = World::gMap ? World::gMap->heightAt(pp.x, pp.y) : 0.f;
                h.cameraHeading = rr.camera.yaw;
                h.playerHeading = rr.camera.yaw + 0.35f;
                h.padPrompts = pad;
                h.health = 0.72f;
                h.armor = 0.45f;
                h.stamina = 0.62f;
                h.special = 0.68f;
                h.specialActive = has("focus");
                h.money = 148200;
                if (shotFrame == 2) h.moneyDelta = 2500;
                h.money += shotFrame >= 2 ? 2500 : 0;
                h.timeOfDay = 17.5f;
                h.day = 12;
                h.zoneName = World::gMap ? World::regionInfo(World::gMap->regionAt(pp.x, pp.y)).name : "Porto Sol";
                if (World::gRoads) {
                    int e = World::gRoads->nearestEdge(pp, 200.f);
                    if (e >= 0) h.streetName = World::gRoads->edges[e].name;
                }
                h.locationTimer = 3.f;
                // blips around the player
                auto blip = [&](vec2 off, UI::BlipIcon ic, float hd = 0.f, char letter = 0, const char* label = nullptr, bool shortRange = false) {
                    UI::Blip b;
                    b.pos = pp + off;
                    b.icon = ic;
                    b.heightDiff = hd;
                    b.letter = letter;
                    b.label = label;
                    b.shortRange = shortRange;
                    if (ic == UI::BLIP_POLICE || ic == UI::BLIP_POLICE_HELI) b.flash = false;
                    h.blips.push_back(b);
                };
                blip(vec2(60, 80), UI::BLIP_ENEMY);
                blip(vec2(95, 40), UI::BLIP_ENEMY, 9.f);
                blip(vec2(-55, 125), UI::BLIP_ENEMY, -7.f);
                blip(vec2(-30, 40), UI::BLIP_FRIEND);
                blip(vec2(140, -60), UI::BLIP_POLICE);
                blip(vec2(-160, 260), UI::BLIP_POLICE);
                blip(vec2(40, 170), UI::BLIP_POLICE_HELI, 60.f);
                blip(vec2(-600, 900), UI::BLIP_MISSION, 0.f, 'M', "Marisol");
                blip(vec2(200, -150), UI::BLIP_MISSION, 0.f, 'D', "Dex");
                blip(vec2(360, 420), UI::BLIP_OBJECTIVE);
                blip(vec2(25, -40), UI::BLIP_VEHICLE);
                blip(vec2(-150, -90), UI::BLIP_SAFEHOUSE, 0.f, 0, "Safehouse");
                blip(vec2(180, 60), UI::BLIP_GUN_SHOP, 0.f, 0, "Palmetto Arms");
                blip(vec2(-220, 20), UI::BLIP_HOSPITAL, 0.f, 0, "St. Marisol Hospital");
                blip(vec2(60, -180), UI::BLIP_CLOTHES_SHOP, 0.f, 0, "Sunwear Outlet");
                blip(vec2(-90, -200), UI::BLIP_BAR, 0.f, 0, "The Salt Rim");
                blip(vec2(300, 150), UI::BLIP_BANK, 0.f, 0, "Banco Palmera");
                blip(vec2(-40, 230), UI::BLIP_CONVENIENCE_STORE, 0.f, 0, "QuickStop");
                blip(vec2(-260, -160), UI::BLIP_RACE, 0.f, 0, "Street Race", true);
                blip(vec2(110, -110), UI::BLIP_COLLECTIBLE, 0.f, 0, "Sea Glass", true);
                blip(vec2(-330, 80), UI::BLIP_GARAGE, 0.f, 0, "Chrome & Spray");
                blip(vec2(250, -300), UI::BLIP_CAR_SHOP, 0.f, 0, "Coastline Motors");
                h.hasWaypoint = true;
                h.waypoint = pp + vec2(1300.f, 1700.f);
                if (route.empty()) route = makeRoute(pp, h.waypoint);
                h.gpsRoute = route;
                h.radarVisible = true;
                h.radarZoom = 1.f;
                h.metricUnits = true;
                bool wheel = has("wheel"), drive = has("drive"), air = has("air");
                // weapon
                h.showWeapon = !drive && !air;
                h.weaponName = "Marauder Rifle";
                h.weaponIcon = 6;
                h.ammoClip = 24;
                h.ammoTotal = 180;
                // wanted
                h.wanted = has("calm") ? 0 : 2;
                h.wantedSearching = true;
                h.wantedCooldown = 0.4f;
                h.searchAreaCenters.push_back(pp + vec2(70.f, 90.f));
                h.searchAreaRadii.push_back(170.f);
                if (drive || air) {
                    h.inVehicle = true;
                    h.vehicleName = air ? "Mako Skyhawk" : "Vapor GT";
                    h.vehicleNameTimer = 2.5f;
                    h.speedKmh = air ? 212.f : 87.f;
                    h.showSpeedometer = true;
                    h.radioStation = "Tide FM";
                    h.radioTrack = "Neon Palms - Midnight Causeway";
                    h.radioTimer = 2.f;
                    h.radarZoom = 1.5f;
                    h.objective = "Take the car to the ~y~Bayside Marina~s~ before the ~r~cops~s~ close the causeway.";
                    h.missionTimer = 94.f - shotT;
                    if (air) {
                        h.aircraft = true;
                        h.altitude = 186.f;
                        h.radarZoom = 2.4f;
                        h.missionTimer = 8.f;
                    }
                } else {
                    h.aiming = !wheel;
                    h.reticleSpread = 14.f;
                    h.reticleOnEnemy = true;
                    h.hitMarker = shotFrame < 40 ? Max(0.f, 1.f - shotFrame * 0.03f) : 0.f;
                    if (shotFrame < 40) h.damageDirections.push_back(1.2f);
                    h.helpText = "Press ~i:F|Y~ to steal the ~b~Vapor GT~s~. Hold ~i:RMB|LT~ to aim and ~i:LMB|RT~ to fire.";
                    h.objective = "Take the ~y~briefcase~s~ to ~p~Marisol~s~ at the ~y~Bayside Marina~s~.";
                    h.subtitle.speaker = "Marisol";
                    h.subtitle.text = "Don't scratch the case. Whatever is inside is worth more than your car.";
                    h.subtitle.speakerColor = 0xffff7aca;
                    h.notificationTitle = "Dex";
                    h.notification = "Cops are all over the Solano Causeway. Take the ~c~Venetia~s~ bridge instead.";
                    h.missionCounterLabel = "Packages";
                    h.missionCounter = 3;
                    h.missionCounterMax = 10;
                    h.breath = 1.f;
                }
                if (wheel) {
                    h.weaponWheelOpen = true;
                    h.wheelSlots = {"Fists", "Switchblade", "Vesper 9mm  45", "Kestrel SMG  120", "Marauder Rifle  204", "Tidebreaker 12ga  32",
                                    "", "Grenade  4"};
                    h.wheelIcons = {0, 1, 3, 5, 6, 7, -1, 10};
                    h.wheelSelected = 4;
                    h.helpText.clear();
                    h.subtitle = UI::Subtitle();
                }
                if (has("passed")) {
                    h.bigMessage = "MISSION PASSED";
                    h.bigMessageSub = "Low Tide  -  $25,000";
                    h.bigMessageTime = 1.4f + shotT;
                    h.aiming = false;
                    h.subtitle = UI::Subtitle();
                    h.helpText.clear();
                }
                if (has("wasted") || has("busted")) {
                    h.bigMessage = has("wasted") ? "WASTED" : "BUSTED";
                    h.bigMessageTime = 2.2f + shotT;
                    h.dead = has("wasted");
                    h.health = has("wasted") ? 0.f : h.health;
                    h.aiming = false;
                    h.subtitle = UI::Subtitle();
                    h.helpText.clear();
                    h.objective.clear();
                }
                if (has("swim")) {
                    h.breath = 0.38f;
                    h.showWeapon = false;
                    h.aiming = false;
                }
                if (has("lowhp")) h.health = 0.16f;
                if (has("lock")) {
                    // lock-on marker test: a target right of the reticle, hurt, hostile unless "neutral"
                    h.lockOn = true;
                    h.lockScreen = vec2(UI::screenWidth() * 0.6f, UI::screenHeight() * 0.46f);
                    h.lockHealth = shotFrame < 30 ? 0.62f : 0.41f;
                    h.lockHostile = !has("neutral");
                    h.lockMelee = has("melee");
                }
            }

            void fillMenu() {
                UI::MenuState& m = ms;
                m.playerPos = hs.playerPos;
                m.playerHeading = hs.playerHeading;
                m.hasWaypoint = hs.hasWaypoint;
                m.waypoint = hs.waypoint;
                m.gpsRoute = route;
                m.mapBlips = hs.blips;
                auto wb = [&](vec2 p, UI::BlipIcon ic, const char* label, char letter = 0) {
                    UI::Blip b;
                    b.pos = p;
                    b.icon = ic;
                    b.label = label;
                    b.letter = letter;
                    m.mapBlips.push_back(b);
                };
                wb(vec2(83, 1424), UI::BLIP_AIRPORT, "Porto Sol International");
                wb(vec2(4273, -201), UI::BLIP_DELIVERY_JOB, "Port Isle Freight");
                wb(vec2(4554, 7945), UI::BLIP_HIDEOUT, "Castell Crew Hideout");
                wb(vec2(-6000, 200), UI::BLIP_BOAT, "Sawgrass Airboats");
                wb(vec2(-8066, -9205), UI::BLIP_SAFEHOUSE, "Key Solano Safehouse");
                wb(vec2(275, 6742), UI::BLIP_TAXI_JOB, "Okahatchee Cabs");
                wb(vec2(-4997, 4856), UI::BLIP_STUNT_JUMP, "Harlow Quarry Jump");
                wb(vec2(-6891, 7235), UI::BLIP_HELI, "Ridge Heli Tours");
                wb(vec2(5125, 718), UI::BLIP_MISSION, "Vince", 'V');
                wb(vec2(2093, -3010), UI::BLIP_VIGILANTE, "Vigilante");
                wb(vec2(3185, -1319), UI::BLIP_POLICE_STATION, "PSPD Solaris");
                m.canContinue = true;
                m.canSave = true;
                m.money = hs.money;
                m.timeOfDay = 17.6f;
                m.day = 12;
                m.playerName = "Rafa Castillo";
                m.briefTitle = "Low Tide";
                m.briefText = "Marisol needs the briefcase from the Solaris tower before the Castell crew moves it offshore. ~y~Steal a fast car~s~, "
                              "lose the ~r~police~s~ on the causeway and meet her at the ~p~Bayside Marina~s~.~n~~n~The case is chained to a courier "
                              "who never leaves the 40th floor without two bodyguards. Dex will cut the elevator power at ~y~21:00~s~ - be in the "
                              "stairwell when the lights go out.";
                m.loadingProgress = 0.62f;
                const char* sn[][2] = {{"Time Played", "12h 41m"}, {"Game Progress", "27.4%"}, {"Missions Passed", "14 / 52"},
                                       {"Money Earned", "$412,850"}, {"Money Spent", "$264,650"}, {"Distance Driven", "384.2 km"},
                                       {"Distance on Foot", "41.8 km"}, {"Vehicles Stolen", "96"}, {"Top Speed", "243 km/h"},
                                       {"Stunt Jumps", "7 / 40"}, {"Sea Glass Collected", "18 / 100"}, {"Kills", "487"},
                                       {"Headshots", "211"}, {"Accuracy", "38.6%"}, {"Bullets Fired", "18,420"},
                                       {"Wanted Stars Attained", "312"}, {"Times Busted", "4"}, {"Times Wasted", "11"},
                                       {"Favorite Station", "Tide FM"}, {"Longest Jump", "88.4 m"}, {"Races Won", "6 / 15"},
                                       {"Safehouses Owned", "3"}};
                m.stats.clear();
                for (auto& e : sn) m.stats.push_back({e[0], e[1]});
                m.slots.clear();
                const char* titles[] = {"Chapter 3 - Low Tide", "Chapter 2 - Neon Nights", "", "Chapter 1 - Arrival", "", "", "Autosave", ""};
                const float kPct[8] = {27.4f, 21.3f, 0.f, 9.1f, 0.f, 0.f, 28.0f, 0.f};
                const int kMoneyK[8] = {148, 118, 0, 58, 0, 0, 151, 0}, kDay[8] = {12, 9, 0, 3, 0, 0, 12, 0};
                for (int i = 0; i < 8; i++) {
                    UI::SaveSlotInfo si;
                    si.used = titles[i][0] != 0;
                    if (si.used) {
                        si.title = titles[i];
                        si.detail = StrFormat("%.1f%% complete  |  $%d,%03d  |  Day %d, %02d:%02d", kPct[i], kMoneyK[i], 200, kDay[i], 21, 40 - i * 5);
                        si.timestamp = StrFormat("2026-09-%02d  %02d:%02d", 28 - i, 22 - i, 14 + i * 7);
                    }
                    m.slots.push_back(si);
                }
            }

            UI::MenuScreen screenFromName(const std::string& n) {
                if (n == "main") return UI::MENU_MAIN;
                if (n == "pause") return UI::MENU_PAUSE;
                if (n == "map") return UI::MENU_MAP;
                if (n == "brief") return UI::MENU_BRIEF;
                if (n == "stats") return UI::MENU_STATS;
                if (n == "settings") return UI::MENU_SETTINGS;
                if (n == "save") return UI::MENU_SAVE;
                if (n == "load") return UI::MENU_LOAD;
                if (n == "quit") return UI::MENU_CONFIRM_QUIT;
                if (n == "loading") return UI::MENU_LOADING;
                return UI::MENU_MAIN;
            }

            void scriptedInput(InputState& in) {
                in = InputState();
                memcpy(in.prevKeys, prevIn.keys, sizeof(in.keys));
                in.pad.prevButtons = prevIn.pad.buttons;
                in.lastInputWasPad = pad;
                in.pad.connected = pad;
                in.mousePos = hasMouse ? mouse : vec2(-100.f, -100.f);
                int start = 10, every = 6;
                int k = (shotFrame - start) / every;
                bool onFrame = shotFrame >= start && (shotFrame - start) % every == 0;
                std::vector<std::string> sk = shotKeys();
                const std::vector<std::string>& ks = sk.empty() ? keys : sk;
                if (onFrame && k >= 0 && k < (int)ks.size()) {
                    const std::string& key = ks[k];
                    LOG("UI test: input '%s'", key.c_str());
                    if (key == "down") in.keys[KEY_DOWN] = true;
                    else if (key == "up") in.keys[KEY_UP] = true;
                    else if (key == "left") in.keys[KEY_LEFT] = true;
                    else if (key == "right") in.keys[KEY_RIGHT] = true;
                    else if (key == "enter") in.keys[KEY_ENTER] = true;
                    else if (key == "esc") in.keys[KEY_ESCAPE] = true;
                    else if (key == "q") in.keys[KEY_Q] = true;
                    else if (key == "e") in.keys[KEY_E] = true;
                    else if (key == "x") in.keys[KEY_X] = true;
                    else if (key == "click") in.keys[KEY_MOUSE_LEFT] = true;
                    else if (key == "wheelup") in.wheelDelta = 1.f;
                    else if (key == "wheeldown") in.wheelDelta = -1.f;
                    else if (key == "pada") in.pad.buttons |= PAD_A;
                    else if (key == "padb") in.pad.buttons |= PAD_B;
                    else if (key == "padrb") in.pad.buttons |= PAD_RB;
                    else if (key == "padlb") in.pad.buttons |= PAD_LB;
                    else if (key == "paddown") in.pad.buttons |= PAD_DOWN;
                    else if (key == "padright") in.pad.buttons |= PAD_RIGHT;
                    else if (key == "back") in.keys[KEY_BACK] = true;
                    else if (key == "space") in.keys[KEY_SPACE] = true;
                    else if (key == "h") in.keys[KEY_H] = true;
                }
                prevIn = in;
            }

            // Phone test data: contacts, messages with a mission text, store / realty / jobs apps, Tidegram events
            UI::PhoneState ps;
            void fillPhone(const Render::Renderer& rr) {
                UI::PhoneState& p = ps;
                p.timeOfDay = 17.6f;
                p.day = 12;
                p.signal = 3;
                p.battery = 0.64f;
                p.owner = "Mari";
                p.money = 150700;
                vec2 pp = hs.playerPos;
                p.playerPos = vec3(pp.x, pp.y, hs.playerZ);
                p.cameraPos = vec3((float)rr.camera.pos.x, (float)rr.camera.pos.y, (float)rr.camera.pos.z);
                p.cameraYaw = rr.camera.yaw;
                p.cameraPitch = rr.camera.pitch;
                p.cameraFov = rr.camera.fovY;
                if (!p.contacts.empty()) return;
                const char* cn[7][2] = {{"Tomas", "New job: Dead Air"}, {"Mama Lucha", ""}, {"Rook", "Garage, Port Isle"}, {"Kit", "Pulse FM"},
                                        {"Jonah", "Out of town"}, {"Dex", ""}, {"Porto Sol Cabs", "Taxi"}};
                for (int i = 0; i < 7; i++) {
                    UI::PhoneContact c;
                    c.id = i + 1;
                    c.name = cn[i][0];
                    c.subtitle = cn[i][1];
                    c.mission = i == 0;
                    c.enabled = i != 4;
                    p.contacts.push_back(c);
                }
                struct M { int cid; const char* from; const char* text; const char* time; bool unread, mission, loc; const char* action; };
                const M msgs[4] = {
                    {2, "Mama Lucha", "Did you eat today? There is rice and beans in the fridge. Don't make me come find you.", "Day 11", false, false, false, ""},
                    {-1, "Wheels.ps", "Your ~b~Vapor GT~s~ has been delivered to your garage in ~y~Canvas District~s~. Drive safe!", "09:12", false, false, false, ""},
                    {3, "Rook", "Car's ready. Bring cash, not excuses.", "14:40", true, false, true, ""},
                    {1, "Tomas", "Kit at ~p~Pulse FM~s~ can crack that phone Dex grabbed. She's downtown, top floor of the ~y~Solaris~s~ tower. Don't be late.",
                     "17:31", true, true, true, "Accept job"}};
                for (int i = 0; i < 4; i++) {
                    UI::PhoneMessage m;
                    m.id = i + 1;
                    m.contactId = msgs[i].cid;
                    m.from = msgs[i].from;
                    m.text = msgs[i].text;
                    m.time = msgs[i].time;
                    m.unread = msgs[i].unread;
                    m.mission = msgs[i].mission;
                    m.hasLocation = msgs[i].loc;
                    m.location = pp + vec2(900.f, 1300.f);
                    m.actionLabel = msgs[i].action;
                    p.messages.push_back(m);
                }
                auto addApp = [&](int id, const char* name, const char* sub, UI::PhoneGlyph g, bool action) -> UI::PhoneListApp& {
                    UI::PhoneListApp a;
                    a.id = id;
                    a.name = name;
                    a.subtitle = sub;
                    a.glyph = g;
                    a.action = action;
                    p.apps.push_back(a);
                    return p.apps.back();
                };
                auto addItem = [](UI::PhoneListApp& a, int id, const char* label, const char* detail, long long price, const char* right, bool en) {
                    UI::PhoneListItem it;
                    it.id = id;
                    it.label = label;
                    it.detail = detail;
                    it.price = price;
                    it.right = right;
                    it.enabled = en;
                    a.items.push_back(it);
                };
                UI::PhoneListApp& w = addApp(1, "Wheels.ps", "Cash $150,700  |  Delivered to your garages", UI::PG_CAR, false);
                addItem(w, 1, "Vapor GT", "Sports  |  Top speed 290 km/h", 185000, "", true);
                addItem(w, 2, "Marlin Coupe", "Coupe  |  Top speed 240 km/h", 64000, "", true);
                addItem(w, 3, "Bayrunner 4x4", "SUV  |  Off-road package", 52000, "", true);
                addItem(w, 4, "Stingray Moto", "Motorcycle  |  0-100 in 3.1 s", 21500, "", true);
                addItem(w, 5, "Tidewater Van", "Van  |  Seats 8", 18000, "OWNED", false);
                UI::PhoneListApp& re = addApp(2, "Dynasty Realty", "Safehouses and businesses", UI::PG_HOUSE, false);
                addItem(re, 1, "Canvas Loft", "Safehouse: save, rest, wardrobe and garage.", -1, "OWNED", true);
                addItem(re, 2, "Lucky Palms Bar", "Business  |  Income $900 a day.", 240000, "", true);
                UI::PhoneListApp& jb = addApp(3, "Jobs", "Mark one on your map", UI::PG_BRIEFCASE, false);
                addItem(jb, 1, "Causeway Sprint", "Street race  |  Best: 1:52", -1, "", true);
                addItem(jb, 2, "Courier: Night Run", "Deliveries across Porto Sol", -1, "20:00-04:00", true);
                addApp(4, "Replay", "Finished story missions", UI::PG_REPLAY, false);
                addApp(5, "Switch to Dex", "", UI::PG_SWITCH, true);
            }
        };
        static UiTest ui;
        if (!ui.inited) ui.init(r);
        while (ui.shotDone(ui.shot)) {
            double median = 0.0;
            if (!ui.uiSamples.empty()) {
                std::vector<double> sorted = ui.uiSamples;
                std::sort(sorted.begin(), sorted.end());
                median = sorted[sorted.size() / 2];
            }
            LOG("UI test: shot '%s' done, UI cpu median %.3f ms avg %.3f ms max %.3f ms, %d verts", ui.shotNames[ui.shot].c_str(), median,
                ui.uiN ? ui.uiMs / ui.uiN : 0.0, ui.uiMaxMs, ui.verts);
            ui.shot++;
            ui.shotT = 0.f;
            ui.shotFrame = 0;
            ui.uiMs = ui.uiMaxMs = 0.0;
            ui.uiN = 0;
            ui.uiSamples.clear();
            ui.hs = UI::HudState();
            UI::hudReset();
            ui.ms.screen = UI::MENU_NONE;
            UI::MenuState fresh;
            ui.ms = fresh;
            UI::Menus::reset();
            // accessibility variants by shot name: cbp / cbd / cbt colour-blind modes, subxl large boxed subtitles
            UI::GameSettings gs;
            if (ui.has("cbp")) gs.colorblindMode = 1;
            if (ui.has("cbd")) gs.colorblindMode = 2;
            if (ui.has("cbt")) gs.colorblindMode = 3;
            if (ui.has("subxl")) {
                gs.subtitleSize = 3;
                gs.subtitleBackground = 0.6f;
            }
            ui.ms.settings = gs;
            UI::applyUiSettings(gs);
            InputState none;
            UI::Menus::update(ui.ms, none, 0.f);
        }
        // automated shots advance the UI clock faster so fades settle within the few frames before a screenshot
        float fdt = Platform::hasArg("autotest") ? 1.f / 15.f : dt;
        ui.fillHud(r);
        int v0 = UI::vertexCount();
        double t0 = TimeSeconds();
        if (ui.has("clean")) {
            // world only (backgrounds for offline UI previews)
        } else if (mode == "hud" && ui.has("icons")) {
            // icon sheet: every procedural icon and weapon silhouette with its id
            float W = (float)UI::screenWidth(), H = (float)UI::screenHeight(), s = H / 1080.f;
            UI::rect(0, 0, W, H, UI::rgba(0.05f, 0.06f, 0.12f, 0.92f));
            UI::TextStyle ts;
            ts.size = 13.f * s;
            ts.align = UI::ALIGN_CENTER;
            ts.color = UI::rgba(0.7f, 0.75f, 0.9f);
            for (int i = 0; i < UI::uix::ICO_COUNT; i++) {
                float cx = 70.f * s + (i % 16) * 112.f * s, cy = 70.f * s + (i / 16) * 110.f * s;
                u32 col = i < UI::BLIP_COUNT ? UI::uix::blipDefaultColor((UI::BlipIcon)i) : UI::rgba(1, 1, 1);
                UI::uix::drawIcon(i, cx, cy, 64.f * s, col, 1.5f * s, UI::rgba(0, 0, 0, 0.9f));
                UI::text(cx, cy + 36.f * s, StrFormat("%d", i).c_str(), ts);
            }
            for (int w = 0; w < UI::uix::kWeaponIconCount; w++) {
                float cx = 170.f * s + (w % 5) * 360.f * s, cy = 690.f * s + (w / 5) * 130.f * s;
                UI::uix::drawWeapon(w, cx, cy, 300.f * s, UI::rgba(1, 1, 1), 1.5f * s, UI::rgba(0, 0, 0, 0.9f));
                UI::text(cx, cy + 58.f * s, StrFormat("weapon %d", w).c_str(), ts);
            }
            float px = 1500.f * s, py = 60.f * s;
            const char* prompts[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "START", "BACK", "UP", "DPAD"};
            for (int k = 0; k < 14; k++) UI::uix::drawPrompt(px + (k % 4) * 90.f * s, py + (k / 4) * 60.f * s, prompts[k], true, 40.f * s);
            const char* keysKb[] = {"E", "SPACE", "ENTER", "LMB", "RMB", "WHEEL", "ESC", "TAB"};
            for (int k = 0; k < 8; k++) UI::uix::drawPrompt(px + (k % 4) * 90.f * s, py + 260.f * s + (k / 4) * 60.f * s, keysKb[k], false, 40.f * s);
        } else if (mode == "hud" && ui.has("phone")) {
            // phone scenarios: phone_<anything>_k<keys> navigates from the home screen; callin / callactive calls;
            // the scene depth feeds the photo mode depth of field
            if (ui.shotFrame == 0) {
                ui.ps = UI::PhoneState();
                UI::Phone::reset();
                ui.fillPhone(r);
                ui.ps.open = true;
                if (ui.has("callin")) {
                    ui.ps.call = UI::CALL_INCOMING;
                    ui.ps.callName = "Dex";
                    ui.ps.callContactId = 6;
                }
                if (ui.has("callactive")) {
                    ui.ps.call = UI::CALL_ACTIVE;
                    ui.ps.callName = "Tomas";
                    ui.ps.callContactId = 1;
                }
                ui.ps.photo.filter = ui.has("noir") ? 3 : (ui.has("neon") ? 1 : 2);
                ui.ps.photo.autoFocus = false;
                ui.ps.photo.focusDistance = 25.f;
                ui.ps.photo.aperture = 2.f;
                if (ui.has("polaroid")) ui.ps.photo.frame = 1;
                static bool fed = false;
                if (!fed) {
                    fed = true;
                    UI::tidegramReport(UI::TE_WANTED, ui.hs.playerPos, nullptr, 3.f);
                    UI::tidegramReport(UI::TE_CAR_STOLEN, ui.hs.playerPos, "Vapor GT", 0.f);
                    UI::tidegramReport(UI::TE_STUNT_JUMP, ui.hs.playerPos + vec2(400.f, 200.f), nullptr, 38.f);
                    for (int k = 0; k < 280; k++) UI::uix::tideTick(0.25f, 17.6f, 0, "Mari");
                }
            }
            ui.fillPhone(r);
            if (ui.ps.call == UI::CALL_ACTIVE) ui.ps.callSeconds += fdt;
            if (ui.has("pad")) ui.hs.padPrompts = true;
            UI::drawHud(ui.hs, fdt);
            InputState in;
            ui.scriptedInput(in);
            UI::setSceneDepth(r.depth.srv, r.camera.nearZ);
            UI::PhoneAction pa = UI::Phone::update(ui.ps, ui.hs, in, fdt);
            if (pa.type != UI::PA_NONE) LOG("UI test: phone action %d id %d app %d", (int)pa.type, pa.id, pa.app);
        } else if (mode == "hud" && ui.shotMenu().empty()) {
            if (ui.has("pad")) ui.hs.padPrompts = true;
            UI::drawHud(ui.hs, fdt);
        } else {
            if (ui.ms.screen == UI::MENU_NONE && ui.shotFrame == 0) {
                ui.fillMenu();
                std::string sm = ui.shotMenu();
                ui.ms.screen = !sm.empty() ? ui.screenFromName(sm) : mode == "map" ? UI::MENU_MAP : ui.screenFromName(ui.menuScreen);
                ui.pad = Platform::hasArg("menupad") || ui.has("pad");
            } else {
                UI::MenuState keep = ui.ms;
                ui.fillMenu();
                ui.ms.screen = keep.screen;
                ui.ms.settings = keep.settings;
                ui.ms.cursor = keep.cursor;
                ui.ms.tab = keep.tab;
                ui.ms.hasWaypoint = keep.hasWaypoint;
                ui.ms.waypoint = keep.waypoint;
                ui.ms.prevScreen = keep.prevScreen;
            }
            InputState in;
            ui.scriptedInput(in);
            if (ui.ms.screen != UI::MENU_NONE) {
                UI::MenuAction a = UI::Menus::update(ui.ms, in, fdt);
                if (a.type != UI::MA_NONE) LOG("UI test: menu action %d slot %d pos %.0f %.0f -> screen %d", (int)a.type, a.slot, a.pos.x, a.pos.y, (int)ui.ms.screen);
            }
        }
        double ms = (TimeSeconds() - t0) * 1000.0;
        ui.uiMs += ms;
        ui.uiMaxMs = Max(ui.uiMaxMs, ms);
        ui.uiN++;
        ui.uiSamples.push_back(ms);
        ui.verts = UI::vertexCount() - v0;
        ui.shotT += fdt;
        ui.shotFrame++;
        ui.frame++;
        if (ui.frame % 60 == 0) LOG("UI test: frame %d (shot %d, shot frame %d), frame dt %.0f ms, pending cells %d", ui.frame, ui.shot, ui.shotFrame, dt * 1000.f, r.world->pendingCount());
        if (!Platform::hasArg("uidebug")) UI::discardDrawsUntilEndFrame();
#endif
    }

    void update(Render::Renderer& r, World::WorldMap& map, float dt) {
        if (mode.empty()) return;
        t += dt;
        (void)map;
#ifdef HAVE_VEHICLE_MODELS
        for (size_t i = 0; i < vmodels.size(); i++) {
            const Vehicles::VehicleModel& vm = vmodels[i];
            vec2 p = kViewerOrigin + vec2(8.f * i, 0);
            float gz = map.heightAt(p.x, p.y);
            bool isBoat = vm.cls == Vehicles::VC_BOAT || vm.cls == Vehicles::VC_JETSKI || vm.cls == Vehicles::VC_AIRBOAT;
            Render::DrawItem d;
            d.model = bodies[i];
            d.pos = dvec3(p.x, p.y, gz + (isBoat ? 0.4f : 0.f));
            float yaw = Platform::hasArg("spin") ? t * 0.5f : 0.f;
            d.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), yaw));
            vec3 col = vm.fixedLivery ? vm.liveryPrimary : (vm.paletteColors.empty() ? vec3(0.6f) : vm.paletteColors[i % vm.paletteColors.size()]);
            d.tint0 = vec4(col, 0.1f);
            d.tint1 = vec4(vm.fixedLivery ? vm.liverySecondary : vec3(0.1f), 0);
            d.lightBits = Platform::hasArg("lights") ? (1u | 2u | 32u) : 0u;
            d.id = 0x7000 + i;
            r.dynamic->submit(d);
            // side doors: shut, or opened by -doors F (0..1; "cycle" swings them open and shut)
            for (size_t k = 0; k < vm.doors.size() && k < doorParts[i].size(); k++) {
                if (!doorParts[i][k]) continue;
                const Vehicles::DoorSpec& ds = vm.doors[k];
                const char* dv = Platform::argValue("doors");
                float open = !dv ? 0.f : (strcmp(dv, "cycle") == 0 ? 0.5f - 0.5f * cosf(t * 1.2f) : Saturate((float)atof(dv)));
                mat3 Rd = mat3FromQuat(quatAxisAngle(ds.axis, open * ds.maxAngle));
                Render::DrawItem dd = d;
                dd.model = doorParts[i][k];
                dd.rot = d.rot * Rd;
                dd.pos = d.pos + dvec3(d.rot * (ds.hinge - Rd * ds.hinge));
                dd.id = 0x7C00 + i * 8 + k;
                r.dynamic->submit(dd);
            }
            for (const auto& w : vm.wheels) {
                if (!wheels[i]) break;
                Render::DrawItem wd;
                wd.model = wheels[i];
                vec3 wp = rotate(quatAxisAngle(vec3(0, 0, 1), yaw), w.pos);
                wd.pos = dvec3(p.x + wp.x, p.y + wp.y, gz + wp.z);
                quat q = quatAxisAngle(vec3(0, 0, 1), yaw + (w.left ? kPi : 0.f));
                wd.rot = mat3FromQuat(q);
                wd.tint0 = d.tint0;
                wd.id = 0x9000 + i * 16 + (&w - &vm.wheels[0]);
                r.dynamic->submit(wd);
                if (calipers[i]) {
                    Render::DrawItem cd = wd;
                    cd.model = calipers[i];
                    cd.id = 0xB000 + i * 16 + (&w - &vm.wheels[0]);
                    r.dynamic->submit(cd);
                }
            }
            // main rotor spins about +Z; plane, boat and airboat propellers about +Y; the tail rotor about +X
            quat qy = quatAxisAngle(vec3(0, 0, 1), yaw);
            float bz = gz + (isBoat ? 0.4f : 0.f);
            if (steerWheels[i]) {
                Render::DrawItem sd;
                sd.model = steerWheels[i];
                vec3 sp = rotate(qy, vm.steerWheelPos);
                sd.pos = dvec3(p.x + sp.x, p.y + sp.y, bz + sp.z);
                vec3 za = vm.steerWheelAxis, xa(1, 0, 0);
                sd.rot = mat3FromQuat(qy) * mat3(xa, normalize(cross(za, xa)), za);
                sd.tint0 = d.tint0;
                sd.id = 0x7A00 + i;
                r.dynamic->submit(sd);
            }
            if (rotors[i]) {
                Render::DrawItem rd;
                rd.model = rotors[i];
                vec3 rp = rotate(qy, vm.rotorPos);
                rd.pos = dvec3(p.x + rp.x, p.y + rp.y, bz + rp.z);
                bool heli = vm.cls == Vehicles::VC_HELI;
                rd.rot = mat3FromQuat(qy * quatAxisAngle(heli ? vec3(0, 0, 1) : vec3(0, 1, 0), t * (heli ? 4.f : 9.f)));
                rd.tint0 = d.tint0;
                rd.id = 0x7800 + i;
                r.dynamic->submit(rd);
            }
            if (tailRotors[i]) {
                Render::DrawItem td;
                td.model = tailRotors[i];
                vec3 tp = rotate(qy, vm.tailRotorPos);
                td.pos = dvec3(p.x + tp.x, p.y + tp.y, bz + tp.z);
                td.rot = mat3FromQuat(qy * quatAxisAngle(vec3(1, 0, 0), t * 14.f));
                td.tint0 = d.tint0;
                td.id = 0x7900 + i;
                r.dynamic->submit(td);
            }
        }
#endif
#ifdef HAVE_CHARACTERS
        // --facecam: the entry of the shot the camera is on (matched by position and hour)
        const FaceCam* fc = nullptr;
        if (!faceCams.empty()) {
            for (size_t k = 0; k < shotKeys.size(); k++) {
                const ShotKey& sk = shotKeys[k];
                if (length(rel(sk.pos, r.camera.pos)) < 1e-4f && fabsf(sk.hour - r.frame.time.y) < 1e-3f) {
                    fc = &faceCams[Min(k, faceCams.size() - 1)];
                    break;
                }
            }
        }
        for (size_t i = 0; i < chars.size(); i++) {
            Ch& c = chars[i];
            if (!c.model) continue;
            if (!faceCams.empty() && (!fc || fc->index != (int)i)) continue;
            Anim::AnimInput in;
            const Anim::ClipInfo& ci = Anim::clipInfo((Anim::Clip)c.clip);
            // Locomotion clips are shown in place; others as one-shot/stance
            in.speed = ci.speed;
            if (ci.speed <= 0) in.action = c.clip;
            c.anim.update(in, dt);
            Anim::Pose pose;
            Anim::sampleClip(c.skel, (Anim::Clip)c.clip, t, pose, (u32)i);
            mat4 ms[Anim::B_COUNT];
            Anim::computeMatrices(c.skel, pose, ms, c.skin.data());
            vec2 p = kViewerOrigin + vec2(1.6f * i, 20.f);
            Render::DrawItem d;
            d.model = c.model;
            d.pos = dvec3(p.x, p.y, map.heightAt(p.x, p.y));
            d.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), Platform::hasArg("spin") ? t * 0.6f : kPi));
            d.bones = c.skin.data();
            d.boneCount = Anim::B_COUNT;
            d.id = 0xA000 + i;
            if (fc) {
                // facing the camera (model +Y towards it), turned by yawDeg; the eye midpoint on the camera axis
                vec3 fwd = r.camera.forward();
                vec3 flat = normalize(vec3(fwd.x, fwd.y, 0.f));
                float th = atan2f(flat.x, -flat.y) + fc->yawDeg * kDegToRad;
                d.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), th));
                vec3 eyes = (ms[Anim::B_EYE_L].c[3].xyz() + ms[Anim::B_EYE_R].c[3].xyz()) * 0.5f;
                dvec3 target = r.camera.pos + dvec3(fwd * fc->dist);
                d.pos = target - dvec3(d.rot * eyes);
                if (fc->dist >= 2.5f) d.pos.z = map.heightAt((float)d.pos.x, (float)d.pos.y);
                if (lampKind >= 0) {
                    vec3 toCam = -flat, side = normalize(cross(vec3(0, 0, 1), toCam));
                    Render::DynamicLight lamp;
                    double gz = map.heightAt((float)target.x, (float)target.y);
                    lamp.pos = dvec3(target.x, target.y, gz) + dvec3(toCam * 3.f + side * 1.2f + vec3(0, 0, 8.6f));
                    lamp.color = (lampKind == 1 ? vec3(0.85f, 0.9f, 1.f) : vec3(1.f, 0.72f, 0.42f)) * 7000.f;
                    lamp.radius = 30.f;
                    lamp.dir = vec3(0, 0, -1);
                    lamp.spotCos = 0.2f;
                    lamp.spotInner = 0.45f;
                    r.addLight(lamp);
                }
            }
            r.dynamic->submit(d);
        }
#endif
    }
};

}  // namespace Game
