#ifndef ATMOSPHERIC_SKY_HLSLI
#define ATMOSPHERIC_SKY_HLSLI

static const float PI = 3.14159265;

// ==========================================================
// Helper Functions
// ==========================================================

float RayleighPhase(float cosTheta)
{
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float gg = g * g;
    return (1.0 - gg) / (4.0 * PI * pow(1.0 + gg - 2.0 * g * cosTheta, 1.5));
}

// Local "up" at camera (planet-centered)
float3 GetUp(float3 camPosPlanetCS)
{
    float r = max(length(camPosPlanetCS), 1e-6);
    return camPosPlanetCS / r;
}

float3 Tonemap_ACES(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

#endif