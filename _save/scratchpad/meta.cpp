#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
#include <cstdio>
int main(int argc, char** argv) {
    int n = Vehicles::modelCount();
    for (int i = 0; i < n; i++) {
        Vehicles::VehicleModel m;
        Vehicles::buildModel(i, m);
        AABB bb = m.body.bounds;
        printf("[%2d] %-18s cls=%2d mass=%5.0f P=%4.0fkW T=%4.0f rpm=%5.0f top=%4.1f(%3.0fkmh) g=%d dF=%.2f brk=%5.0f grip=%.2f trav=%.2f stiff=%.2f cd=%.2f A=%.2f df=%.2f eng=%d siren=%d\n",
               i, m.name.c_str(), (int)m.cls, m.mass, m.power, m.torque, m.maxRpm, m.topSpeed, m.topSpeed * 3.6f, m.gears, m.driveFront, m.brakeForce, m.grip,
               m.suspensionTravel, m.suspensionStiffness, m.dragCoef, m.frontalArea, m.downforce, m.engineSound, m.sirenMode);
        printf("      bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f)  box c(%.2f %.2f %.2f) h(%.2f %.2f %.2f) => (%.2f %.2f %.2f)-(%.2f %.2f %.2f) com(%.2f %.2f %.2f)\n",
               bb.mn.x, bb.mn.y, bb.mn.z, bb.mx.x, bb.mx.y, bb.mx.z, m.boxCenter.x, m.boxCenter.y, m.boxCenter.z, m.boxHalf.x, m.boxHalf.y, m.boxHalf.z,
               m.boxCenter.x - m.boxHalf.x, m.boxCenter.y - m.boxHalf.y, m.boxCenter.z - m.boxHalf.z, m.boxCenter.x + m.boxHalf.x, m.boxCenter.y + m.boxHalf.y,
               m.boxCenter.z + m.boxHalf.z, m.centerOfMass.x, m.centerOfMass.y, m.centerOfMass.z);
        printf("      wheels %zu:", m.wheels.size());
        for (auto& w : m.wheels) printf(" (%.2f %.2f %.2f r%.2f w%.2f %s%s%s)", w.pos.x, w.pos.y, w.pos.z, w.radius, w.width, w.steer ? "S" : "", w.drive ? "D" : "", w.left ? "L" : "");
        printf("\n      seats %zu:", m.seats.size());
        for (auto& s : m.seats) printf(" (%.2f %.2f %.2f %s%s)", s.pos.x, s.pos.y, s.pos.z, s.driver ? "drv" : "", s.exitLeft ? "<" : ">");
        int lc[16] = {};
        for (auto& l : m.lights) lc[(int)l.type & 15]++;
        printf("\n      lights %zu:", m.lights.size());
        for (int k = 0; k < 16; k++) if (lc[k]) printf(" t%d x%d", k, lc[k]);
        printf("  floats %zu wing %.1f lift %.1f rotorR %.2f rotorPos(%.2f %.2f %.2f) tailPos(%.2f %.2f %.2f) palette %zu fixed=%d prim(%.3f %.3f %.3f) sec(%.3f %.3f %.3f) spawn %.2f price %d\n",
               m.floatPoints.size(), m.wingArea, m.liftSlope, m.rotorRadius, m.rotorPos.x, m.rotorPos.y, m.rotorPos.z, m.tailRotorPos.x, m.tailRotorPos.y, m.tailRotorPos.z,
               m.paletteColors.size(), (int)m.fixedLivery, m.liveryPrimary.x, m.liveryPrimary.y, m.liveryPrimary.z, m.liverySecondary.x, m.liverySecondary.y,
               m.liverySecondary.z, m.spawnWeight, m.price);
        if (!m.floatPoints.empty()) {
            printf("      floatPts:");
            for (auto& f : m.floatPoints) printf(" (%.2f %.2f %.2f)", f.x, f.y, f.z);
            printf("\n");
        }
    }
}
