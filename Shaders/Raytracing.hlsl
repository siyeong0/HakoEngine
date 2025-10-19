#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#include "Raytracing_common.hlsl"
#include "BxDF.hlsli"

RadiancePayload TraceRadianceRay(in Ray ray, in uint currRayRecursionDepth, in uint maxRecursionDepth, float tMin, float tMax, bool bCullNonOpaque, bool bCullBackFace)
{
    RadiancePayload rayPayload = (RadiancePayload) 0;

    rayPayload.rayRecursionDepth = currRayRecursionDepth + 1;
    rayPayload.radiance = 0;
	
    if (currRayRecursionDepth >= maxRecursionDepth)
    {
        rayPayload.radiance = float3(1, 1, 1);
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
    TraceRay(Scene, rayFlags, ~0, 0, 1, 0, rayDesc, rayPayload);

    return rayPayload;
}

float3 TraceReflectedRay(in float3 hitPosition, in float3 wi, in float3 N, inout RadiancePayload rayPayload, in float tMax, in uint maxRecursionDepth)
{
	// Here we offset ray start along the ray direction instead of surface normal 
	// so that the reflected ray projects to the same screen pixel. 
	// Offsetting by surface normal would result in incorrect mappating in temporally accumulated buffer. 
    const float T_OFFSET = 0.01;
    float3 offsetAlongRay = T_OFFSET * wi;

    float3 adjustedHitPosition = hitPosition + offsetAlongRay;

    Ray ray = { adjustedHitPosition, wi };

    float tMin = NEAR_PLANE;

    bool bCullNonOpaque = false;
    bool bCullBackFace = true;
		
    rayPayload = TraceRadianceRay(ray, rayPayload.rayRecursionDepth, maxRecursionDepth, tMin, tMax, bCullNonOpaque, bCullBackFace);
    return rayPayload.radiance;
}

float3 Shade(inout RadiancePayload rayPayload, in float3 N, in float3 hitPosition, in ShadingMaterial material)
{
    uint MaxRadianceRecursionDepth = g_MaxRadianceRayRecursionDepth;
	
    float3 V = -WorldRayDirection(); // View Vector
    float3 L = 0;
	
    const float3 Kd = material.Kd;
    const float3 Ks = material.Ks;
    const float3 Kr = material.Kr;
    const float3 Kt = material.Kt;
    const float roughness = material.Roughness;

	// Direct illumination
    if (!BxDF::IsBlack(material.Kd) || !BxDF::IsBlack(material.Ks))
    {
        for (uint i = 0; i < g_NumLights; i++)
        {
            float3 lightColor = g_LightList[i].Color;
            float3 wi;
            if (g_LightList[i].Type == LIGHT_TYPE_DIRECTIONAL)
            {
                wi = normalize(-g_LightList[i].PosOrDir);
            }
            else /* point */
            {
                wi = normalize(g_LightList[i].PosOrDir - HitWorldPosition());
            }
            
			// Raytraced shadows.
            bool isInShadow = false;
            // Kd = diffuse , Ks = specular , V = view vector, wi = light vector
            L += BxDF::DirectLighting::Shade(
						material.Type,
						Kd,
						Ks,
						lightColor.rgb,
						isInShadow,
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
    L += material.AmbientIntensity * Kd;

	// Specular Indirect Illumination
    bool bReflective = !BxDF::IsBlack(Kr);
    bool bTransmissive = !BxDF::IsBlack(Kt);

	// Handle cases where ray is coming from behind due to imprecision,
	// don't cast reflection rays in that case.
    float smallValue = 1e-6f;
    bReflective = dot(V, N) > smallValue ? bReflective : false;
	
    if (bReflective || bTransmissive)
    {
        if (bReflective && (BxDF::Specular::Reflection::IsTotalInternalReflection(V, N)))
        {
            float3 wi = reflect(-V, N);
            RadiancePayload reflectedRayPayload = rayPayload;
            L += Kr * TraceReflectedRay(hitPosition, wi, N, reflectedRayPayload, FAR_PLANE, MaxRadianceRecursionDepth);
        }
        else
        {
            float3 Fo = Ks;
            if (bReflective)
            {
				// Radiance contribution from reflection.
                float3 wi;
                float3 Fr = Kr * BxDF::Specular::Reflection::Sample_Fr(V, wi, N, Fo); // Calculates wi

                RadiancePayload reflectedRayPayLoad = rayPayload;
				// Ref: eq 24.4, [Ray-tracing from the Ground Up]
                L += Fr * TraceReflectedRay(hitPosition, wi, N, reflectedRayPayLoad, FAR_PLANE, MaxRadianceRecursionDepth);
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
    // float3 texNormal = l_NormalTexture.SampleLevel(g_SamplerWrap, texCoord, 0).rgb;
    float3 texNormal = float3(0.5, 0.5, 1.0);
    
    float3 localNormal = HitAttribute(vertexNormal, attr);
    float3 localTangent = HitAttribute(vertexTangent, attr);
    
    float3 worldNormal = normalize(mul(localNormal, (float3x3) ObjectToWorld4x3())); // TODO: like Standard.hlsl. use InverseTranspose?
    float3 worldTangent = normalize(mul(localTangent, (float3x3) ObjectToWorld4x3()));
    float3 worldBinormal = normalize(cross(worldTangent, worldNormal));
    
    float3 tanNormal = texNormal.rgb * 2.0 - 1.0;
    float3 surfaceNormal = (tanNormal.xxx * worldTangent) + (tanNormal.yyy * worldBinormal) + (tanNormal.zzz * worldNormal);
    
    // Compute depth
    float4 projPos = mul(float4(hitPosition, 1.0), g_ViewProj);
    projPos /= projPos.w;
    rayPayload.depth = saturate(projPos.z);
    
    // Compute radiance
    ShadingMaterial material;
    material.Kd = texDiffuse.rgb * l_RayGeomCB.Material.Opacity;
    material.Type = l_RayGeomCB.Material.Type;
    material.Ks = l_RayGeomCB.Material.Ks;
    material.Roughness = l_RayGeomCB.Material.Roughness;
    material.Kr = l_RayGeomCB.Material.Kr;
    material.AmbientIntensity = l_RayGeomCB.Material.AmbientIntensity;
    material.Kt = l_RayGeomCB.Material.Kt;
   
    rayPayload.radiance = Shade(rayPayload, surfaceNormal, hitPosition, material);
}

[shader("miss")]
void MyMissShader_RadianceRay(inout RadiancePayload rayPayload)
{
    // TODO: sky diffuse
    rayPayload.radiance = float3(0, 0, 0);
    rayPayload.depth = 1.2;
}

[shader("anyhit")]
void MyAnyHitShader_RadianceRay(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    uint instanceID = InstanceID(); // The instance ID as specified in the instance desc.
    uint systemInstanceIndex = InstanceIndex(); // The autogenerated index of the current instance in the top-level structure.
}
#endif // RAYTRACING_HLSL
