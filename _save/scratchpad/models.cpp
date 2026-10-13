#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "core/math.cpp"
#include "core/noise.cpp"
#include "render/mesh.cpp"
#include "sim/vehicle_models.cpp"
int main() {
    int n = Vehicles::modelCount();
    for (int i = 0; i < n; i++) {
        Vehicles::VehicleModel m; Vehicles::buildModel(i, m);
        float wb = m.wheels.size() >= 4 ? m.wheels[0].pos.y - m.wheels[2].pos.y : 0;
        printf("%2d %-16s cls %2d mass %5.0f kW %4.0f top %4.1f box c(%.2f %.2f %.2f) h(%.2f %.2f %.2f) wb %.2f seats %zu siren %d w %.2f\n", i, m.name.c_str(), m.cls, m.mass, m.power, m.topSpeed,
            m.boxCenter.x, m.boxCenter.y, m.boxCenter.z, m.boxHalf.x, m.boxHalf.y, m.boxHalf.z, wb, m.seats.size(), m.sirenMode, m.spawnWeight);
    }
}
