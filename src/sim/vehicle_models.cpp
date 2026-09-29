// Procedural vehicle models (unity build entry for the vehicle module). Every vehicle is generated from code:
// lofted bodies, projected details, lathed wheels, and physically plausible metadata.
#include "vehicle_models.h"
#include "../core/rng.h"
#include "../audio/audio.h"
#include "vehicle_geom.cpp"
#include "vehicle_wheels.cpp"
#include "vehicle_carbody.cpp"
#include "vehicle_cardetail.cpp"
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

}  // namespace Vehicles
