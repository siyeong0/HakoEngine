Texture2D texDiffuse : register(t0);

cbuffer CB_PerFrame : register(b0)
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

cbuffer CB_Object : register(b1)
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
    float3 Tangent  : TANGENT;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 Normal   : NORMAL;
};

PSInput VSMain(VSInput input)
{
    PSInput result = (PSInput)0;
    
    matrix worldViewProj = mul(g_World, g_ViewProj);   // world x view x proj
    
    result.Position = mul(input.Position, worldViewProj); // pojtected vertex = vertex x world x view x proj
    result.TexCoord = input.TexCoord;
    
    // Use tangent as fake normal (normalize it in world space)
    float3 fakeNormal = mul((float3x3) g_World, input.Tangent);
    result.Normal = normalize(fakeNormal);
    
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample the texture5
    float4 texColor = texDiffuse.Sample(g_SamplerWrap, input.TexCoord);

    // Diffuse lighting (Lambert)
    float NdotL = saturate(dot(normalize(input.Normal), -normalize(g_LightDir)));
    float3 diffuse = g_LightColor * NdotL;
    float3 ambient = g_Ambient;

    float3 finalColor = texColor.rgb * (diffuse + ambient);

    return float4(finalColor, texColor.a);
}
