#ifndef RAYTRACING_COMMON_HLSL
#define RAYTRACING_COMMON_HLSL

#include "Raytracing_typedef.hlsl"
#include "AtmosphericSky.hlsli"
#include "Sampler.hlsli"
#include "ConstantBuffer.hlsli"

// Global Root Parameter
RWTexture2D<float4> g_OutputDiffuse : register(u0);
RWTexture2D<float4> g_OutputDepth : register(u1);
RaytracingAccelerationStructure Scene : register(t0, space3);

Texture2D<float3> g_TransmittanceLUT : register(t10, space0); // R^3
Texture3D<float4> g_ScatteringLUT : register(t11, space0); // RGBA
Texture2D<float3> g_IrradianceLUT : register(t12, space0); // optional

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

// ==========================================================
// SampleSky: single scattering using precomputed LUTs
// ==========================================================
float3 SampleSky(float3 viewDirWorld)
{
    // Planet-centered camera info
    float r = length(g_CameraPosPlanetCoord); // camera radius
    float3 up = GetUp(g_CameraPosPlanetCoord); // local up
    
    // Normalize inputs
    float3 viewDir = normalize(viewDirWorld);
    float3 sunDir = -normalize(g_SunDir);

    // Direction cosines
    float mu = dot(viewDir, up); // angle between view and local up
    float muS = dot(sunDir, up); // angle between sun and local up
    float nu = dot(viewDir, sunDir); // angle between view and sun

    // Clamp physical domain
    r = clamp(r, g_PlanetRadius, g_TopRadius);
    mu = clamp(mu, -1.0, 1.0);
    muS = clamp(muS, -1.0, 1.0);
    nu = clamp(nu, -1.0, 1.0);

    // --- Sample scattering LUT (RayleighRGB, MieScalar) ---
    float3 uvw = GetMapScatteringUVW(r, mu, muS, nu);
    float4 scat = g_ScatteringLUT.SampleLevel(g_SamplerClamp, uvw, 0);

    // --- Phase functions (your LUT excludes phase) ---
    // NOTE: S is "sun -> ground". For usual phase angle θ between "light direction" and -V,
    // cosθ = dot(-S, V) = -dot(S, V) = -nu. If your definition wants cosθ = dot(V, -S),
    // it's the same value (-nu). We'll use cosθ = -nu below.
    float cosTheta = -nu;

    float PR = RayleighPhase(cosTheta);
    float PM = HenyeyGreenstein(cosTheta, g_MieG);

    // --- Mie scalar -> RGB via tint ---
    float3 mieRGB = scat.a * g_MieTint;

    // --- Combine (phase only; view transmittance is already baked in the LUT) ---
    float3 L = scat.rgb * PR + mieRGB * PM;

    // Optional: If your LUT does NOT include view transmittance, multiply here by Transmittance:
    //float2 uvt = GetMapTransmittanceUV(r, mu);
    //float3 Tview = g_TransmittanceLUT.Sample(g_LinearClamp, uvt);
    //L *= Tview;

    // Sun irradiance scaling + exposure
    L *= g_SunIrradiance;
    L *= g_SunExposure;

    return L; // linear HDR radiance
}

float SunDiskMask(float cosTheta, float radius, float feather)
{
    float rIn = radius;
    float rOut = radius + max(feather, 0.0f);

    float cIn = cos(rIn);
    float cOut = cos(rOut);

    // cosTheta가 cOut→cIn로 갈수록 0→1
    return saturate(smoothstep(cOut, cIn, cosTheta));
}

float SunHaloMask(float cosTheta, float radius, float radiusMul, float feather)
{
    float rIn = radius * max(radiusMul, 1.0f);
    float rOut = rIn + max(feather, 0.0f);

    float cIn = cos(rIn);
    float cOut = cos(rOut);

    // 디스크 바깥쪽에서 부드럽게 깔리는 헤일로
    return saturate(smoothstep(cOut, cIn, cosTheta));
}

static const float SUN_DEFAULT_INTENSITY = 15.0f;
static const float3 SUN_DEFAULT_COLOR = float3(1.0, 0.95, 0.85);
static const float SUN_DEFAULT_RADIUS = 0.004675f; // ~0.267°
static const float SUN_DEFAULT_FEATHER = SUN_DEFAULT_RADIUS * 3.0f;
static const float SUN_DEFAULT_HALO_MUL = 0.15f;
static const float SUN_DEFAULT_HALO_RMUL = 5.0f;

float3 EvaluateSun(float3 dir)
{
    // 파라미터 기본 보정
    float sunI = SUN_DEFAULT_INTENSITY;
    float3 sunCol = SUN_DEFAULT_COLOR;
    float rad = SUN_DEFAULT_RADIUS;
    float fth = SUN_DEFAULT_FEATHER;
    float haloM = SUN_DEFAULT_HALO_MUL;
    float haloRM = SUN_DEFAULT_HALO_RMUL;

    float cosTheta = dot(dir, -normalize(g_SunDir));

    // 디스크(원반) + 헤일로(코로나) 마스크
    float disk = SunDiskMask(cosTheta, rad, fth);
    float halo = SunHaloMask(cosTheta, rad, haloRM, fth) * haloM;

    // 간단한 limb-darkening 느낌을 조금 주고 싶다면:
    // disk *= saturate((cosTheta - cos(rad)) / (1.0 - cos(rad)))^0.25; // 선택사항

    float sunMask = disk + halo;

    // 최종 태양 기여(HDR)
    return sunCol * sunI * sunMask;
}

#endif // RAYTRACING_COMMON_HLSL
