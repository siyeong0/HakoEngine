//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// A family of BRDF, BSDF and BTDF functions.
// BRDF, BSDF, BTDF - bidirectional reflectance, scattering, transmission distribution function.
// Ref: Ray Tracing from the Ground Up (RTG), Suffern
// Ref: Real-Time Rendering (RTR), Fourth Edition
// Ref: Real Shading in Unreal Engine 4 (Karis_UE4), Karis2013
// Ref: PBR Diffuse Lighting for GGX+Smith Microsurfaces, Hammon2017

// BRDF terms generally include 1 / PI factor, but this is removed in the implementations below as it cancels out
// with the omitted PI factor in the reflectance equation. Ref: eq 9.14, RTR

// Parameters:
// iorIn - ior of media ray is coming from
// iorOut - ior of media ray is going to
// eta - relative index of refraction, namely iorIn / iorOut
// G - shadowing/masking function.
// Fo - specular reflectance at normal incidence (AKA specular color).
// Albedo - material color
// Roughness - material roughness
// N - surface normal
// V - direction to viewer
// L - incoming "to-light" direction
// T - transmission scale factor (aka transmission color)
// thetai - incident angle

#ifndef BXDF_HLSL
#define BXDF_HLSL

#include "HLSL_Common.hlsli"

// Fresnel reflectance - Schlick approximation.
float3 BxDF_Fresnel(in float3 F0, in float cos_thetai)
{
    return F0 + (1.0 - F0) * pow(abs(1.0 - cos_thetai), 5.0);
    // return F0 + (1 - F0) * pow(1 - cos_thetai, 5);
}

// -----------------------------------------------------------------------------
// Diffuse
// -----------------------------------------------------------------------------

// Lambert: returns albedo (1/PI omitted to cancel in rendering eq.)
float3 BxDF_DiffuseLambertF(in float3 albedo)
{
    return albedo;
}

// Hammon2017 diffuse
float3 BxDF_DiffuseHammonF(
    in float3 Albedo,
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L,
    in float3 Fo)
{
    float3 diffuse = 0;

    float3 H = normalize(V + L);
    float NoH = dot(N, H);
    if (NoH > 0)
    {
        float a = Roughness * Roughness;

        float NoV = saturate(dot(N, V));
        float NoL = saturate(dot(N, L));
        float LoV = saturate(dot(L, V));

        float facing = 0.5 + 0.5 * LoV;
        float rough = facing * (0.9 - 0.4 * facing) * ((0.5 + NoH) / NoH);
        float3 smooth = 1.05 * (1 - pow(1 - NoL, 5)) * (1 - pow(1 - NoV, 5));

        // Extract 1 / PI from the single equation since it's omitted in the reflectance function.
        float3 single = lerp(smooth, rough, a); // scalar 'rough' expands to float3
        float multi = 0.3641 * a; // 0.3641 = PI * 0.1159

        diffuse = Albedo * (single + Albedo * multi);
    }
    return diffuse;
}

// -----------------------------------------------------------------------------
// Specular - Perfect reflection / transmission + GGX microfacet
// -----------------------------------------------------------------------------

// Perfect reflection: computes L and returns BRDF (Fresnel) value for that direction.
// Note: returns value without dividing by cos term (to cancel in rendering eq. caller).
float3 BxDF_SampleReflectionFr(
    in float3 V,
    out float3 L,
    in float3 N,
    in float3 Fo)
{
    L = reflect(-V, N);
    float cos_thetai = dot(N, L);
    return BxDF_Fresnel(Fo, cos_thetai);
}

// Check total internal reflection (preserved exactly as original)
bool BxDF_IsTotalInternalReflection(
    in float3 V,
    in float3 normal)
{
    float ior = 1;
    float eta = ior;
    float cos_thetai = dot(normal, V); // Incident angle

    return 1 - 1 * (1 - cos_thetai * cos_thetai) / (eta * eta) < 0;
}

