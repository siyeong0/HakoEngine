#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

struct MyCasperIntersectionAttributes
{
    float3 UnitSpaceHitPosition; // Hit position in [0,1]^3 inside the AABB
    int Face; // 0:+X,1:-X, 2:+Y,3:-Y, 4:+Z,5:-Z
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

// --------------------------------------------------
// Object-space entry/exit t for the current AABB
// --------------------------------------------------
float RayTEnterObj()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDirection = ObjectRayDirection();
    float3 invDirection = 1.0 / rayDirection;

    float3 t0 = (aabb.Min - rayOrigin) * invDirection;
    float3 t1 = (aabb.Max - rayOrigin) * invDirection;
    float3 tMin3 = min(t0, t1);

    float tEnter = max(max(tMin3.x, tMin3.y), tMin3.z);
    return max(tEnter, RayTMin());
}

float RayTExitObj()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDirection = ObjectRayDirection();
    float3 invDirection = 1.0 / rayDirection;

    float3 t0 = (aabb.Min - rayOrigin) * invDirection;
    float3 t1 = (aabb.Max - rayOrigin) * invDirection;
    float3 tMax3 = max(t0, t1);

    float tExit = min(min(tMax3.x, tMax3.y), tMax3.z);
    return min(tExit, RayTCurrent());
}

// --------------------------------------------------
// Object-space <-> Unit-space helpers
// --------------------------------------------------
void ObjectToUnit(in float3 objectPosition, out float3 unitPosition, out float3 inverseExtent)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    inverseExtent = 1.0 / extent;
    unitPosition = (objectPosition - aabb.Min) * inverseExtent;
}

// Scale object-space ray direction into unit-space
float3 UnitRayDirection()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 inverseExtent = 1.0 / (aabb.Max - aabb.Min);
    return ObjectRayDirection() * inverseExtent;
}

// Convert a unit-space ray parameter tUnit back to object-space t
float UnitTToObjectT(float tUnit, float3 unitRayOrigin, float3 unitRayDirection)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;

    float3 unitHit = unitRayOrigin + unitRayDirection * tUnit;
    float3 objectHit = aabb.Min + unitHit * extent;

    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDirection = ObjectRayDirection();

    // Project hit point onto the original (possibly non-normalized) ray
    float tObject = dot(objectHit - rayOrigin, rayDirection) / dot(rayDirection, rayDirection);
    return tObject;
}

// --------------------------------------------------
// CASPER: sample empty-space (depth) from cube map
// --------------------------------------------------
float GetEmpty(float3 unitPosition, int axis, int sign, int mip)
{
    float3 sampleDirection = unitPosition * 2.0f - 1.0f; // [0,1] -> [-1,1]
    sampleDirection[axis] = sign;
    return l_DepthAtlasTexture.SampleLevel(g_SamplerPoint, normalize(sampleDirection), mip);
}

void GetEmpties(float3 unitPosition, out float outEmpties[6], int mip)
{
    outEmpties[0] = GetEmpty(unitPosition, 0, -1.0, mip);
    outEmpties[1] = GetEmpty(unitPosition, 0, 1.0, mip);
    outEmpties[2] = GetEmpty(unitPosition, 1, -1.0, mip);
    outEmpties[3] = GetEmpty(unitPosition, 1, 1.0, mip);
    outEmpties[4] = GetEmpty(unitPosition, 2, -1.0, mip);
    outEmpties[5] = GetEmpty(unitPosition, 2, 1.0, mip);
}

// For a given sample position, compute valid [min,max] ranges along X/Y/Z
void GetLimits(float3 unitPosition, out float2 outLimits[3], int mip)
{
    float empties[6];
    GetEmpties(unitPosition, empties, mip);
    outLimits[0] = float2(empties[0], 1.0 - empties[1]);
    outLimits[1] = float2(empties[2], 1.0 - empties[3]);
    outLimits[2] = float2(empties[4], 1.0 - empties[5]);
}

