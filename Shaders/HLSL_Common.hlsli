#ifndef COMMON_TYPES_HLSLI
#define COMMON_TYPES_HLSLI

#include "HLSL_CPP_CommonTypes.h"

struct Light
{
    float3 PosOrDir;
    float Rs;
    float3 Color;
    LIGHT_TYPE Type;
};

struct BasicMaterial
{
    float3 Ks;
    MATERIAL_TYPE Type;
    float3 Kr;
    float Roughness;
    float3 Kt;
    float AmbientIntensity;
    float3 Opacity;
    uint Reserved0;
};

// shader internal
struct ShadingMaterial
{
    float3 Kd;
    float3 Ks;
    float3 Kr;
    float3 Kt;
    MATERIAL_TYPE Type;
    float Roughness;
    float AmbientIntensity;
};

bool IsBlack(in float3 color)
{
    return !any(color);
}

#endif