#define PREVIEW_NO_MAIN
#include "/home/user/GTA-6-Claude-v0.5/tests/anim/preview.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/sim/vehicle_models.cpp"
using namespace Vehicles;
int main() {
    for (int mi = 0; mi < 27; mi++) {
        VehicleModel vm;
        buildModel(mi, vm);
        for (size_t si = 0; si < vm.seats.size(); si++) {
            const SeatSpec& ss = vm.seats[si];
            if (si != 0 && si != 2) continue;
            printf("%-16s seat %zu hip z %.3f floor %.3f (hip over floor %.3f) headZ %.3f room %.3f\n", vm.name.c_str(), si, ss.pos.z, ss.floorZ,
                   ss.pos.z - ss.floorZ, ss.headZ, ss.headZ - ss.pos.z);
        }
    }
}
