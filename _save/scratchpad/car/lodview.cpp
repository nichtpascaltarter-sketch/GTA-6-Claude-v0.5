// scratch: a vehicle's far LOD bodies (LOD1 / LOD2) from the side, around the rear side window's slanted edge
#define main carview_main
#include "tests/anim/carview.cpp"
#undef main
int main(int argc, char** argv) {
    int model = atoi(argv[1]), lod = atoi(argv[2]);
    const char* out = argv[3];
    float ty = (float)atof(argv[4]), tz = (float)atof(argv[5]), tx = (float)atof(argv[6]);
    MeshData lods[2];
    buildVehicleLods(model, lods);
    Geo g;
    addMesh(g, lods[lod - 1], quat(), vec3(0.f), vec3(0.f));
    vec3 ctr(tx, ty, tz);
    float yw = -90.f * kDegToRad, pa = 3.f * kDegToRad, R = 2.2f;
    vec3 dir(sinf(yw) * cosf(pa), cosf(yw) * cosf(pa), sinf(pa));
    Cam cam;
    cam.eye = ctr + dir * R;
    cam.target = ctr;
    cam.fov = 40.f;
    gNear = 0.05f;
    Img img(840, 600);
    cam.setup(img.w, img.h);
    drawMesh(img, cam, g.P, g.N, g.A, g.M, g.I);
    drawGlass(img, cam, g.GP);
    img.down(2).save(out);
    return 0;
}
