// Procedural noise functions (CPU side). GPU equivalents live in shaders/noise.
#pragma once
#include "math.h"

float valueNoise2(float x, float y, u32 seed);
float perlin2(float x, float y, u32 seed);           // ~[-1,1]
float perlin3(float x, float y, float z, u32 seed);  // ~[-1,1]
float simplex2(float x, float y, u32 seed);          // ~[-1,1]
float fbm2(float x, float y, int octaves, float lacunarity, float gain, u32 seed);
float ridged2(float x, float y, int octaves, float lacunarity, float gain, u32 seed);
// Worley (cellular) returning F1 distance in cell units.
float worley2(float x, float y, u32 seed, vec2* cellPoint = nullptr, u32* cellHash = nullptr);
// Domain-warped fbm useful for coastlines and natural boundaries.
float warpedFbm2(float x, float y, int octaves, float warp, u32 seed);
