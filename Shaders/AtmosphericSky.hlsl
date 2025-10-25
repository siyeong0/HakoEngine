// ==========================================================
// Atmospheric Sky - SampleSky (Precomputed Single Scattering)
// Author: you & gpt
// Assumptions:
//  - Transmittance 2D: r∈[Rg,Rt], mu∈[-1,1], uniform texel-center mapping
//  - Scattering 3D (packed): dims = (SNU*SMUS, SMU, SR)
//      value = float4(RayleighRGB, MieScalar)
//      includes view-path transmittance (no phase applied)
//  - All textures are LINEAR (no sRGB sampling)
// ==========================================================

#include "AtmosphericSky.hlsli"
#include "Sampler.hlsli"
#include "ConstantBuffer.hlsli"

// ---------- Resources ----------
Texture2D<float3> g_TransmittanceLUT : register(t0); // R^3
Texture3D<float4> g_ScatteringLUT : register(t1); // RGBA
Texture2D<float3> g_IrradianceLUT : register(t2); // optional

// ==========================================================
// Helper: Fullscreen triangle VS (SV_VertexID)
// ==========================================================

struct VSOut
{
    float4 pos : SV_Position; // clip space
    float2 uv : TEXCOORD0; // 0~1 (top left origin)
};

VSOut VSMain(uint vid : SV_VertexID)
{
    // Per-vertex uv (0,0)=top-left, (1,1)=bottom-right
    float2 uv = float2((vid == 1 || vid == 3) ? 0.0 : 1.0,
                       (vid < 2) ? 0.0 : 1.0);

    // uv -> clip-space (be careful with y-flip)
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    VSOut o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv = uv;
    return o;
}

// ==========================================================
// SampleSky: single scattering using precomputed LUTs
// ==========================================================
float3 SampleSky(float3 viewDirWorld)
{
    // Normalize inputs
    float3 viewDir = normalize(viewDirWorld);
    float3 sunDir = -normalize(g_SunDir);

    // Planet-centered camera info
    float r = length(g_CameraPosPlanetCoord); // camera radius
    float3 up = GetUp(g_CameraPosPlanetCoord); // local up

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
    float4 scat = g_ScatteringLUT.Sample(g_SamplerClamp, uvw);

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

// ==========================================================
// Tonemapping & Output
// ==========================================================

float3 ReconstructViewDirW(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 vs = mul(float4(ndc, 1.0, 1.0), g_InvProj);
    vs.xyz /= max(vs.w, 1e-6);
    return normalize(mul(vs.xyz, (float3x3) g_InvView)); // Drop translation
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 V = normalize(ReconstructViewDirW(i.uv));
    float3 L = SampleSky(V);

    // Tonemap -> Gamma
    float3 t = Tonemap_ACES(L);
    float3 srgb = pow(t, 1.0 / 2.2); // or output to an sRGB RT without manual pow
    return float4(srgb, 1.0);
}


