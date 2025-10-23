#ifndef COMMON_TYPES_HLSLI
#define COMMON_TYPES_HLSLI

#include "HLSL_CPP_CommonTypes.h"

static const float ALPHA_TEST_THRESHOLD = 0.01;

struct Light
{
    float3 PosOrDir;
    float Rs;
    float3 Color;
    LIGHT_TYPE Type;
};

struct BasicMaterial
{
    float3 BaseColor;
    float Opacity;

    float3 SpecularColor;
    float SpecularFactor;

    float MetallicFactor;
    float RoughnessFactor;
    float NormalScale;
    float AmbientOcclusionStrength;
};

bool IsBlack(in float3 color)
{
    return !any(color);
}

#endif