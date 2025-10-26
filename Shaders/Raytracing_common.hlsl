#ifndef RAYTRACING_COMMON_HLSL
#define RAYTRACING_COMMON_HLSL

#include "Raytracing_typedef.hlsl"
#include "AtmosphericSky.hlsli"
#include "Sampler.hlsli"
#include "ConstantBuffer.hlsli"
#include "BxDF.hlsli"

// Global Root Parameter
RWTexture2D<float4> g_OutputDiffuse : register(u0);
RWTexture2D<float4> g_OutputDepth : register(u1);
RaytracingAccelerationStructure Scene : register(t0, space3);

Texture2D<float3> g_TransmittanceLUT : register(t10, space0); // R^3
Texture3D<float4> g_ScatteringLUT : register(t11, space0); // RGBA
Texture2D<float3> g_IrradianceLUT : register(t12, space0); // optional

// Interpolate vertex attribute using barycentric coordinates.
float4 HitAttribute(float4 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
		attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
		attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
		attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
		attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

float2 HitAttribute(float2 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

RadiancePayload TraceRadianceRay(in Ray ray, in uint currRayRecursionDepth, in uint maxRecursionDepth, float tMin, float tMax, bool bCullNonOpaque, bool bCullBackFace)
{
    RadiancePayload rayPayload = (RadiancePayload) 0;

    rayPayload.rayRecursionDepth = currRayRecursionDepth + 1;
    rayPayload.radiance = 0;
	
    if (currRayRecursionDepth >= maxRecursionDepth)
    {
        rayPayload.radiance = float3(0, 0, 0);
        return rayPayload;
    }

	// Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin = tMin;
    rayDesc.TMax = tMax;

    uint rayFlags = 0;
    if (bCullNonOpaque)
    {
        rayFlags |= RAY_FLAG_CULL_NON_OPAQUE;
    }
    if (bCullBackFace)
    {
        rayFlags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
    }
    // TraceRay(Scene, rayFlags, ~0, 0, 2, 0, rayDesc, rayPayload); // radiance
	// TraceRay(Scene, rayFlags, ~0, 1, 2, 1, rayDesc, shadowPayload); // shadow
	
    //void TraceRay(RaytracingAccelerationStructure AccelerationStructure,
    //      uint RayFlags,
    //      uint InstanceInclusionMask,
    //      uint RayContributionToHitGroupIndex,
    //      uint MultiplierForGeometryContributionToHitGroupIndex,  //stride
    //      uint MissShaderIndex,
    //      RayDesc Ray,
    //      inout payload_t Payload);
    TraceRay(Scene, rayFlags, ~0, 0, 2, 0, rayDesc, rayPayload);
    
    return rayPayload;
}

float3 TraceReflectedRay(in float3 hitPosition, in float3 wi, in float3 N, inout RadiancePayload rayPayload, in float tMax, in uint maxRecursionDepth)
{
	// Here we offset ray start along the ray direction instead of surface normal 
	// so that the reflected ray projects to the same screen pixel. 
	// Offsetting by surface normal would result in incorrect mappating in temporally accumulated buffer. 
    float3 offsetAlongRay = T_OFFSET * wi;

    float3 adjustedHitPosition = hitPosition + offsetAlongRay;

    Ray ray = { adjustedHitPosition, wi };

    float tMin = NEAR_PLANE;

    bool bCullNonOpaque = false;
    bool bCullBackFace = true;
		
    rayPayload = TraceRadianceRay(ray, rayPayload.rayRecursionDepth, maxRecursionDepth, tMin, tMax, bCullNonOpaque, bCullBackFace);
    return rayPayload.radiance;
}

float3 TraceRefractedRay(in float3 hitPosition, in float3 wt, in float3 N, inout RadiancePayload rayPayload, in float tMax, in uint maxRecursionDepth)
{
    // Here we offset ray start along the ray direction instead of surface normal 
    // so that the reflected ray projects to the same screen pixel. 
    // Offsetting by surface normal would result in incorrect mappating in temporally accumulated buffer. 
    float3 offsetAlongRay = T_OFFSET * wt;

    float3 adjustedHitPosition = hitPosition + offsetAlongRay;

    Ray ray = { adjustedHitPosition, wt };

    float tMin = NEAR_PLANE;

    // TRADEOFF: Performance vs visual quality
    // Cull transparent surfaces when casting a transmission ray for a transparent surface.
    // Spaceship in particular has multiple layer glass causing a substantial perf hit 
    // with multiple bounces along the way.
    // This can cause visual pop ins however, such as in a case of looking at the spaceship's
    // glass cockpit through a window in the house. The cockpit will be skipped in this case.
    bool bCullNonOpaque = false; // 나뭇잎등은 NonOpaque이므로 이 경우 non opaque 매시를 컬링하면 나뭇잎 모서리 알파 문제가 생긴다.
    bool bCullBackFace = true;
    
    rayPayload = TraceRadianceRay(ray, rayPayload.rayRecursionDepth, maxRecursionDepth, tMin, tMax, bCullNonOpaque, bCullBackFace);
    return rayPayload.radiance;
}

bool TraceShadowRayAndReportIfHit(out float tHit, in Ray ray, in bool bRetrieveTHit, in float tMax)
{
	// Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin = T_OFFSET;
    rayDesc.TMax = tMax;

	// Initialize shadow ray payload.
	// Set the initial value to a hit at TMax. 
	// Miss shader will set it to HitDistanceOnMiss.
	// This way closest and any hit shaders can be skipped if true tHit is not needed. 
    ShadowPayload shadowPayload = { tMax };

    uint rayFlags = 0;
    bool acceptFirstHit = !bRetrieveTHit;
    if (acceptFirstHit)
    {
	    // Performance TIP: Accept first hit if true hit is not neeeded,
	    // or has minimal to no impact. The peformance gain can
	    // be substantial.
        rayFlags |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH; // hit이벤트가 발생하면 그대로 탐색을 종료한다.
        rayFlags |= RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    }

    // Skip closest hit shaders of tHit time is not needed.
    if (!bRetrieveTHit)
    {
        // closest hit shader를 사용하지 않는다. 가깝든 멀든 광원에 사이에 장애물이 있는지만 확인하면 된다. 이는 miss shader에서 확인 가능하다.
        rayFlags |= RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    }
    // rayFlags = 0; // RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

    // TraceRay(Scene, rayFlags, ~0, 0, 2, 0, rayDesc, rayPayload); // radiance
	// TraceRay(Scene, rayFlags, ~0, 1, 2, 1, rayDesc, shadowPayload); // shadow

	//void TraceRay(RaytracingAccelerationStructure AccelerationStructure,
    //      uint RayFlags,
    //      uint InstanceInclusionMask,
    //      uint RayContributionToHitGroupIndex,
    //      uint MultiplierForGeometryContributionToHitGroupIndex,  //stride
    //      uint MissShaderIndex,
    //      RayDesc Ray,
    //      inout payload_t Payload);
    TraceRay(Scene, rayFlags, ~0, 1, 2, 1, rayDesc, shadowPayload);

    tHit = shadowPayload.tHit;

    return shadowPayload.tHit > 0;
}

bool TryTraceShadowRayAndReportIfHit(in float3 hitPosition, in float3 direction, in float3 N, in RadiancePayload rayPayload, in float tMax, in uint maxRecursionDepth)
{
	// hitPosition - ray가 충돌한 점
	// direction - ray가 충돌한 점에서 광원까지의 normalized 방향벡터
	// N - ray가 충돌한 점의 노말벡터
    bool bInShadow = false;
    float dummyTHit;
		
    if (rayPayload.rayRecursionDepth >= maxRecursionDepth)
    {
        return false;
    }
    
    Ray visibilityRay = { hitPosition + T_OFFSET * N, direction };
	
	// Only trace if the surface is facing the target.
    if (dot(visibilityRay.direction, N) <= 0)
    {
        return false;
    }
    
    return TraceShadowRayAndReportIfHit(dummyTHit, visibilityRay, false, tMax);
}

float3 Shade(inout RadiancePayload rayPayload, in float3 N, in float3 hitPosition, in ShadingInfo info)
{
    uint maxRadianceRecursionDepth = g_MaxRadianceRayRecursionDepth;
	
    float3 V = -WorldRayDirection(); // View Vector
    float3 L = 0;
	
    const float3 Kd = info.Kd;
    const float3 Ks = info.Ks;
    const float3 Kr = info.Kr;
    const float3 Kt = info.Kt;
    const float roughness = info.Roughness;
    const float ambientOcclusionStrength = info.AmbientOcclusionStrength;

	// Direct illumination
    if (!IsBlack(Kd) || !IsBlack(Ks))
    {
        for (uint i = 0; i < g_NumLights; i++)
        {
            float3 lightColor = g_LightList[i].Color;
            float3 wi;
            float tMax = g_LightList[i].Rs;
            
            if (g_LightList[i].Type == LIGHT_TYPE_DIRECTIONAL)
            {
                wi = normalize(-g_LightList[i].PosOrDir);
            }
            else /* point */
            {
                wi = normalize(g_LightList[i].PosOrDir - hitPosition);
            }
            
			// Raytraced shadows.
            bool bInShadow = TryTraceShadowRayAndReportIfHit(hitPosition, wi, N, rayPayload, tMax, maxRadianceRecursionDepth);
            // Kd = diffuse , Ks = specular , V = view vector, wi = light vector
            L += BxDF_ShadeDirect(
						Kd,
						Ks,
						lightColor.rgb,
						bInShadow,
						roughness,
						N,
						V,
						wi);
        }
    }
	// Ambient Indirect Illumination
	// Add a default ambient contribution to all hits. 
	// This will be subtracted for hitPositions with 
	// calculated Ambient coefficient in the composition pass.
    L += ambientOcclusionStrength * Kd;

	// Specular Indirect Illumination
    bool bReflective = !IsBlack(Kr);
    bool bTransmissive = !IsBlack(Kt);

	// Handle cases where ray is coming from behind due to imprecision,
	// don't cast reflection rays in that case.
    float smallValue = 1e-6f;
    bReflective = (dot(V, N) > smallValue) ? bReflective : false;
	
    if (bReflective || bTransmissive)
    {
        if (bReflective && (BxDF_IsTotalInternalReflection(V, N)))
        {
            float3 wi = reflect(-V, N);
            RadiancePayload reflectedRayPayload = rayPayload;
            L += Kr * TraceReflectedRay(hitPosition, wi, N, reflectedRayPayload, FAR_PLANE, maxRadianceRecursionDepth);
        }
        else
        {
            float3 Fo = Ks;
            if (bReflective)
            {
				// Radiance contribution from reflection.
                float3 wi;
                float3 Fr = Kr * BxDF_SampleReflectionFr(V, wi, N, Fo); // Calculates wi

                RadiancePayload reflectedRayPayLoad = rayPayload;
				// Ref: eq 24.4, [Ray-tracing from the Ground Up]
                L += Fr * TraceReflectedRay(hitPosition, wi, N, reflectedRayPayLoad, FAR_PLANE, maxRadianceRecursionDepth);
            }
            
            if (bTransmissive)
            {
			    // Radiance contribution from refraction.
                float3 wt;
                float3 Ft = Kt * BxDF_SampleTransmissionFt(V, wt, N, Fo); // Calculates wt

                RadiancePayload refractedRayPayLoad = rayPayload;

                L += Ft * TraceRefractedRay(hitPosition, wt, N, refractedRayPayLoad, FAR_PLANE, maxRadianceRecursionDepth);
            }
        }
    }
    
    return L;
}

// ==========================================================
// SampleSky: single scattering using precomputed LUTs
// ==========================================================
float3 SampleSky(float3 viewDirWorld)
{
    // Planet-centered camera info
    float r = length(g_CameraPosPlanetCoord); // camera radius
    float3 up = GetUp(g_CameraPosPlanetCoord); // local up
    
    // Normalize inputs
    float3 viewDir = normalize(viewDirWorld);
    float3 sunDir = -normalize(g_SunDir);

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
    float4 scat = g_ScatteringLUT.SampleLevel(g_SamplerClamp, uvw, 0);

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

float SunDiskMask(float cosTheta, float radius, float feather)
{
    float rIn = radius;
    float rOut = radius + max(feather, 0.0f);

    float cIn = cos(rIn);
    float cOut = cos(rOut);

    // cosTheta가 cOut→cIn로 갈수록 0→1
    return saturate(smoothstep(cOut, cIn, cosTheta));
}

float SunHaloMask(float cosTheta, float radius, float radiusMul, float feather)
{
    float rIn = radius * max(radiusMul, 1.0f);
    float rOut = rIn + max(feather, 0.0f);

    float cIn = cos(rIn);
    float cOut = cos(rOut);

    // 디스크 바깥쪽에서 부드럽게 깔리는 헤일로
    return saturate(smoothstep(cOut, cIn, cosTheta));
}

static const float SUN_DEFAULT_INTENSITY = 15.0f;
static const float3 SUN_DEFAULT_COLOR = float3(1.0, 0.95, 0.85);
static const float SUN_DEFAULT_RADIUS = 0.004675f; // ~0.267°
static const float SUN_DEFAULT_FEATHER = SUN_DEFAULT_RADIUS * 3.0f;
static const float SUN_DEFAULT_HALO_MUL = 0.15f;
static const float SUN_DEFAULT_HALO_RMUL = 5.0f;

float3 EvaluateSun(float3 dir)
{
    // 파라미터 기본 보정
    float sunI = SUN_DEFAULT_INTENSITY;
    float3 sunCol = SUN_DEFAULT_COLOR;
    float rad = SUN_DEFAULT_RADIUS;
    float fth = SUN_DEFAULT_FEATHER;
    float haloM = SUN_DEFAULT_HALO_MUL;
    float haloRM = SUN_DEFAULT_HALO_RMUL;

    float cosTheta = dot(dir, -normalize(g_SunDir));

    // 디스크(원반) + 헤일로(코로나) 마스크
    float disk = SunDiskMask(cosTheta, rad, fth);
    float halo = SunHaloMask(cosTheta, rad, haloRM, fth) * haloM;

    // 간단한 limb-darkening 느낌을 조금 주고 싶다면:
    // disk *= saturate((cosTheta - cos(rad)) / (1.0 - cos(rad)))^0.25; // 선택사항

    float sunMask = disk + halo;

    // 최종 태양 기여(HDR)
    return sunCol * sunI * sunMask;
}

#endif // RAYTRACING_COMMON_HLSL
