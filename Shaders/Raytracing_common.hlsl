#ifndef RAYTRACING_COMMON_HLSL
#define RAYTRACING_COMMON_HLSL

#include "Raytracing_typedef.hlsl"

// Global Root Parameter
RWTexture2D<float4> g_OutputDiffuse : register(u0);
RWTexture2D<float4> g_OutputDepth : register(u1);
RaytracingAccelerationStructure Scene : register(t0, space0);

SamplerState g_SamplerWrap : register(s0);
SamplerState g_SamplerClamp : register(s1);
SamplerState g_SamplerPoint : register(s2); // TODO: sync with Standard SamplerState
SamplerState g_SamplerMirror : register(s3);

// Local Root Parameter
StructuredBuffer<Vertex> l_Vertices : register(t0, space1);
ByteAddressBuffer l_Indices : register(t1, space1);
Texture2D<float4> l_DiffuseTexture : register(t2, space1);

cbuffer CONSTANT_BUFFER_PER_FRAME : register(b0)
{
    matrix g_View;
    matrix g_Proj;
    matrix g_ViewProj;
    matrix g_InvView;
    matrix g_InvProj;
    matrix g_InvViewProj;
    
    float3 g_LightDir; // Directional light direction (normalized)
    float _pad0;
    float3 g_LightColor; // Light color
    float _pad1;
    float3 g_Ambient; // Ambient light color
    float _pad2;
    
    float g_Near;
    float g_Far;
    uint g_MaxRadianceRayRecursionDepth;
};

// Interpolate vertex attribute using barycentric coordinates.
float4 HitAttribute(float4 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
		attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
		attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

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

// Load three 16 bit indices.
static uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

	// ByteAdressBuffer loads must be aligned at a 4 byte boundary.
	// Since we need to read three 16 bit indices: { 0, 1, 2 } 
	// aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
	// we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
	// based on first index's offsetBytes being aligned at the 4 byte boundary or not:
	//  Aligned:     { 0 1 | 2 - }
	//  Not aligned: { - 0 | 1 2 }
    const uint alignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = l_Indices.Load2(alignedOffset);

	// Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (alignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Notaligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}


#endif // RAYTRACING_COMMON_HLSL
