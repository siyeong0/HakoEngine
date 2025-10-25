#ifndef SAMPLER_DECLARATION_HLSLI
#define SAMPLER_DECLARATION_HLSLI

SamplerState g_SamplerWrap : register(s0);
SamplerState g_SamplerClamp : register(s1);
SamplerState g_SamplerPoint : register(s2);
SamplerState g_SamplerMirror : register(s3);

#endif