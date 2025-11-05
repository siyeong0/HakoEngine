#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

struct MyCasperIntersectionAttributes
{
    float3 UnitSpaceHitPosition;
    int Face;
};

ConstantBuffer<CONSTANT_BUFFER_RT_PROC> l_ProcGeomCB : register(b1, space0);
TextureCube<float4> l_DiffuseAtlasTexture : register(t0, space1);
TextureCube<float> l_DepthAtlasTexture : register(t1, space1);

struct AABB
{
    float3 Min;
    float3 Max;
};
StructuredBuffer<AABB> l_AABBBuffer : register(t2, space1);

// =======================================================
// Unit-space helpers
// =======================================================

float3 UnitToObject(float3 unitPos)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    return aabb.Min + unitPos * extent;
}

float RayTEnter()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float3 inv = 1.0 / rd;

    float3 t0 = (aabb.Min - ro) * inv;
    float3 t1 = (aabb.Max - ro) * inv;
    float3 tmin3 = min(t0, t1);

    float tEnter = max(max(tmin3.x, tmin3.y), tmin3.z);
    tEnter = max(tEnter, RayTMin());
    return tEnter;
}

float RayTExit()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float3 inv = 1.0 / rd;

    float3 t0 = (aabb.Min - ro) * inv;
    float3 t1 = (aabb.Max - ro) * inv;
    float3 tmax3 = max(t0, t1);

    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z);
    tExit = min(tExit, RayTCurrent());
    return tExit;
}

float3 UnitRayDirection()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    float3 invExtent = 1.0 / extent;
    return ObjectRayDirection() * invExtent;
}

float3 UnitSpaceHitPosition()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 objectHitPosition = ObjectRayOrigin() + RayTEnter() * ObjectRayDirection();
    float3 extent = aabb.Max - aabb.Min;
    float3 invExtent = 1.0 / extent;
    return (objectHitPosition - aabb.Min) * invExtent;
}

float3 UnitSpaceExitPosition()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 objectExitPosition = ObjectRayOrigin() + RayTExit() * ObjectRayDirection();
    float3 extent = aabb.Max - aabb.Min;
    float3 invExtent = 1.0 / extent;
    return (objectExitPosition - aabb.Min) * invExtent;
}

float ComputeTHit(float3 unitSpacePos)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    float3 objectSpacePos = aabb.Min + unitSpacePos * extent;
    
    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDir = ObjectRayDirection();
    
    float3 toPos = objectSpacePos - rayOrigin;
    float tHit = dot(toPos, rayDir) / dot(rayDir, rayDir);
    return tHit;
}

float GetEmpty(float3 unitPos, int axis, int sign, int mip)
{
    float3 sampleDir = unitPos * 2.0f - 1.0f; // [0,1] -> [-1,1]
    sampleDir[axis] = sign;
    return l_DepthAtlasTexture.SampleLevel(g_SamplerPoint, normalize(sampleDir), mip);
}

void GetEmpties(float3 unitPos, out float outEmpties[6], int mip)
{
    outEmpties[0] = GetEmpty(unitPos, 0, -1.0, mip);
    outEmpties[1] = GetEmpty(unitPos, 0, 1.0, mip);
    outEmpties[2] = GetEmpty(unitPos, 1, -1.0, mip);
    outEmpties[3] = GetEmpty(unitPos, 1, 1.0, mip);
    outEmpties[4] = GetEmpty(unitPos, 2, -1.0, mip);
    outEmpties[5] = GetEmpty(unitPos, 2, 1.0, mip);
}

void GetLimits(float3 unitPos, out float2 outLimits[3], int mip)
{
    float empties[6];
    GetEmpties(unitPos, empties, mip);
    outLimits[0] = float2(empties[0], 1.0 - empties[1]);
    outLimits[1] = float2(empties[2], 1.0 - empties[3]);
    outLimits[2] = float2(empties[4], 1.0 - empties[5]);
}

bool IsInsideGeometry(float3 unitMin, float3 unitMax, int mip)
{
    float3 samplePos = (unitMin + unitMax) * 0.5;
    float2 limits[3];
    GetLimits(samplePos, limits, mip);
    
    bool bInsideX = (limits[0].x <= unitMax.x && unitMin.x <= limits[0].y);
    bool bInsideY = (limits[1].x <= unitMax.y && unitMin.y <= limits[1].y);
    bool bInsideZ = (limits[2].x <= unitMax.z && unitMin.z <= limits[2].y);
    
    return bInsideX && bInsideY && bInsideZ;
}

// =======================================================
// Intersection Shader
// =======================================================