// Returns true if the unit-space box [unitMin,unitMax] intersects
// the valid ranges along all three axes at this mip.
bool IsInsideGeometry(float3 unitMin, float3 unitMax, int mip)
{
    float3 samplePosition = (unitMin + unitMax) * 0.5;
    float2 limits[3];
    GetLimits(samplePosition, limits, mip);
    
    bool insideX = (limits[0].x <= unitMax.x && unitMin.x <= limits[0].y);
    bool insideY = (limits[1].x <= unitMax.y && unitMin.y <= limits[1].y);
    bool insideZ = (limits[2].x <= unitMax.z && unitMin.z <= limits[2].y);
    
    return insideX && insideY && insideZ;
}

// =======================================================
// Intersection Shader (unit-space DDA traversal)
// =======================================================

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    const int MAX_STEPS = 2048;
    
    // Object-space entry / exit for the AABB
    float tEnterObject = RayTEnterObj();
    float tExitObject = RayTExitObj();
    
    if (tEnterObject >= tExitObject)
    {
        // No intersection with the AABB
        return;
    }

    float3 rayOriginObject = ObjectRayOrigin();
    float3 rayDirectionObject = ObjectRayDirection();

    float3 objectEnter = rayOriginObject + tEnterObject * rayDirectionObject;
    float3 objectExit = rayOriginObject + tExitObject * rayDirectionObject;

    // Convert entry/exit points into unit-space
    float3 unitEnter, inverseExtent0;
    float3 unitExit, inverseExtent1;
    ObjectToUnit(objectEnter, unitEnter, inverseExtent0);
    ObjectToUnit(objectExit, unitExit, inverseExtent1);

    float3 unitRayOrigin = unitEnter;
    float3 unitRayDirection = UnitRayDirection();
    float3 unitRayDirectionAbs = abs(unitRayDirection);

    // Use base cube-map resolution as the voxel grid resolution
    uint textureWidth, textureHeight, mipCount;
    l_DepthAtlasTexture.GetDimensions(0, textureWidth, textureHeight, mipCount);
    uint gridResolution = textureWidth;
    int mipToTest = 0; // Fine level only in this version
    gridResolution = max(gridResolution >> mipToTest, 1);

    // Slightly nudge start point inside the AABB to avoid boundary issues
    unitRayOrigin += unitRayDirection * EPSILON;

    // Current voxel index in unit-space grid
    int3 cellIndex = int3(
        clamp(
            (int3) floor(unitRayOrigin * gridResolution),
            int3(0, 0, 0),
            int3(gridResolution - 1, gridResolution - 1, gridResolution - 1)
        )
    );
    
    int3 cellStep = int3(
        (unitRayDirection.x > 0.0f) ? 1 : -1,
        (unitRayDirection.y > 0.0f) ? 1 : -1,
        (unitRayDirection.z > 0.0f) ? 1 : -1
    );

    float3 cellMinUnit = (float3(cellIndex)) / gridResolution;
    float3 cellMaxUnit = (float3(cellIndex) + 1.0) / gridResolution;

    float txMax = (unitRayDirection.x > 0.0f)
        ? (cellMaxUnit.x - unitRayOrigin.x) / max(unitRayDirection.x, 1e-30f)
        : (cellMinUnit.x - unitRayOrigin.x) / min(unitRayDirection.x, -1e-30f);

    float tyMax = (unitRayDirection.y > 0.0f)
        ? (cellMaxUnit.y - unitRayOrigin.y) / max(unitRayDirection.y, 1e-30f)
        : (cellMinUnit.y - unitRayOrigin.y) / min(unitRayDirection.y, -1e-30f);

    float tzMax = (unitRayDirection.z > 0.0f)
        ? (cellMaxUnit.z - unitRayOrigin.z) / max(unitRayDirection.z, 1e-30f)
        : (cellMinUnit.z - unitRayOrigin.z) / min(unitRayDirection.z, -1e-30f);

    float3 tMaxPerAxis = float3(txMax, tyMax, tzMax);

    float3 tDeltaPerAxis = abs(
        (1.0 / gridResolution) /
        max(unitRayDirectionAbs, float3(1e-30f, 1e-30f, 1e-30f))
    );

    float tUnit = 0.0f;

    // Check the initial cell immediately (instant-hit case)
    {
        float3 cellUnitMin = (float3(cellIndex)) / gridResolution;
        float3 cellUnitMax = (float3(cellIndex) + 1.0) / gridResolution;

        if (IsInsideGeometry(cellUnitMin, cellUnitMax, mipToTest))
        {
            // Estimate which face we entered from, based on how close unitEnter is to 0/1 on each axis
            float3 distanceToMin = abs(unitEnter - 0.0);
            float3 distanceToMax = abs(1.0 - unitEnter);

            int entryAxis = 0;
            float bestDistance = 1e9;

            float candidate = min(distanceToMin.x, distanceToMax.x);
            if (candidate < bestDistance)
            {
                bestDistance = candidate;
                entryAxis = 0;
            }

            candidate = min(distanceToMin.y, distanceToMax.y);
            if (candidate < bestDistance)
            {
                bestDistance = candidate;
                entryAxis = 1;
            }

            candidate = min(distanceToMin.z, distanceToMax.z);
            if (candidate < bestDistance)
            {
                bestDistance = candidate;
                entryAxis = 2;
            }

            MyCasperIntersectionAttributes attributes;
            attributes.UnitSpaceHitPosition = unitRayOrigin;
            attributes.Face = entryAxis * 2 + ((unitRayDirection[entryAxis] > 0.0f) ? 0 : 1);

            float tObjectHit = UnitTToObjectT(tUnit, unitRayOrigin, unitRayDirection);
            ReportHit(tObjectHit, /*hitKind*/0, attributes);
            return;
        }
    }

    int lastStepAxis = 2;

    // Main DDA traversal in unit-space
    [loop]
    for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex)
    {
        uint stepAxis = 0;
        float tNext = tMaxPerAxis.x;

        if (tMaxPerAxis.y < tNext)
        {
            tNext = tMaxPerAxis.y;
            stepAxis = 1;
        }
        if (tMaxPerAxis.z < tNext)
        {
            tNext = tMaxPerAxis.z;
            stepAxis = 2;
        }

        tUnit = tNext;

        float tObjectCandidate = UnitTToObjectT(tUnit, unitRayOrigin, unitRayDirection);
        if (tObjectCandidate > tExitObject + 1e-6f)
        {
            break;
        }

        if (stepAxis == 0)
        {
            cellIndex.x += cellStep.x;
            tMaxPerAxis.x += tDeltaPerAxis.x;
        }
        else if (stepAxis == 1)
        {
            cellIndex.y += cellStep.y;
            tMaxPerAxis.y += tDeltaPerAxis.y;
        }
        else
        {
            cellIndex.z += cellStep.z;
            tMaxPerAxis.z += tDeltaPerAxis.z;
        }

        lastStepAxis = stepAxis;

        if (any(cellIndex < int3(0, 0, 0)) ||
            any(cellIndex > int3(gridResolution - 1, gridResolution - 1, gridResolution - 1)))
        {
            break;
        }

        float3 cellUnitMin = (float3(cellIndex)) / gridResolution;
        float3 cellUnitMax = (float3(cellIndex) + 1.0) / gridResolution;

        if (IsInsideGeometry(cellUnitMin, cellUnitMax, mipToTest))
        {
            float3 unitHitPosition = unitRayOrigin + unitRayDirection * tUnit;

            MyCasperIntersectionAttributes attributes;
            attributes.UnitSpaceHitPosition = unitHitPosition;

            int faceBase = lastStepAxis * 2;
            int faceIndex = faceBase + ((unitRayDirection[lastStepAxis] > 0.0f) ? 0 : 1);
            attributes.Face = faceIndex;

            float tObjectHit = UnitTToObjectT(tUnit, unitRayOrigin, unitRayDirection);
            ReportHit(tObjectHit, /*hitKind*/0, attributes);
            return;
        }
    }

    // No hit
    return;
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
    localtion[attr.Face / 2] = attr.Face % 2 ? 1.0 : -1.0;
    float4 texDiffuse = l_DiffuseAtlasTexture.SampleLevel(g_SamplerClamp, normalize(localtion), 0);
    
    //float4 texDepth = l_DepthAtlasTexture.SampleLevel(g_SamplerPoint, normalize(localtion), 0);
    //rayPayload.radiance = texDepth.xxx; // Visualize depth for debugging
    //return;
    
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
