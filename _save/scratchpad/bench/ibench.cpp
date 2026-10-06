// Native benchmark: generate the world, plan interiors, build each one and report CPU time / triangles.
#define R_ "/home/user/GTA-6-Claude-v0.5/"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roadmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/propmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/cellgen.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitegeo.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/airport.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/port.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/landmarks.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/leisure.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/rural.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transitmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitecell.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/facadedetail.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorkit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorfurniture.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorlayouts.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorhomes.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorvenues.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorshops.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorcivic.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorindustrial.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiortower.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorgarages.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorresidences.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiors.cpp"
#include <thread>
#include <time.h>
#include <unistd.h>
static double cpuNow() { timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    double t0 = TimeSeconds();
    World::WorldMap m;
    m.generate();
    World::gMap = &m;
    World::RoadNetwork roads;
    roads.generate(m);
    World::gRoads = &roads;
    World::BuildingSet bset;
    bset.generate(m, roads);
    World::gBuildings = &bset;
    printf("world %.1f s\n", TimeSeconds() - t0);
    if (!World::gInteriors) { printf("no interiors\n"); return 1; }
    if (argc > 2 && !strcmp(argv[1], "probe")) {
        static const char* kStyle[] = {"tower", "midrise", "condo", "deco", "shops", "stripmall", "house", "villa", "warehouse", "factory", "farmhouse",
                                       "barn", "motel", "gas", "garage", "church", "shack"};
        for (int a = 2; a + 2 < argc; a += 3) {
            vec2 h((float)atof(argv[a]), (float)atof(argv[a + 1]));
            float rad = (float)atof(argv[a + 2]);
            printf("== near (%.0f, %.0f) r %.0f\n", h.x, h.y, rad);
            for (size_t i = 0; i < bset.buildings.size(); i++) {
                const World::Building& b = bset.buildings[i];
                if (length(b.c - h) > rad) continue;
                const World::FacadeGPU& f = bset.facades[b.facade];
                printf("  b%zu %-9s c (%.1f, %.1f) %.1f x %.1f h %.1f fl %d front (%.2f, %.2f) gH %.2f bayW %.2f store %d style %d z %.1f int %d dist %.0f\n", i,
                       kStyle[b.style], b.c.x, b.c.y, 2 * b.hx, 2 * b.hy, b.height, b.floors, b.front.x, b.front.y, f.groundH, f.bayW, (int)(f.flags & 1u),
                       (int)f.style, b.baseZ, b.interior, length(b.c - h));
            }
        }
        fflush(stdout);
        _exit(0);
    }
    if (argc > 2 && !strcmp(argv[1], "open")) {
        int i = World::gInteriors->byName(argv[2]);
        if (i < 0) { printf("no interior %s\n", argv[2]); _exit(1); }
        const World::InteriorDef& d = World::gInteriors->defs[i];
        if (d.building >= 0) {
            const World::Building& b = bset.buildings[d.building];
            const World::FacadeGPU& f = bset.facades[b.facade];
            printf("building %d style %d hx %.2f hy %.2f floors %d gH %.2f bayW %.2f flags %u style %.0f bays %d bw %.3f bayX0 %.2f doorBay %d x0 %.2f x1 %.2f\n", d.building, b.style, b.hx, b.hy,
                   b.floors, f.groundH, f.bayW, f.flags, f.style, d.bays, d.bw, d.bayX0, d.doorBay, d.x0, d.x1);
        }
        printf("origin (%.2f %.2f %.2f) ax (%.4f %.4f) x %.2f..%.2f depth %.2f ceil %.2f link %d markers %zu scenarios %zu portals %zu\n", d.origin.x, d.origin.y, d.origin.z,
               d.ax.x, d.ax.y, d.x0, d.x1, d.depth, d.ceil, d.link, d.markers.size(), d.scenarios.size(), d.portals.size());
        for (auto& mk : d.markers) printf("  marker %d at (%.2f %.2f %.2f) yaw %.2f\n", mk.kind, mk.pos.x, mk.pos.y, mk.pos.z, mk.yaw);
        for (auto& op : d.openings) {
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            printf("  opening kind %d x %.2f..%.2f y %.2f z %.2f..%.2f\n", op.kind, a.x, c.x, a.y, op.z0 - d.origin.z, op.z1 - d.origin.z);
        }
        for (auto& dr : d.doors) printf("  door kind %d c (%.2f %.2f %.2f) w %.2f h %.2f ext %d\n", dr.kind, dr.c.x, dr.c.y, dr.c.z, dr.w, dr.h, (int)dr.exterior);
        for (auto& r : d.rooms) printf("  room (%.2f %.2f %.2f)-(%.2f %.2f %.2f) out %d\n", r.mn.x, r.mn.y, r.mn.z, r.mx.x, r.mx.y, r.mx.z, (int)r.outdoor);
        fflush(stdout);
        _exit(0);
    }
    if (argc > 2 && !strcmp(argv[1], "colprobe")) {
        // collision boxes (building cell + interior) crossing the lane from the street to the service lift
        int i = World::gInteriors->byName(argv[2]);
        if (i < 0) { printf("no interior %s\n", argv[2]); _exit(1); }
        const World::InteriorDef& d = World::gInteriors->defs[i];
        const World::InteriorMarker* mk = d.marker(World::IM_SERVICE);
        vec3 lift = mk ? mk->pos : vec3(0.f, 5.f, 0.f);
        printf("lift local (%.2f %.2f) x0 %.2f x1 %.2f depth %.2f floor z %.2f\n", lift.x, lift.y, d.x0, d.x1, d.depth, d.origin.z);
        for (const auto& op : d.openings) {
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            printf("  opening kind %d x %.2f..%.2f y %.2f z %.2f..%.2f\n", op.kind, a.x, c.x, a.y, op.z0 - d.origin.z, op.z1 - d.origin.z);
        }
        auto test = [&](const char* what, const std::vector<World::CollisionBox>& boxes) {
            for (const auto& cb : boxes) {
                // sample the lane: x = lift.x +- 0.95, y from -4 to lift.y + 2.4, z 0.3..1.3 (car body)
                bool hit = false;
                for (float y = -4.f; y <= lift.y + 2.4f && !hit; y += 0.25f)
                    for (float x = lift.x - 0.95f; x <= lift.x + 0.96f && !hit; x += 0.19f)
                        for (float z = 0.35f; z <= 1.3f && !hit; z += 0.3f) {
                            vec3 w = d.toWorld(vec3(x, y, z));
                            vec3 q = w - cb.c;
                            vec2 ay(-cb.ax.y, cb.ax.x);
                            if (fabsf(dot(q.xy(), cb.ax)) < cb.he.x && fabsf(dot(q.xy(), ay)) < cb.he.y && fabsf(q.z) < cb.he.z) hit = true;
                        }
                if (!hit) continue;
                vec3 lc = d.toLocal(cb.c);
                printf("  %s box local c (%.2f %.2f %.2f) he (%.2f %.2f %.2f) ax (%.2f %.2f)\n", what, lc.x, lc.y, lc.z, cb.he.x, cb.he.y, cb.he.z, cb.ax.x, cb.ax.y);
            }
        };
        vec3 o = d.origin;
        int cx = (int)floorf((o.x + World::kWorldHalf) / World::kCellSize), cy = (int)floorf((o.y + World::kWorldHalf) / World::kCellSize);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                World::CellGeometry geo;
                World::generateCell(cx + dx, cy + dy, true, geo);
                test("cell", geo.collision);
            }
        World::InteriorMesh im;
        World::buildInterior(i, im);
        test("interior", im.col);
        fflush(stdout);
        _exit(0);
    }
    int reps = argc > 1 ? atoi(argv[1]) : 3;
    for (int i = 0; i < (int)World::gInteriors->defs.size(); i++) {
        double best = 1e9, wall = 1e9;
        World::InteriorMesh out;
        for (int r = 0; r < reps; r++) {
            World::InteriorMesh o;
            double c0 = cpuNow(), w0 = TimeSeconds();
            World::buildInterior(i, o);
            best = Min(best, (cpuNow() - c0) * 1000.0);
            wall = Min(wall, (TimeSeconds() - w0) * 1000.0);
            if (r == reps - 1) out = std::move(o);
        }
        int pt[World::IP_COUNT];
        for (int k = 0; k < World::IP_COUNT; k++) pt[k] = (int)out.parts[k].indices.size() / 3;
        size_t verts = 0;
        for (int k = 0; k < World::IP_COUNT; k++) verts += out.parts[k].verts.size();
        printf("%-24s kind %2d: %7d tris (shell %d furn %d detail %d leaves %zu) %zu verts, %zu lights, %zu cols: cpu %.2f ms wall %.2f ms\n",
               World::gInteriors->defs[i].name.c_str(), World::gInteriors->defs[i].kind, out.triangles, pt[0], pt[1], pt[2], out.leaves.size(), verts,
               out.lights.size(), out.col.size(), best, wall);
    }
    fflush(stdout);
    _exit(0);
}
