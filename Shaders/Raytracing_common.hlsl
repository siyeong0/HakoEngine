#ifndef RAYTRACING_COMMON_HLSL
#define RAYTRACING_COMMON_HLSL


#include "Raytracing_typedef.hlsl"

// Global Root Parameter
RWTexture2D<float4> g_OutputDiffuse : register(u0);
RWTexture2D<float4> g_OutputDepth : register(u1);
RaytracingAccelerationStructure Scene : register(t0, space0);

cbuffer CONSTANT_BUFFER_RAY_TRACING : register(b0)
{
    float g_Near;
    float g_Far;
    uint g_MaxRadianceRayRecursionDepth;
    uint Reserved0;
};

float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
		attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
		attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}
float2 HitAttribute(float2 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

#endif // RAYTRACING_COMMON_HLSL