float FracInSlice(float x, float slice)
{
    float q = x / slice;
    return (q - floor(q)) * slice;
}

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    const float EPS = 1e-6f;
    const int MAX_STEPS = 256;
    
    float3 rayDir = normalize(UnitRayDirection());
    float3 enter = UnitSpaceHitPosition();
    float3 exit = UnitSpaceExitPosition();

    float tEnter = 0.0f;
    float tExit = length(exit - enter) / length(rayDir);
    
    uint width, height, mipCount;
    l_DepthAtlasTexture.GetDimensions(0, width, height, mipCount);
    uint baseDim = width; // Limit max resolution to 256x256 for performance.
    
    float3 absDir = abs(rayDir);
    int zax = absDir.x > absDir.y ? (absDir.x > absDir.z ? 0 : 2) : (absDir.y > absDir.z ? 1 : 2);
    int zsign = rayDir[zax] < 0.0 ? 1.0 : -1.0;
    int uax = (zax + 1) % 3;
    int vax = (zax + 2) % 3;
    
    int taxis = zax;
    float3 fracs = 0.xxx;
    float tCurr = tEnter + EPS;
    float3 currPos = enter + tCurr * rayDir; // +tCurr * rayDir;
    float tPrev = tCurr;
    float3 prevPos = currPos;
    for (int mip = mipCount - 1; mip >= 0; --mip)
    {
        uint dimL = max(1u, baseDim >> mip); // Assert square textures
        float slice = 1.0 / dimL;
        
        for (int stepCount = 0; stepCount < MAX_STEPS && tPrev < tExit; ++stepCount)
        {
            fracs[uax] = FracInSlice(currPos[uax], slice);
            fracs[vax] = FracInSlice(currPos[vax], slice);
            fracs[zax] = FracInSlice(currPos[zax], slice);
            
            float3 uintMin = currPos - fracs;
            float3 unitMax = uintMin + slice.xxx;
            if (IsInsideGeometry(uintMin, unitMax, mip))
            {
                break;
            }
            
            float tStep = 0.0;
     
            float du = rayDir[uax] > 0.0 ? slice - fracs[uax] : fracs[uax];
            float dv = rayDir[vax] > 0.0 ? slice - fracs[vax] : fracs[vax];
            float dz = rayDir[zax] > 0.0 ? slice - fracs[zax] : fracs[zax];
        
            float tu = du / max(absDir[uax], EPS);
            float tv = dv / max(absDir[vax], EPS);
            float tz = dz / max(absDir[zax], EPS);
            
            taxis = tu < tv ? (tu < tz ? uax : zax) : (tv < tz ? vax : zax);
            float tuvz = min(min(tu, tv), tz);
        
            //float emptySpace = GetEmpty(currPos, zax, zsign, mip);
            //float reamin = (zsign > 0) ? (emptySpace - (1.0 - currPos[zax])) : (emptySpace - currPos[zax]);
            //float te = reamin / absDir[zax];
        
            //// tStep = tz < 0.0 ? tuv : min(tuv, tz);
            //tStep = te < EPS ? tuvz : min(tuvz, te);
        
            tStep = tuvz;
            
            tPrev = tCurr;
            prevPos = currPos;
            
            tCurr += tStep + EPS;
            currPos = enter + tCurr * rayDir;
        }
        
        tCurr = tPrev;
        currPos = prevPos;
    }
    
    if (tCurr >= tExit)
    {
        return;
    }
    
    float3 hitPos = enter + (tCurr - EPS) * rayDir;
    
    MyCasperIntersectionAttributes attr;
    attr.UnitSpaceHitPosition = currPos;
    attr.Face = taxis * 2 + ((rayDir[taxis] > 0.0) ? 0 : 1);
    float tReport = ComputeTHit(currPos);
    ReportHit(tReport, /*hitKind*/0, attr);
}

//[shader("intersection")]
//void MyIntersectionShader_Casper()
//{
//    const int MAX_STEPS = 128;
    
//    float3 rayDir = UnitRayDirection();
//    float3 enter = UnitSpaceHitPosition();
//    float3 exit = UnitSpaceExitPosition();
    
//    float tEnter = 0.0f;
//    float tExit = length(exit - enter) / length(rayDir);
    
//    float tCurr = tEnter;
//    float3 currPos = enter;
//    float dt = (tExit - tEnter) / MAX_STEPS;
//    for (int stepCount = 0; stepCount < MAX_STEPS; ++stepCount)
//    {
//        if (IsInsideGeometry(currPos, 0))
//        {
//            MyCasperIntersectionAttributes attr;
//            attr.UnitSpaceHitPosition = currPos;
//            float tHit = ComputeTHit(currPos);
//            ReportHit(tHit, /*hitKind*/0, attr);
//        }
//        tCurr += dt;
//        currPos = enter + tCurr * normalize(rayDir);
//    }
//}

