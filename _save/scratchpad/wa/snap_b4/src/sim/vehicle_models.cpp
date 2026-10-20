// Procedural vehicle models (unity build entry for the vehicle module). Every vehicle is generated from code:
// lofted bodies, projected details, lathed wheels, and physically plausible metadata.
#include "vehicle_models.h"
#include "../core/rng.h"
#include "../audio/audio.h"
#include "vehicle_geom.cpp"
#include "vehicle_wheels.cpp"
#include "vehicle_carbody.cpp"
#include "vehicle_cardetail.cpp"
#include "vehicle_badges.cpp"
#include "vehicle_cars.cpp"
#include "vehicle_heavy.cpp"
#include "vehicle_bikes.cpp"
#include "vehicle_marine.cpp"
#include "vehicle_air.cpp"
#include "vehicle_catalog.cpp"

namespace Vehicles {

int modelCount() { return (int)ARRAY_COUNT(detail::kModels); }

void buildModel(int index, VehicleModel& out) {
    out = VehicleModel();
    if (index < 0 || index >= modelCount()) return;
    detail::kModels[index].fn(out);
}

int findModel(VehicleClass cls, int n) {
    int c = modelCount();
    for (int i = 0; i < c; i++)
        if (detail::kModels[i].cls == cls && n-- == 0) return i;
    return -1;
}

namespace detail {
inline void appendMesh(MeshData& dst, const MeshData& src) {
    u32 base = (u32)dst.verts.size();
    for (const VtxStatic& v : src.verts) {
        dst.verts.push_back(v);
        dst.bounds.add(v.pos);
    }
    for (u32 i : src.indices) dst.indices.push_back(base + i);
}
// Far-LOD wheels: 10-sided tire band with a light rim disc on the outboard face and a dark inner disc.
inline void wheelCylinders(PMesh& m, const std::vector<WheelSpec>& wheels) {
    const int seg = 8;
    for (const WheelSpec& w : wheels) {
        float s = w.left ? -1.f : 1.f;  // outboard direction
        vec3 c = w.pos;
        float hw = w.width * 0.5f;
        m.newGroup(50.f);
        m.use(MAT_TIRE, kCol1);
        for (int k = 0; k < seg; k++) {
            float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
            vec3 r0(0, cosf(a0), sinf(a0)), r1(0, cosf(a1), sinf(a1));
            u32 p0 = m.add(c + r0 * w.radius + vec3(-hw, 0, 0)), p1 = m.add(c + r1 * w.radius + vec3(-hw, 0, 0));
            u32 p2 = m.add(c + r1 * w.radius + vec3(hw, 0, 0)), p3 = m.add(c + r0 * w.radius + vec3(hw, 0, 0));
            m.quadFacing(p0, p1, p2, p3, r0 + r1);
        }
        // outboard face: sidewall ring + rim; the inboard face is a plain dark disk (both dressed on centre-line bike
        // wheels, which are seen from either side)
        bool centre = fabsf(c.x) < 0.1f;
        for (int side = 0; side < 2; side++) {
            float d = side == 0 ? s : -s;
            m.newGroup(30.f);
            m.use(MAT_TIRE, kCol1);
            if (side == 0 || centre) {
                disk(m, c + vec3(d * hw, 0, 0), vec3(d, 0, 0), w.radius, seg, w.radius * 0.64f);
                m.use(MAT_RIM, col(0.62f, 0.62f, 0.64f));
                disk(m, c + vec3(d * (hw + 0.002f), 0, 0), vec3(d, 0, 0), w.radius * 0.64f, seg);
            } else {
                disk(m, c + vec3(d * hw, 0, 0), vec3(d, 0, 0), w.radius, seg);
            }
        }
    }
}
}  // namespace detail

void buildVehicleLods(int index, MeshData lods[2], MeshData* wheelLod1) {
    lods[0].clear();
    lods[1].clear();
    if (wheelLod1) wheelLod1->clear();
    if (index < 0 || index >= modelCount()) return;
    int& level = detail::lodLevel();
    int saved = level;
    {
        VehicleModel a;
        level = 1;
        detail::kModels[index].fn(a);
        lods[0] = std::move(a.body);
        if (wheelLod1) *wheelLod1 = std::move(a.wheel);
    }
    {
        VehicleModel b;
        level = 2;
        detail::kModels[index].fn(b);
        lods[1] = std::move(b.body);
        level = 0;
        if (!b.wheels.empty()) {
            detail::PMesh pm;
            detail::wheelCylinders(pm, b.wheels);
            MeshData wm;
            detail::finalizeMesh(pm, wm);
            detail::appendMesh(lods[1], wm);
        }
    }
    level = saved;
}

}  // namespace Vehicles
