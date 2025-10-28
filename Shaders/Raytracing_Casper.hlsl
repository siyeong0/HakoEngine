#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

struct MyCasperIntersectionAttributes
{
    float3 UnitSpaceHitPosition;
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

float RayTEnter()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 origin = ObjectRayOrigin();
    float3 invDir = 1.0 / ObjectRayDirection();
    
    float3 t0 = (aabb.Min - origin) * invDir;
    float3 t1 = (aabb.Max - origin) * invDir;
    float3 tMin3 = min(t0, t1);
    
    return max(max(tMin3.x, tMin3.y), tMin3.z);
}

float RayTExit()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 origin = ObjectRayOrigin();
    float3 invDir = 1.0 / ObjectRayDirection();
    
    float3 t0 = (aabb.Min - origin) * invDir;
    float3 t1 = (aabb.Max - origin) * invDir;
    float3 tMax3 = max(t0, t1);
    
    return min(min(tMax3.x, tMax3.y), tMax3.z);
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

float GetEmpty(float3 unitPos, int axis, int sign)
{
    float3 sampleDir = unitPos * 2.0f - 1.0f; // [0,1] -> [-1,1]
    sampleDir[axis] = sign;
    return l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(sampleDir), 0);
}

void GetEmpties(float3 unitPos, out float outEmpties[6])
{
    outEmpties[0] = GetEmpty(unitPos, 0, -1.0);
    outEmpties[1] = GetEmpty(unitPos, 0, 1.0);
    outEmpties[2] = GetEmpty(unitPos, 1, -1.0);
    outEmpties[3] = GetEmpty(unitPos, 1, 1.0);
    outEmpties[4] = GetEmpty(unitPos, 2, -1.0);
    outEmpties[5] = GetEmpty(unitPos, 2, 1.0);
}

void GetLimits(float3 unitPos, out float2 outLimits[3])
{
    float empties[6];
    GetEmpties(unitPos, empties);
    outLimits[0] = float2(empties[0], 1.0 - empties[1]);
    outLimits[1] = float2(empties[2], 1.0 - empties[3]);
    outLimits[2] = float2(empties[4], 1.0 - empties[5]);
}

bool IsInsideGeometry(float3 unitPos)
{
    float2 limits[3];
    GetLimits(unitPos, limits);
    
    bool bInsideX = (limits[0].x <= unitPos.x) && (unitPos.x <= limits[0].y);
    bool bInsideY = (limits[1].x <= unitPos.y) && (unitPos.y <= limits[1].y);
    bool bInsideZ = (limits[2].x <= unitPos.z) && (unitPos.z <= limits[2].y);
    
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
    const float EPS = 1e-4f;
    const int MAX_STEPS = 512;
    
    float3 rayDir = normalize(UnitRayDirection());
    float3 enter = UnitSpaceHitPosition();
    float3 exit = UnitSpaceExitPosition();
    
    float tEnter = 0.0f;
    float tExit = length(exit - enter) / length(rayDir);
    
    uint width, height, mipCount;
    l_DepthAtlasTexture.GetDimensions(0, width, height, mipCount);
    width = 256;
    float slice = 1.0 / (float) width; // Assert square textures
    
    float3 absDir = abs(rayDir);
    int zax = absDir.x > absDir.y ? (absDir.x > absDir.z ? 0 : 2) : (absDir.y > absDir.z ? 1 : 2);
    int zsign = rayDir[zax] < 0.0 ? 1.0 : -1.0;
    int uax = (zax + 1) % 3;
    int vax = (zax + 2) % 3;
    
    float tCurr = tEnter + EPS;
    float3 currPos = enter; //    +tCurr * rayDir;
    
    for (int stepCount = 0; stepCount < MAX_STEPS && tCurr < tExit; ++stepCount)
    {
        if (IsInsideGeometry(currPos))
        {
            MyCasperIntersectionAttributes attr;
            attr.UnitSpaceHitPosition = currPos * 2.0 - 1.0;
            attr.UnitSpaceHitPosition[zax] = zsign;
            float tReport = ComputeTHit(currPos);
            ReportHit(tReport, /*hitKind*/0, attr);
            return;
        }

        float tStep = 0.0;
        
        float fracU = FracInSlice(currPos[uax], slice);
        float fracV = FracInSlice(currPos[vax], slice);
        float fracZ = FracInSlice(currPos[zax], slice);
        
        float du = rayDir[uax] > 0.0 ? slice - fracU : fracU;
        float dv = rayDir[vax] > 0.0 ? slice - fracV : fracV;
        float dz = rayDir[zax] > 0.0 ? slice - fracZ : fracZ;
        
        float tu = du / max(absDir[uax], EPS);
        float tv = dv / max(absDir[vax], EPS);
        float tz = dz / max(absDir[zax], EPS);
        float tuvz = min(min(tu, tv), tz);
        
        float emptySpace = GetEmpty(currPos, zax, zsign);
        float reamin = (zsign > 0) ? (emptySpace - (1.0 - currPos[zax])) : (emptySpace - currPos[zax]);
        float te = reamin / absDir[zax];
        
            // tStep = tz < 0.0 ? tuv : min(tuv, tz);
        tStep = te < EPS ? tuvz : min(tuvz, te);
        
        tCurr += tStep + EPS;
        currPos = enter + tCurr * rayDir;
    }
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
//        if (IsInsideGeometry(currPos))
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
    
    float3 hitObjectPosition = ObjectRayOrigin() + RayTCurrent() * ObjectRayDirection();
    float3 localtion = attr.UnitSpaceHitPosition;
    float4 texDiffuse = l_DiffuseAtlasTexture.SampleLevel(g_SamplerClamp, localtion, 0);
    float texDepth = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, localtion, 0);
    
    float tt = RayTCurrent() / 25.0;
    rayPayload.radiance = (tt * tt).xxx;
    return;
    
    rayPayload.radiance = texDiffuse.xyz;
    return;
    
    float3 objectNormal = float3(0, 0, 1); // TODO: Compute proper normal 
    float3 surfaceNormal = normalize(mul((float3x3) ObjectToWorld3x4(), objectNormal));
    
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