// Perfect transmission: computes transmitted ray wt and returns BRDF value for that direction.
// Note: returns value without dividing by cos term (to cancel in rendering eq. caller).
float3 BxDF_SampleTransmissionFt(
    in float3 V,
    out float3 wt,
    in float3 N,
    in float3 Fo)
{
    // TODO in original: use parameters; keep hardcoded iors for identical behavior.
    float iorIn = 1.0; // air
    float iorOut = 1.33; // water
    float eta = iorIn / iorOut;

    wt = refract(-V, N, eta);

    float cos_thetai = dot(V, N);
    float3 Kr = BxDF_Fresnel(Fo, cos_thetai);
    return (1 - Kr);
}

// GGX microfacet specular BRDF (Karis style)
float3 BxDF_SpecularGGXF(
    in float Roughness, // assumed remapped to alpha already
    in float3 N,
    in float3 V,
    in float3 L,
    in float3 Fo)
{
    float3 H = V + L;
    float NoL = dot(N, L);
    float NoV = dot(N, V);
    float3 specular = 0;

    // Preserve original any/all usage (original used all(H))
    if (NoL > 0 && NoV > 0 && all(H))
    {
        H = normalize(H);
        float a = Roughness;
        float3 M = H; // microfacet normal
        float NoM = saturate(dot(N, M));
        float HoL = saturate(dot(H, L));

        // D (eq 9.41 RTR): a^2 / (1 + NoM^2 (a^2 - 1))^2
        float denom = 1 + NoM * NoM * (a * a - 1);
        float D = (a * a) / (denom * denom); // Karis

        // F (Schlick)
        float3 F = BxDF_Fresnel(Fo, HoL);

        // G (Smith form with 1/(4 NoL NoV) baked-in; Karis)
        float G = 0.5 / lerp(2 * NoL * NoV, NoL + NoV, a);

        specular = F * G * D;
    }

    return specular;
}

// -----------------------------------------------------------------------------
// Direct lighting wrapper (single light)
// -----------------------------------------------------------------------------

float3 BxDF_ShadeDirect(
    in MATERIAL_TYPE materialType,
    in float3 Albedo,
    in float3 Fo,
    in float3 Radiance,
    in bool inShadow,
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L)
{
    float3 directLighting = 0;

    float NoL = dot(N, L);
    if (!inShadow && NoL > 0)
    {
        float3 directDiffuse = 0;
        if (!IsBlack(Albedo))
        {
            if (materialType == MATERIAL_TYPE_DEFAULT)
            {
                directDiffuse = BxDF_DiffuseHammonF(Albedo, Roughness, N, V, L, Fo);
            }
            else
            {
                directDiffuse = BxDF_DiffuseLambertF(Albedo);
            }
        }

        float3 directSpecular = 0;
        if (materialType == MATERIAL_TYPE_DEFAULT)
        {
            directSpecular = BxDF_SpecularGGXF(Roughness, N, V, L, Fo);
        }

        directLighting = NoL * Radiance * (directDiffuse + directSpecular);
    }

    return directLighting;
}

// -----------------------------------------------------------------------------
// Full shade (direct + simple ambient)
// -----------------------------------------------------------------------------

float3 BxDF_Shade(
    in MATERIAL_TYPE materialType,
    in float3 Albedo,
    in float3 Fo,
    in float3 Radiance,
    in bool isInShadow,
    in float AmbientCoef,
    in float Roughness,
    in float3 N,
    in float3 V,
    in float3 L)
{
    float NoL = dot(N, L);
    Roughness = max(0.1, Roughness);

    float3 directLighting = 0;

    if (!isInShadow && NoL > 0)
    {
        // Diffuse
        float3 diffuse;
        if (materialType == MATERIAL_TYPE_DEFAULT)
        {
            diffuse = BxDF_DiffuseHammonF(Albedo, Roughness, N, V, L, Fo);
        }
        else
        {
            diffuse = BxDF_DiffuseLambertF(Albedo);
        }

        // Specular
        float3 directDiffuse = diffuse;
        float3 directSpecular = 0;

        if (materialType == MATERIAL_TYPE_DEFAULT)
        {
            directSpecular = BxDF_SpecularGGXF(Roughness, N, V, L, Fo);
        }

        directLighting = NoL * Radiance * (directDiffuse + directSpecular);
    }

    float3 indirectDiffuse = AmbientCoef * Albedo;
    float3 indirectLighting = indirectDiffuse;

    return directLighting + indirectLighting;
}

#endif // BXDF_HLSL
