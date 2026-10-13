// Per-cell dispatch of site element generators (called from World::generateCell on streaming worker threads).
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"
// Hand-built places (places.h): included here, after the site toolkit and the other site generators, so every build of
// the site cells (the game, worldcheck, traffic_sim) compiles them
#include "placekit.cpp"
#include "decostrip.cpp"
#include "campus.cpp"
#include "cemetery.cpp"
#include "churchyard.cpp"
#include "hospital.cpp"
#include "outskirts.cpp"
#include "places.cpp"

namespace World {

const std::vector<int>& siteFarCells(float* range) {
    static const std::vector<int> none;
    if (!gSites || !gSites->generated) {
        *range = 0.f;
        return none;
    }
    *range = gSites->farRange;
    return gSites->farCells;
}

void buildSiteCell(int cx, int cy, bool detail, CellGeometry& out) {
    if (!gSites || gSites->cellElems.empty()) return;
    sitegeo::G g;
    g.m = &out.opaque;
    g.d = &out.decals;
    g.org = vec3(cellOrigin(cx, cy), 0.f);
    g.detail = detail;
    g.cx = cx;
    g.cy = cy;
    g.col = detail ? &out.collision : nullptr;
    g.props = detail ? &out.props : nullptr;
    g.lights = detail ? &out.lights : nullptr;
    sitegeo::drawPads(g);
    const std::vector<int>& list = gSites->cellElems[(size_t)cy * kCellsPerSide + cx];
    for (int ei : list) {
        const SiteElem& e = gSites->elems[ei];
        switch (e.kind) {
            // airport
            case SK_RUNWAY: airport_mesh::genRunway(e, g); break;
            case SK_TAXI_MARKS: airport_mesh::genTaxiMarks(e, g); break;
            case SK_APRON_MARKS: airport_mesh::genApronMarks(e, g); break;
            case SK_TERMINAL: airport_mesh::genTerminal(e, g); break;
            case SK_CONCOURSE: airport_mesh::genConcourse(e, g); break;
            case SK_CONTROL_TOWER: airport_mesh::genControlTower(e, g); break;
            case SK_HANGAR: airport_mesh::genHangar(e, g); break;
            case SK_FUEL_FARM: airport_mesh::genFuelFarm(e, g); break;
            case SK_AIRLINER: airport_mesh::genAirliner(e, g); break;
            case SK_SMALL_PLANE: airport_mesh::genSmallPlane(e, g); break;
            case SK_HELICOPTER: airport_mesh::genHelicopter(e, g); break;
            case SK_HELIPAD: airport_mesh::genHelipad(e, g); break;
            case SK_FENCE: airport_mesh::genFence(e, g); break;
            case SK_APPROACH_LIGHTS: airport_mesh::genApproachLights(e, g); break;
            case SK_FLOOD_MAST: airport_mesh::genFloodMast(e, g); break;
            case SK_WINDSOCK: airport_mesh::genWindsock(e, g); break;
            case SK_FIRE_STATION: airport_mesh::genFireStation(e, g); break;
            case SK_GSE: airport_mesh::genGse(e, g); break;
            case SK_AIRPORT_SIGN: airport_mesh::genAirportSign(e, g); break;
            case SK_PARKING_MARKS: airport_mesh::genParkingMarks(e, g); break;
            case SK_GARAGE_RAMP: airport_mesh::genGarageRamp(e, g); break;
            case SK_FORECOURT: airport_mesh::genForecourt(e, g); break;
            // port
            case SK_QUAY: port_mesh::genQuay(e, g); break;
            case SK_CONTAINER_BLOCK: port_mesh::genContainerBlock(e, g); break;
            case SK_STS_CRANE: port_mesh::genStsCrane(e, g); break;
            case SK_STRADDLE: port_mesh::genStraddle(e, g); break;
            case SK_SHIP: port_mesh::genShip(e, g); break;
            case SK_RAIL: port_mesh::genRail(e, g); break;
            case SK_RAIL_YARD: port_mesh::genRailYard(e, g); break;
            case SK_PORT_GATE: port_mesh::genPortGate(e, g); break;
            case SK_YARD_MAST: airport_mesh::genFloodMast(e, g); break;
            case SK_RMG_CRANE: port_mesh::genRmgCrane(e, g); break;
            case SK_PORT_TRUCK: port_mesh::genPortTruck(e, g); break;
            case SK_REACH_STACKER: port_mesh::genReachStacker(e, g); break;
            case SK_PORT_DRESS: port_mesh::genPortDress(e, g); break;
            // Key Coral, marinas, beaches
            case SK_MARINA: leisure_mesh::genMarina(e, g); break;
            case SK_GOLF_HOLE: leisure_mesh::genGolfHole(e, g); break;
            case SK_GOLF_POND: leisure_mesh::genGolfPond(e, g); break;
            case SK_BEACH_CLUB: leisure_mesh::genBeachClub(e, g); break;
            case SK_BEACH_ACCESS: leisure_mesh::genBeachAccess(e, g); break;
            case SK_DOCK: leisure_mesh::genDock(e, g); break;
            case SK_RIVER_MARINA: leisure_mesh::genRiverMarina(e, g); break;
            // landmarks
            case SK_SOLARIS: landmark_mesh::genSolaris(e, g); break;
            case SK_STADIUM: landmark_mesh::genStadium(e, g); break;
            case SK_CITY_HALL: landmark_mesh::genCityHall(e, g); break;
            case SK_PARK: landmark_mesh::genPark(e, g); break;
            case SK_FOUNTAIN: landmark_mesh::genFountain(e, g); break;
            case SK_BILLBOARD: landmark_mesh::genBillboard(e, g); break;
            case SK_BEACH_PIER: leisure_mesh::genBeachPier(e, g); break;
            case SK_FERRIS_WHEEL: leisure_mesh::genFerrisWheel(e, g); break;
            case SK_COASTER: leisure_mesh::genCoaster(e, g); break;
            case SK_DROP_TOWER: leisure_mesh::genDropTower(e, g); break;
            case SK_CAROUSEL: leisure_mesh::genCarousel(e, g); break;
            case SK_SWING_RIDE: leisure_mesh::genSwingRide(e, g); break;
            case SK_PIER_GAMES: leisure_mesh::genPierGames(e, g); break;
            // public transit
            case SK_METRO_VIADUCT: transit_mesh::genViaduct(e, g); break;
            case SK_METRO_STATION: transit_mesh::genStation(e, g); break;
            case SK_BUS_STOP: transit_mesh::genBusStop(e, g); break;
            case SK_FERRY_PIER: transit_mesh::genFerryPier(e, g); break;
            case SK_TRAM_TRACK: transit_mesh::genTramTrack(e, g); break;
            case SK_TRAM_STOP: transit_mesh::genTramStop(e, g); break;
            case SK_LIGHTHOUSE: rural_mesh::genLighthouse(e, g); break;
            case SK_RADIO_MAST: rural_mesh::genRadioMast(e, g); break;
            case SK_SUGAR_MILL: rural_mesh::genSugarMill(e, g); break;
            case SK_SILO: rural_mesh::genSilo(e, g); break;
            case SK_WINDMILL: rural_mesh::genWindmill(e, g); break;
            case SK_BOARDWALK: rural_mesh::genBoardwalk(e, g); break;
            case SK_OBS_TOWER: rural_mesh::genObsTower(e, g); break;
            case SK_BOAT_RAMP: rural_mesh::genBoatRamp(e, g); break;
            // hand-built places
            case SK_DECO_HOTEL: deco_strip::genHotel(e, g); break;
            case SK_DECO_PARK: deco_strip::genDecoPark(e, g); break;
            case SK_CAMPUS_GROUNDS: campus::genGrounds(e, g); break;
            case SK_CAMPUS_HALL: campus::genHall(e, g); break;
            case SK_CAMPUS_TOWER: campus::genTower(e, g); break;
            case SK_CAMPUS_FIELD: campus::genField(e, g); break;
            case SK_CEMETERY: (e.variant == 4 ? cemetery::genGrounds : cemetery::genSection)(e, g); break;
            case SK_CEMETERY_WALL: cemetery::genWall(e, g); break;
            case SK_CHAPEL: cemetery::genChapel(e, g); break;
            case SK_CHURCHYARD: churchyard::genChurchyard(e, g); break;
            case SK_HOSPITAL: hospital::genBlock(e, g); break;
            case SK_HOSPITAL_GROUNDS: hospital::genGrounds(e, g); break;
            case SK_PRISON:
                if (e.variant <= 3) outskirts::genPrisonBlock(e, g);
                else if (e.variant == 4) outskirts::genSallyPort(e, g);
                else if (e.variant == 5) outskirts::genPrisonGrounds(e, g);
                else outskirts::guardTower(g, e.c, e.z, e.ax);
                break;
            case SK_PRISON_WALL: outskirts::genPrisonWall(e, g); break;
            case SK_SPEEDWAY: outskirts::genSpeedway(e, g); break;
            default: break;
        }
    }
}

}  // namespace World
