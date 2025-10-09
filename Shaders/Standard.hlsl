Texture2D texDiffuse : register(t0);

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

cbuffer CONSTANT_BUFFER_PER_OBJECT : register(b1)
{
    matrix g_World;
};

SamplerState g_SamplerWrap : register(s0);
SamplerState g_SamplerClamp : register(s1);
SamplerState g_SamplerBorder : register(s2);
SamplerState g_SamplerMirror : register(s3);

struct VSInput
{
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 Normal   : NORMAL;
    float3 Tangent  : TANGENT;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 Normal   : NORMAL;
};

float3x3 inverseTranspose(float3x3 M)
{
    float3 c0 = float3(M._11, M._21, M._31);
    float3 c1 = float3(M._12, M._22, M._32);
    float3 c2 = float3(M._13, M._23, M._33);
    float3 r0 = cross(c1, c2);
    float3 r1 = cross(c2, c0);
    float3 r2 = cross(c0, c1);
    float det = dot(c0, r0);
    return transpose(float3x3(r0, r1, r2) / det); // = M^{-T}
}

PSInput VSMain(VSInput input)
{
    PSInput vsout = (PSInput)0;
    
    matrix worldViewProj = mul(g_World, g_ViewProj);   // world x view x proj
    
    vsout.Position = mul(input.Position, worldViewProj); // pojtected vertex = vertex x world x view x proj
    vsout.TexCoord = input.TexCoord;
    vsout.Normal = normalize(mul(input.Normal, inverseTranspose((float3x3) g_World)));
    
    return vsout;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 texColor = texDiffuse.Sample(g_SamplerWrap, input.TexCoord);

    // Diffuse lighting (Lambert)
    float NdotL = saturate(dot(normalize(input.Normal), -normalize(g_LightDir)));
    float3 diffuse = g_LightColor * NdotL;
    float3 ambient = g_Ambient;

    float3 finalColor = texColor.rgb * (diffuse + ambient);

    return float4(finalColor, texColor.a);
}
