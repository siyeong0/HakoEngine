#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#include "Raytracing_common.hlsl"
#include "BxDF.hlsli"

static const float T_OFFSET = 0.01;

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

bool TraceShadowRayAndReportIfHit(out float tHit, in Ray ray, in bool retrieveTHit, in float tMax)
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

    uint rayFlags = RAY_FLAG_CULL_NON_OPAQUE; // ~skip transparent objects
    bool acceptFirstHit = !retrieveTHit;
    if (acceptFirstHit)
    {
	// Performance TIP: Accept first hit if true hit is not neeeded,
	// or has minimal to no impact. The peformance gain can
	// be substantial.
        rayFlags |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH; // hit이벤트가 발생하면 그대로 탐색을 종료한다.
    }

    // Skip closest hit shaders of tHit time is not needed.
    if (!retrieveTHit)
    {
        // closest hit shader를 사용하지 않는다. 가깝든 멀든 광원에 사이에 장애물이 있는지만 확인하면 된다. 이는 miss shader에서 확인 가능하다.
        rayFlags |= RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    }
    rayFlags = 0; // RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

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

[shader("raygeneration")]
void MyRaygenShader_RadianceRay()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    // Ray direction is computed by transforming the screen space coordinate to world space.
    float2 currPixel = float2(launchIndex.xy) + 0.5f; // center in the middle of the pixel.
    float2 resolution = float2(launchDim.xy);
    
    // Generate primary ray.
    float4 rayView = float4(
        ((currPixel.x / resolution.x) * 2.0f - 1.0f) / g_Proj._11,
        -((currPixel.y / resolution.y) * 2.0f - 1.0f) / g_Proj._22,
        1.0, 0.0);
    float4 rayDstWorld = mul(rayView, g_InvView);
    rayDstWorld.xyz = normalize(rayDstWorld.xyz);
    float3 worldDir = rayDstWorld.xyz;
    float3 worldOrigin = g_InvView._41_42_43; // Ray origin is the camera position.
    
    Ray ray =
    {
        worldOrigin,
		worldDir
    };
    
    // Trace primary ray.
    uint currRayRecursionDepth = 0;
    bool bCullNonOpaque = false;
    bool bCullBackFace = false;
    RadiancePayload rayPayload = TraceRadianceRay(ray, currRayRecursionDepth, g_MaxRadianceRayRecursionDepth, NEAR_PLANE, FAR_PLANE, bCullNonOpaque, bCullBackFace);
	
    // Output to the screen.
    g_OutputDiffuse[launchIndex.xy] = float4(rayPayload.radiance, 1);
    g_OutputDepth[launchIndex.xy] = rayPayload.depth;
}

[shader("closesthit")]
void MyClosestHitShader_RadianceRay(inout RadiancePayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    uint instanceID = InstanceID(); // The instance ID as specified in the instance desc.
    uint systemInstanceIndex = InstanceIndex(); // The autogenerated index of the current instance in the top-level structure.
    
    uint baseIndex = PrimitiveIndex() * g_TriangleIndexStride; // The base index of the hit triangle's first 16 bit index.
    uint3 indices = Load3x16BitIndices(baseIndex);
    
    // Get vertex attributes.
    float2 vertexUV[3] =
    {
        l_Vertices[indices[0]].UV,
        l_Vertices[indices[1]].UV,
        l_Vertices[indices[2]].UV
    };
    float3 vertexNormal[3] =
    {
        l_Vertices[indices[0]].Normal,
        l_Vertices[indices[1]].Normal,
        l_Vertices[indices[2]].Normal
    };
    float3 vertexTangent[3] =
    {
        l_Vertices[indices[0]].Tangent,
        l_Vertices[indices[1]].Tangent,
        l_Vertices[indices[2]].Tangent
    };
    
    float2 texCoord = HitAttribute(vertexUV, attr);
    
    float4 texDiffuse = l_DiffuseTexture.SampleLevel(g_SamplerPoint, texCoord, 0);
    float3 texNormal = l_NormalTexture.SampleLevel(g_SamplerWrap, texCoord, 0).rgb;
    // float3 texNormal = float3(0.5, 0.5, 1.0);
    
    float3 localNormal = HitAttribute(vertexNormal, attr);
    float3 localTangent = HitAttribute(vertexTangent, attr);
    
    float3 worldNormal = normalize(mul(localNormal, (float3x3) ObjectToWorld4x3())); // TODO: like Standard.hlsl. use InverseTranspose?
    float3 worldTangent = normalize(mul(localTangent, (float3x3) ObjectToWorld4x3()));
    float3 worldBinormal = normalize(cross(worldTangent, worldNormal));
    
    float3 tanNormal = texNormal.rgb * 2.0 - 1.0;
    tanNormal.xy *= l_RayGeomCB.Material.NormalScale;
    tanNormal = normalize(tanNormal);
    
    float3 surfaceNormal = (tanNormal.xxx * worldTangent) + (tanNormal.yyy * worldBinormal) + (tanNormal.zzz * worldNormal);
    
    // Compute depth
    float4 projPos = mul(float4(hitPosition, 1.0), g_ViewProj);
    projPos /= projPos.w;
    rayPayload.depth = saturate(projPos.z);
    
    // Compute radiance
    BasicMaterial mtl = l_RayGeomCB.Material;
    float3 baseColor = mtl.BaseColor * texDiffuse.rgb;
    float metallic = mtl.MetallicFactor;
    float roughness = max(0.04f, saturate(mtl.RoughnessFactor));
    
    ShadingInfo info;
    info.Kd = (1.0 - mtl.MetallicFactor) * baseColor;
    info.Ks = saturate(lerp(mtl.SpecularColor, baseColor, metallic) * mtl.SpecularFactor);
    info.Kr = (metallic > 0.5) ? normalize(max(baseColor, float3(1e-4, 1e-4, 1e-4))) : float3(1.0, 1.0, 1.0);
    //info.Kr = float3(mtl.SpecularFactor, mtl.SpecularFactor, mtl.SpecularFactor);
    info.Kt = (1.0f - saturate(mtl.Opacity)) * baseColor;
    info.Roughness = roughness;
    info.AmbientOcclusionStrength = mtl.AmbientOcclusionStrength;
    
    rayPayload.radiance = Shade(rayPayload, surfaceNormal, hitPosition, info);
}

