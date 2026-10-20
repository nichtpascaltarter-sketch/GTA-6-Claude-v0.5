#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
#include <cstdio>
int main(int argc, char** argv) {
    int n = Vehicles::modelCount();
    int a = 0, b = n - 1;
    if (argc > 1) { a = b = atoi(argv[1]); }
    long total = 0;
    for (int i = a; i <= b; i++) {
        Vehicles::VehicleModel vm;
        Vehicles::buildModel(i, vm);
        size_t bt = vm.body.indices.size() / 3, wt = vm.wheel.indices.size() / 3;
        printf("[%2d] %-22s %-18s cls=%2d body=%6zu wheel=%5zu rotor=%4zu tail=%4zu verts=%6zu\n", i, vm.maker.c_str(), vm.name.c_str(), (int)vm.cls, bt, wt,
               vm.rotor.indices.size() / 3, vm.tailRotor.indices.size() / 3, vm.body.verts.size());
        total += bt;
    }
    printf("total body tris %ld\n", total);
}