[shader("closesthit")]
void MyClosestHitShader_RadianceRay_Casper(inout RadiancePayload rayPayload, in MyCasperIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    uint instanceID = InstanceID(); // The instance ID as specified in the instance desc.
    uint systemInstanceIndex = InstanceIndex(); // The autogenerated index of the current instance in the top-level structure.
    
    // Compute depth
    float4 projPos = mul(float4(hitPosition, 1.0), g_ViewProj);
    projPos /= projPos.w;
    rayPayload.depth = saturate(projPos.z);
    
    float3 localtion = attr.UnitSpaceHitPosition * 2.0 - 1.0;
    localtion[attr.Face / 2] = attr.Face % 2 ? -1.0 : 1.0;
    float4 texDiffuse = l_DiffuseAtlasTexture.SampleLevel(g_SamplerClamp, normalize(localtion), 0);
    
    texDiffuse = float4(1.0, 1.0, 1.0, 1.0); // Disable texture for debugging
    
    
    int zax = attr.Face / 2;
    //int uax = (zax + 1) % 3;
    //int vax = (zax + 2) % 3;
    
    //float offset = 1.0 / 512.0; // Hardcoded base dimension
    //float3 offset0 = 0.xxx;
    //offset0[uax] = -offset;
    //offset0[vax] = -offset;
    //float3 offset1 = 0.xxx;
    //offset1[uax] = +offset;
    //offset1[vax] = -offset;
    //float3 offset2 = 0.xxx;
    //offset2[uax] = -offset;
    //offset2[vax] = +offset;
    
    //float3 pn0 = attr.UnitSpaceHitPosition + offset0;
    //float3 loc0 = pn0 * 2.0 - 1.0;
    //loc0[zax] = attr.Face % 2 ? -1.0 : 1.0;
    //float d0 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc0), 0);
    //pn0[zax] = attr.Face % 2 ? d0 : (1.0 - d0);
    //pn0 = UnitToObject(pn0);
    
    //float3 pn1 = attr.UnitSpaceHitPosition + offset1;
    //float3 loc1 = pn1 * 2.0 - 1.0;
    //loc1[zax] = attr.Face % 2 ? -1.0 : 1.0;
    //float d1 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc1), 0);
    //pn1[zax] = attr.Face % 2 ? d1 : (1.0 - d1);
    //pn1 = UnitToObject(pn1);
    
    //float3 pn2 = attr.UnitSpaceHitPosition + offset2;
    //float3 loc2 = pn2 * 2.0 - 1.0;
    //loc2[zax] = attr.Face % 2 ? -1.0 : 1.0;
    //float d2 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc2), 0);
    //pn2[zax] = attr.Face % 2 ? d2 : (1.0 - d2);
    //pn2 = UnitToObject(pn2);
    
    //float3 v1 = normalize(pn1 - pn0);
    //float3 v2 = normalize(pn2 - pn0);
    
    //float3 objectNormal = attr.Face % 2 ? normalize(cross(v2, v1)) : normalize(cross(v1, v2));
    //float3 surfaceNormal = normalize(mul(objectNormal, (float3x3) ObjectToWorld4x3()));

    //rayPayload.radiance = 0.5 * (objectNormal + 1.0);
    //return;
    
    float3 objectNormal = 0.xxx;
    objectNormal[zax] = attr.Face % 2 ? 1.0 : -1.0;
    float3 surfaceNormal = normalize(mul(objectNormal, (float3x3) ObjectToWorld4x3()));
    
    // Compute radiance
    BasicMaterial mtl = l_ProcGeomCB.Material;
    float3 baseColor = mtl.BaseColor * texDiffuse.xyz;
    float metallic = mtl.MetallicFactor;
    float roughness = max(0.04f, saturate(mtl.RoughnessFactor));
    
    ShadingInfo info;
    info.Kd = (1.0 - mtl.MetallicFactor) * baseColor;
    info.Ks = saturate(lerp(mtl.SpecularColor, baseColor, metallic) * mtl.SpecularFactor);
    info.Kr = (metallic > 0.5) ? baseColor : 1.0.xxx;
    //info.Kr = float3(mtl.SpecularFactor, mtl.SpecularFactor, mtl.SpecularFactor);
    info.Kt = (1.0f - saturate(mtl.Opacity)) * baseColor;
    info.Roughness = roughness;
    info.AmbientOcclusionStrength = mtl.AmbientOcclusionStrength;
    
    rayPayload.radiance = Shade(rayPayload, surfaceNormal, hitPosition, info);
}

[shader("closesthit")]
void MyClosestHitShader_ShadowRay_Casper(inout ShadowPayload rayPayload, in MyCasperIntersectionAttributes attr)
{
    // Never called if RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH is set.
    rayPayload.tHit = RayTCurrent();
}

#endif // RAYTRACING_HLSL
