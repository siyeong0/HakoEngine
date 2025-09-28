Texture2D texDiffuse : register(t0);
SamplerState samplerDiffuse : register(s0);

cbuffer CONSTANT_BUFFER_DEFAULT : register(b0)
{
    matrix g_matWorld;
    matrix g_matView;
	matrix g_matProj;
};

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
    
    matrix matViewProj = mul(g_matView, g_matProj);        // view x proj
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
    // Sample the texture
    float4 texColor = texDiffuse.Sample(samplerDiffuse, input.TexCoord);

    // Diffuse lighting (Lambert)
    float3 g_LightDir = float3(0.5f, -1.0, 0.3f); // Directional light direction (normalized) TODO: move to cbuffer
    float NdotL = saturate(dot(normalize(input.Normal), -normalize(g_LightDir)));

    float3 g_LightColor = float3(1.0f, 1.0f, 1.0f); // White light TODO: move to cbuffer
    float3 diffuse = g_LightColor * NdotL;
    float3 g_Ambient = float3(0.4f, 0.4f, 0.4f); // Ambient light color TODO: move to cbuffer
    float3 ambient = g_Ambient;

    float3 finalColor = texColor.rgb * (diffuse + ambient);

    return float4(finalColor, texColor.a);
}
