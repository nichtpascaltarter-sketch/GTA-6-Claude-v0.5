// Pre-integrated subsurface scattering for skin (Penner & Borshukov, "Pre-Integrated Skin Shading", GPU Pro 2): the
// diffuse falloff of a curved surface lit at an angle theta, with the light scattered under the skin along its
// diffusion profile, so red light (which travels furthest) softens and warms the terminator and the shadow edges.
// The profile is the three-layer skin fit of d'Eon & Luebke (GPU Gems 3, chapter 14): six Gaussians per channel.
// The lighting pass looks it up by N.L across and by the surface's scatter width down (1/r: flat skin at the top,
// a 2 mm radius at the bottom; see SM_SKIN in dynamic.hlsl). Built once on the CPU. Included from renderer.cpp.

namespace Render {

namespace skinlut {

// variance (mm^2), then the red, green and blue weights of each Gaussian
const float kProfile[6][4] = {
    {0.0064f, 0.233f, 0.455f, 0.649f}, {0.0484f, 0.100f, 0.336f, 0.344f}, {0.187f, 0.118f, 0.198f, 0.0f},
    {0.567f, 0.113f, 0.007f, 0.007f},  {1.99f, 0.358f, 0.004f, 0.0f},    {7.41f, 0.078f, 0.0f, 0.0f},
};
const int kSize = 64;           // texels per side
const float kMinRadiusMM = 2.f; // radius at the bottom row

// Diffusion profile at distance d (mm), per channel
vec3 profile(float d) {
    vec3 r(0.f);
    for (const auto& g : kProfile) {
        float w = expf(-d * d / (2.f * g[0])) / (2.f * kPi * g[0]);
        r += vec3(g[1], g[2], g[3]) * w;
    }
    return r;
}

// Scattered irradiance factor (0..1 per channel) for a ring of radius rMM lit at angle theta from its normal
vec3 integrate(float theta, float rMM) {
    // the profile's support is a few mm: integrate over the arc it covers (the whole ring when it is small)
    float xm = Min(kPi, 12.f / rMM);
    const int n = 256;
    vec3 num(0.f), den(0.f);
    for (int k = 0; k < n; k++) {
        float x = -xm + (k + 0.5f) * (2.f * xm / n);
        vec3 p = profile(2.f * rMM * sinf(fabsf(x) * 0.5f));
        num += p * Max(cosf(theta + x), 0.f);
        den += p;
    }
    return vec3(num.x / Max(den.x, 1e-20f), num.y / Max(den.y, 1e-20f), num.z / Max(den.z, 1e-20f));
}

}  // namespace skinlut

// RGBA16 UNORM: rgb scattered falloff; column i: N.L = i / (kSize - 1) * 2 - 1, row j: scatter width
// s = j / (kSize - 1), radius kMinRadiusMM / s (flat Lambert in row 0).
inline gfx::Texture buildSkinLUT() {
    using namespace skinlut;
    std::vector<u16> px((size_t)kSize * kSize * 4);
    for (int j = 0; j < kSize; j++) {
        float s = (float)j / (kSize - 1);
        for (int i = 0; i < kSize; i++) {
            float nl = (float)i / (kSize - 1) * 2.f - 1.f;
            vec3 d = j == 0 ? vec3(Max(nl, 0.f)) : integrate(acosf(Clamp(nl, -1.f, 1.f)), kMinRadiusMM / s);
            u16* o = &px[((size_t)j * kSize + i) * 4];
            o[0] = (u16)(Saturate(d.x) * 65535.f + 0.5f);
            o[1] = (u16)(Saturate(d.y) * 65535.f + 0.5f);
            o[2] = (u16)(Saturate(d.z) * 65535.f + 0.5f);
            o[3] = 65535;
        }
    }
    return gfx::createTexture2D(kSize, kSize, DXGI_FORMAT_R16G16B16A16_UNORM, gfx::TEX_SRV, 1, 1, px.data(), kSize * 8);
}

}  // namespace Render
