// Emulates the unity-build context: Windows headers (with their macros) before the vehicle models.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <xinput.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <thread>
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
int main() {
    int n = Vehicles::modelCount();
    for (int i = 0; i < n; i++) { Vehicles::VehicleModel m; Vehicles::buildModel(i, m); }
    return Vehicles::findModel(Vehicles::VC_HELI, 1) > 0 ? 0 : 1;
}
