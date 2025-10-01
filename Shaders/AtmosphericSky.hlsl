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

// ---------- Resources ----------
Texture2D<float3> g_TransmittanceLUT : register(t0); // R^3
Texture3D<float4> g_ScatteringLUT : register(t1); // RGBA
Texture2D<float3> g_IrradianceLUT : register(t2); // optional
SamplerState g_LinearClamp : register(s1);

// ---------- Constants ----------
static const float PI = 3.14159265;

cbuffer PerFrame : register(b0)
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
};

cbuffer AtmosConstants : register(b1)
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

float3 ReconstructViewDirW(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 vs = mul(float4(ndc, 1.0, 1.0), g_InvProj);
    vs.xyz /= max(vs.w, 1e-6);
    return normalize(mul(vs.xyz, (float3x3) g_InvView)); // Drop translation
}

float3 Tonemap_ACES(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
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
    float4 scat = g_ScatteringLUT.Sample(g_LinearClamp, uvw);

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


float4 PSMain(VSOut i) : SV_Target
{
    float3 V = ReconstructViewDirW(i.uv);
    float3 L = SampleSky(V);

    // Tonemap -> Gamma
    float3 t = Tonemap_ACES(L);
    float3 srgb = pow(t, 1.0 / 2.2); // or output to an sRGB RT without manual pow
    return float4(srgb, 1.0);
}