[shader("miss")]
void MyMissShader_RadianceRay(inout RadiancePayload rayPayload)
{
    float3 L = SampleSky(normalize(WorldRayDirection()));
    
    rayPayload.radiance = L;
    // rayPayload.radiance = float3(0, 0, 0);
    rayPayload.depth = 1.2;
}

[shader("anyhit")]
void MyAnyHitShader_RadianceRay(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    uint instanceID = InstanceID(); // The instance ID as specified in the instance desc.
    uint systemInstanceIndex = InstanceIndex(); // The autogenerated index of the current instance in the top-level structure.
    
    uint baseIndex = PrimitiveIndex() * g_TriangleIndexStride; // The base index of the hit triangle's first 16 bit index.
    
    // Load up 3 16 bit indices for the triangle.
    uint3 indices = Load3x16BitIndices(baseIndex);
    float2 texCoord[3] =
    {
        l_Vertices[indices[0]].UV,
        l_Vertices[indices[1]].UV,
        l_Vertices[indices[2]].UV
    };
    float2 currTexCoord = HitAttribute(texCoord, attr);
    float4 texDiffuse = l_DiffuseTexture.SampleLevel(g_SamplerPoint, currTexCoord, 0);
    float alpha = texDiffuse.a;
    
    if (alpha < ALPHA_TEST_THRESHOLD)
    {
        IgnoreHit();
    }
}

[shader("closesthit")]
void MyClosestHitShader_ShadowRay(inout ShadowPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Never called if RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH is set.
    rayPayload.tHit = RayTCurrent();
}
	
[shader("miss")]
void MyMissShader_ShadowRay(inout ShadowPayload rayPayload)
{
    // hit이벤트가 발생하지 않는다면(충돌하는 매시가 없다면 rayPayload.tHit = 0이 된다.
    rayPayload.tHit = HitDistanceOnMiss;
}

[shader("anyhit")]
void MyAnyHitShader_ShadowRay(inout ShadowPayload rayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

	// Get the base index of the triangle's first 16 bit index.
    uint instanceID = InstanceID();
    uint systemInstanceIndex = InstanceIndex(); // The autogenerated index of the current instance in the top-level structure.
    
    uint baseIndex = PrimitiveIndex() * g_TriangleIndexStride; // The base index of the hit triangle's first 16 bit index.
    
    // Load up 3 16 bit indices for the triangle.
    uint3 indices = Load3x16BitIndices(baseIndex);
    float2 texCoord[3] =
    {
        l_Vertices[indices[0]].UV,
        l_Vertices[indices[1]].UV,
        l_Vertices[indices[2]].UV
    };
    float2 currTexCoord = HitAttribute(texCoord, attr);
    float4 texDiffuse = l_DiffuseTexture.SampleLevel(g_SamplerPoint, currTexCoord, 0);
    float alpha = texDiffuse.a;
    
    if (alpha < ALPHA_TEST_THRESHOLD)
    {
        IgnoreHit();
    }
    else
    {
        // Accept the hit and end search.
        // This is useful for alpha tested geometry.
        // It avoids invoking closest hit shader.
        // rayPayload.tHit = RayTCurrent();
        AcceptHitAndEndSearch();
    }
}
#endif // RAYTRACING_HLSL
