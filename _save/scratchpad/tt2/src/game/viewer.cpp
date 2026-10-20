// Model viewer mode for content iteration: `--viewer vehicles` or `--viewer characters`.
// Vehicles are lined up along +X starting at kViewerOrigin, spaced 8 m apart, facing +Y (north).
// Characters are lined up 1.6 m apart at kViewerOrigin + (0, 20), each playing a different clip.
// Use --shot to place the camera, e.g. to look at vehicle i from the front-left:
//   --shot "X,Y,Z,yawDeg,pitchDeg,hour,name" with X = -300 + 8*i - 4, Y = 1500 + 6, Z = height + 1.6
namespace Game {

static const vec2 kViewerOrigin(-300.f, 1500.f);

struct Viewer {
    std::string mode;
    std::vector<Render::Model*> bodies, wheels, rotors;
#ifdef HAVE_VEHICLE_MODELS
    std::vector<Vehicles::VehicleModel> vmodels;
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
            for (int i = 0; i < n; i++) {
                Vehicles::VehicleModel vm;
                Vehicles::buildModel(i, vm);
                bodies.push_back(r.dynamic->createModel(vm.body));
                wheels.push_back(r.dynamic->createModel(vm.wheel));
                rotors.push_back(vm.rotor.indices.empty() ? nullptr : r.dynamic->createModel(vm.rotor));
                LOG("Vehicle %d: %s %s (%zu tris)", i, vm.maker.c_str(), vm.name.c_str(), vm.body.indices.size() / 3);
                vmodels.push_back(std::move(vm));
            }
        }
#endif
#ifdef HAVE_CHARACTERS
        if (mode == "characters") {
            int n = 16;
            if (const char* c = Platform::argValue("count")) n = atoi(c);
            chars.resize(n);
            for (int i = 0; i < n; i++) {
                Ch& c = chars[i];
                c.desc = Anim::randomCharacter(1000 + i * 7919, i % 7);
                Anim::buildSkeleton(c.desc, c.skel);
                SkinnedMeshData mesh;
                Anim::buildCharacterMesh(c.desc, c.skel, mesh);
                c.model = r.dynamic->createSkinnedModel(mesh);
                c.anim.init(&c.skel, (u32)i);
                c.clip = i % Anim::CLIP_COUNT;
                if (const char* cl = Platform::argValue("clip")) c.clip = atoi(cl);
                c.skin.resize(Anim::B_COUNT);
            }
        }
#endif
    }

    // 2D overlay for UI test modes (called between UI::beginFrame/endFrame after the world is rendered).
    void drawOverlay(Render::Renderer& r, float dt) {
        (void)r;
        (void)dt;
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
            for (const auto& w : vm.wheels) {
                Render::DrawItem wd;
                wd.model = wheels[i];
                vec3 wp = rotate(quatAxisAngle(vec3(0, 0, 1), yaw), w.pos);
                wd.pos = dvec3(p.x + wp.x, p.y + wp.y, gz + wp.z);
                quat q = quatAxisAngle(vec3(0, 0, 1), yaw + (w.left ? kPi : 0.f));
                wd.rot = mat3FromQuat(q);
                wd.tint0 = d.tint0;
                wd.id = 0x9000 + i * 16 + (&w - &vm.wheels[0]);
                r.dynamic->submit(wd);
            }
            if (rotors[i]) {
                Render::DrawItem rd;
                rd.model = rotors[i];
                vec3 rp = vm.rotorPos;
                rd.pos = dvec3(p.x + rp.x, p.y + rp.y, gz + rp.z);
                rd.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), t * 3.f));
                rd.tint0 = d.tint0;
                r.dynamic->submit(rd);
            }
        }
#endif
#ifdef HAVE_CHARACTERS
        for (size_t i = 0; i < chars.size(); i++) {
            Ch& c = chars[i];
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
            r.dynamic->submit(d);
        }
#endif
    }
};

}  // namespace Game
