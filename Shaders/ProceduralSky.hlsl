// ==========================================================
// Procedural Sky (Single Scattering, Rayleigh + Mie Approx)
// Fullscreen-triangle pixel shader
// Units: kilometers (km) for heights / distances
// Notes:
//  - g_SunDir_W is the light *propagation* direction (sun -> ground).
//  - Use DepthWrite = ZERO and LESS_EQUAL (or GREATER_EQUAL for reversed-Z).
// ==========================================================

Texture2D<float> _Dummy : register(t0); // not used; keep slot layout clean
SamplerState _LinearClamp : register(s0);

cbuffer CB_SkyMatrices : register(b0)
{
    float4x4 g_InvProj;
    float4x4 g_InvView; // inverse view with translation removed (pure rotation)
}

cbuffer CB_SkyParams : register(b1)
{
    float3 g_SunDir_W; // world-space, normalized (sun -> ground)
}

static const float PI = 3.14159265;
static const float SUN_ANGULAR_RADIUS = 0.00465;

// ----------------------------------------------------------

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0; // NDC xy
};

VSOut VSMain(uint id : SV_VertexID)
{
    // Fullscreen triangle
    float2 verts[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
    VSOut o;
    o.PosH = float4(verts[id], 0, 1);
    o.UV = o.PosH.xy;
    return o;
}

float4 PSMain(VSOut IN) : SV_Target
{
    return float4(0.4, 0.6, 0.85, 1); // TODO: implement procedural sky shader
}
