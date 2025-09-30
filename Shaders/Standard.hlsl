Texture2D texDiffuse : register(t0);

cbuffer CB_PerFrame : register(b0)
{
    matrix g_ViewMatrix;
    matrix g_ProjMatrix;
    matrix g_ViewProjMatrix;
    matrix g_InvViewMatrix;
    matrix g_InvProjMatrix;
    matrix g_InvViewProjMatrix;
    
    float3 g_LightDir; // Directional light direction (normalized)
    float _pad0;
    float3 g_LightColor; // Light color
    float _pad1;
    float3 g_Ambient; // Ambient light color
    float _pad2;
};

cbuffer CB_Object : register(b1)
{
    matrix g_matWorld;
};

SamplerState samplerWrap : register(s0);
SamplerState samplerClamp : register(s1);
SamplerState samplerBorder : register(s2);
SamplerState samplerMirror : register(s3);

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
    
    matrix matViewProj = mul(g_ViewMatrix, g_ProjMatrix);        // view x proj
    matrix matWorldViewProj = mul(g_matWorld, matViewProj);   // world x view x proj
    
    result.Position = mul(input.Position, matWorldViewProj); // pojtected vertex = vertex x world x view x proj
    result.TexCoord = input.TexCoord;
    
    // Use tangent as fake normal (normalize it in world space)
    float3 fakeNormal = mul((float3x3) g_matWorld, input.Tangent);
    result.Normal = normalize(fakeNormal);
    
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample the texture5
    float4 texColor = texDiffuse.Sample(samplerWrap, input.TexCoord);

    // Diffuse lighting (Lambert)
    float NdotL = saturate(dot(normalize(input.Normal), -normalize(g_LightDir)));
    float3 diffuse = g_LightColor * NdotL;
    float3 ambient = g_Ambient;

    float3 finalColor = texColor.rgb * (diffuse + ambient);

    return float4(finalColor, texColor.a);
}
