#include "HLSL_Common.hlsli"
#include "BxDF.hlsli"

Texture2D g_DiffuseTex : register(t0);
Texture2D g_NormalTex : register(t1);

cbuffer CONSTANT_BUFFER_PER_FRAME : register(b0)
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

cbuffer CONSTANT_BUFFER_PER_OBJECT : register(b1)
{
    matrix g_World;
    BasicMaterial g_Material;
};

SamplerState g_SamplerWrap : register(s0);
SamplerState g_SamplerClamp : register(s1);
SamplerState g_SamplerBorder : register(s2);
SamplerState g_SamplerMirror : register(s3);

struct VSInput
{
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

struct PSInput
{
    float4 ScreenPosition : SV_POSITION;
    float3 WorldPosition : POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldNormal : NORMAL;
    float3 WorldTangent : TANGENT0;
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
    PSInput vsout = (PSInput) 0;
    
    matrix worldViewProj = mul(g_World, g_ViewProj);
    vsout.ScreenPosition = mul(input.Position, worldViewProj);
    vsout.WorldPosition = mul(input.Position, g_World).xyz;
    vsout.TexCoord = input.TexCoord;
    vsout.WorldNormal = normalize(mul(input.Normal, inverseTranspose((float3x3) g_World)));
    vsout.WorldTangent = normalize(mul(input.Tangent, (float3x3) g_World));
    
    return vsout;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 L = 0;
    
    float4 texDiffuse = g_DiffuseTex.Sample(g_SamplerClamp, input.TexCoord);
    float3 texNormal = g_NormalTex.Sample(g_SamplerWrap, input.TexCoord).xyz;
    
    float3 worldNormal = input.WorldNormal;
    float3 worldTangent = input.WorldTangent;
    float3 worldBinormal = normalize(cross(worldTangent, worldNormal));
    
    float3 tanNormal = texNormal.rgb * 2.0 - 1.0;
    float3 surfaceNormal = (tanNormal.xxx * worldTangent) + (tanNormal.yyy * worldBinormal) + (tanNormal.zzz * worldNormal);
    
    float3 viewDir = normalize(g_InvView._41_42_43 - input.WorldPosition);

    float3 baseColor = g_Material.BaseColor * texDiffuse.rgb;
    float metallic = g_Material.MetallicFactor;
    float roughness = max(0.04f, saturate(g_Material.RoughnessFactor));
    
    const float3 Kd = (1.0f - metallic) * baseColor;
    const float3 Ks = saturate(lerp(g_Material.SpecularColor, baseColor, metallic) * g_Material.SpecularFactor);

	// Direct illumination
    if (!IsBlack(Kd) || !IsBlack(Ks))
    {
        for (uint i = 0; i < g_NumLights; i++)
        {
            const float3 lightColor = g_LightList[i].Color;
            float3 wi;
            if (g_LightList[i].Type == LIGHT_TYPE_DIRECTIONAL)
            {
                wi = normalize(-g_LightList[i].PosOrDir);
            }
            else /* point */
            {
                wi = normalize(g_LightList[i].PosOrDir - input.WorldPosition);
            }
            
			// Raytraced shadows.
            bool isInShadow = false;
            // Kd = diffuse , Ks = specular , V = view vector, wi = light vector
            L += BxDF_ShadeDirect(
						Kd,
						Ks,
						lightColor.rgb,
						isInShadow,
						roughness,
						surfaceNormal,
						viewDir,
						wi);
        }
    }
	// Ambient Indirect Illumination
	// Add a default ambient contribution to all hits. 
	// This will be subtracted for hitPositions with 
	// calculated Ambient coefficient in the composition pass.
    L += g_Material.AmbientOcclusionStrength * Kd;
	
    return float4(L, 1.0);
}
