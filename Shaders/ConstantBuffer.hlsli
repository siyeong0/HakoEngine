#ifndef CONSTANT_BUFFER_DECLARATION_HLSLI
#define CONSTANT_BUFFER_DECLARATION_HLSLI

#include "HLSL_CPP_CommonTypes.h"

struct Light
{
    float3 PosOrDir;
    float Rs;
    float3 Color;
    LIGHT_TYPE Type;
};

cbuffer CONSTANT_BUFFER_PER_FRAME : register(b0, space0)
{
    matrix g_View;
    matrix g_Proj;
    matrix g_ViewProj;
    matrix g_InvView;
    matrix g_InvProj;
    matrix g_InvViewProj;
    
    float g_Near;
    float g_Far;
    
    uint g_MaxRadianceRayRecursionDepth;
    uint g_MaxShadowRayRecursionDepth;
    uint g_NumLights;
    uint Reserved0;
    uint Reserved1;
    uint Reserved2;
    
    Light g_LightList[MAX_LIGHT_COUNT];
};

cbuffer CONSTANT_BUFFER_ATMOS : register(b0, space1)
{
    // Camera + sun (in planet-centered space; normalized)
    float3 g_CameraPosPlanetCoord;
    float __pad0;
    
    // TODO: Unify with g_Light### ?
    float3 g_SunDir;
    float g_SunExposure;
    float3 g_SunIrradiance;
    float __pad1;
    
    // Radii
    float g_PlanetRadius; // Rg
    float g_AtmosphereHeight; // H
    float g_TopRadius; // Rt = Rg + H
    float __pad2;
    
    // Mie phase parameter & tint (derived from MieScattering RGB; unitless tint)
    float g_MieG;
    float3 g_MieTint; // normalize(MieScatteringRGB); or (rgb / max(avg(rgb),eps))

    // LUT logical sizes (must match bake)
    float g_TW; // TransmittanceW/H
    float g_TH;
    float g_SR; // Scattering R, MU, MUS, NU counts
    float g_SMU;
    float g_SMUS;
    float g_SNU;
}

// ==========================================================
// LUT mapping (uniform with texel centers)
//  - Transmittance: (mu ∈ [-1,1], r ∈ [Rg,Rt])  -> (u,v)
//  - Scattering 3D packed dims: X = SNU * SMUS, Y = SMU, Z = SR
//    Input params: r, mu, muS, nu
// ==========================================================

// map to texel-center UV in [0,1]
float2 GetMapTransmittanceUV(float r, float mu)
{
    // grid counts
    float TW = g_TW;
    float TH = g_TH;

    // index at texel centers
    float iu = ((mu + 1.0) * 0.5) * (TW - 1.0);
    float iv = ((r - g_PlanetRadius) / (g_TopRadius - g_PlanetRadius)) * (TH - 1.0);

    float2 uv = (float2(iu + 0.5, iv + 0.5)) / float2(TW, TH);
    return uv;
}

float3 GetMapScatteringUVW(float r, float mu, float muS, float nu)
{
    // dimensions
    float SR = g_SR;
    float SMU = g_SMU;
    float SMUS = g_SMUS;
    float SNU = g_SNU;

    // index at texel centers (uniform bins)
    float ix = (((muS + 1.0) * 0.5) * (SMUS - 1.0)) * SNU // mus bin
             + (((nu + 1.0) * 0.5) * (SNU - 1.0)); // nu  bin

    float iy = (((mu + 1.0) * 0.5) * (SMU - 1.0)); // mu  bin
    float iz = (((r - g_PlanetRadius) / (g_TopRadius - g_PlanetRadius)) * (SR - 1.0)); // r bin

    float3 dim = float3(SNU * SMUS, SMU, SR);
    float3 uvw = (float3(ix + 0.5, iy + 0.5, iz + 0.5)) / dim;
    return uvw;
}


#endif