#include "HLSL_CPP_CommonTypes.h"

struct Light
{
    float3 PosOrDir;
    float Rs;
    float3 Color;
    LIGHT_TYPE Type;
};