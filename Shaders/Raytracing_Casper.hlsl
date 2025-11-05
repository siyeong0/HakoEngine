#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

struct MyCasperIntersectionAttributes
{
    float3 UnitSpaceHitPosition; // [0,1]^3
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
// Helpers: AABB slab tEnter/tExit (object space)
// --------------------------------------------------
float RayTEnterObj()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float3 inv = 1.0 / rd;

    float3 t0 = (aabb.Min - ro) * inv;
    float3 t1 = (aabb.Max - ro) * inv;
    float3 tmin3 = min(t0, t1);

    float tEnter = max(max(tmin3.x, tmin3.y), tmin3.z);
    return max(tEnter, RayTMin());
}

float RayTExitObj()
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float3 inv = 1.0 / rd;

    float3 t0 = (aabb.Min - ro) * inv;
    float3 t1 = (aabb.Max - ro) * inv;
    float3 tmax3 = max(t0, t1);

    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z);
    return min(tExit, RayTCurrent());
}

// 오브젝트→유닛 공간 변환
void ObjectToUnit(in float3 objP, out float3 unitP, out float3 invExtent)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    invExtent = 1.0 / extent;
    unitP = (objP - aabb.Min) * invExtent;
}

float3 UnitRayDir()
{
    // 오브젝트 공간 레이를 유닛 공간으로 스케일
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 invExtent = 1.0 / (aabb.Max - aabb.Min);
    return ObjectRayDirection() * invExtent;
}

// 유닛 공간 t→오브젝트 공간 t 변환
float UnitTToObjectT(float tUnit, float3 unitOrigin, float3 unitDir)
{
    // 오브젝트 공간: P(t) = ro + tObj * rd
    // 유닛   공간 : U(t) = Uo + tUnit * Ud  (Ud = rd * invExtent)
    // ⇒ tObj = tUnit / |Ud along ray-param|, 그러나 방향 비정규화 주의.
    // 가장 안전한 변환은 유닛공간 히트 좌표를 오브젝트 좌표로 역변환하여 거리 투영.
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;

    float3 unitHit = unitOrigin + unitDir * tUnit; // [0,1]^3
    float3 objHit = aabb.Min + unitHit * extent;

    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();

    // 정사영으로 t 계산 (rd가 정규화가 아닐 수도 있으므로 dot/|rd|^2)
    float tObj = dot(objHit - ro, rd) / dot(rd, rd);
    return tObj;
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

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    // 세팅
    const float EPS = 1e-6f;
    const int MAX_STEPS = 1024;

    float tEnterObj = RayTEnterObj();
    float tExitObj = RayTExitObj();
    if (tEnterObj > tExitObj)
        return;

    // 오브젝트 공간에서 엔트리/엑싯 포인트 → 유닛 공간
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float3 objEnter = ro + tEnterObj * rd;
    float3 objExit = ro + tExitObj * rd;

    float3 uEnter, invExt0;
    float3 uExit, invExt1;
    ObjectToUnit(objEnter, uEnter, invExt0);
    ObjectToUnit(objExit, uExit, invExt1);

    // 유닛 공간 레이
    float3 uRo = uEnter; // 유닛 공간에서 시작은 엔트리에서
    float3 uRd = UnitRayDir();
    float3 uAbs = abs(uRd);

    // 텍스처 해상도에서 베이스 보xel 수를 가져오되, 원하는 목표 그리드가 있으면 여기에 매핑
    uint w, h, mipCount;
    l_DepthAtlasTexture.GetDimensions(0, w, h, mipCount);
    // CASPER의 base grid 해상도 (큐브맵 한 면의 texel 수와 1:1로 가는 게 보통)
    uint N = w; // 필요시 상수/CB 로 교체 가능
    int mipToTest = 0; //    int(mipCount) - 1; // 보수적 시작을 원하면 coarse mip부터, 정확도 우선이면 0

    // 유닛 경계 박스
    float3 boxMin = float3(0, 0, 0);
    float3 boxMax = float3(1, 1, 1);

    // DDA 초기화 ------------------------------------------------
    // 시작점이 경계에 딱 걸리는 경우를 피하기 위해 살짝 전진
    uRo += uRd * EPS;

    // 현재 위치가 속한 보xel 좌표
    int3 cell = int3(clamp((int3) floor(uRo * N), int3(0, 0, 0), int3(N - 1, N - 1, N - 1)));

    // 축별 스텝(+1 or -1)
    int3 step = int3(
        (uRd.x > 0) ? 1 : -1,
        (uRd.y > 0) ? 1 : -1,
        (uRd.z > 0) ? 1 : -1
    );

    // 다음 경계까지의 t(유닛 파라미터)
    float3 cellMin = (float3(cell)) / N;
    float3 cellMax = (float3(cell) + 1.0) / N;

    float txMax = (uRd.x > 0) ? (cellMax.x - uRo.x) / max(uRd.x, 1e-30f)
                              : (cellMin.x - uRo.x) / min(uRd.x, -1e-30f);
    float tyMax = (uRd.y > 0) ? (cellMax.y - uRo.y) / max(uRd.y, 1e-30f)
                              : (cellMin.y - uRo.y) / min(uRd.y, -1e-30f);
    float tzMax = (uRd.z > 0) ? (cellMax.z - uRo.z) / max(uRd.z, 1e-30f)
                              : (cellMin.z - uRo.z) / min(uRd.z, -1e-30f);

    float3 tMax = float3(txMax, tyMax, tzMax);

    // 축별 한 셀을 건널 때 추가될 t
    float3 tDelta = abs((1.0 / N) / max(uAbs, float3(1e-30f, 1e-30f, 1e-30f)));

    // 현재 유닛 파라미터 (uRo는 uEnter에서 시작이므로 tUnit=0)
    float tUnit = 0.0;

    // 먼저 “현재 셀”이 안에 있는지 검사 (레퍼런스의 instantHit)
    //{
    //    float3 uMin = (float3(cell)) / N;
    //    float3 uMax = (float3(cell) + 1.0) / N;
    //    if (IsInsideGeometry(uMin, uMax, mipToTest))
    //    {
    //        // 히트는 엔트리에서 바로 (tUnit=0). Face는 들어온 면을 알아야 하므로,
    //        // 엔트리에서 가장 큰 tEnter 축이 '들어온 면'이다.
    //        float3 roObj = ObjectRayOrigin();
    //        float3 rdObj = ObjectRayDirection();

    //        // Face 판정: 어느 축 경계로 들어왔는가
    //        // slab에서 tEnter를 만든 축을 다시 계산
    //        // 간단히 uEnter가 0 또는 1에 가장 가까운 축을 찾는다.
    //        float3 d0 = abs(uEnter - 0.0);
    //        float3 d1 = abs(1.0 - uEnter);
    //        int axis = 0;
    //        int sign = 0;
    //        float best = 1e9;

    //        // 축별로 “경계에 가장 근접”한 걸 찾는다 (정밀 안정화 목적)
    //        if (min(d0.x, d1.x) < best)
    //        {
    //            best = min(d0.x, d1.x);
    //            axis = 0;
    //            sign = (uRd.x > 0) ? 0 : 1;
    //        }
    //        if (min(d0.y, d1.y) < best)
    //        {
    //            best = min(d0.y, d1.y);
    //            axis = 1;
    //            sign = (uRd.y > 0) ? 2 : 3;
    //        }
    //        if (min(d0.z, d1.z) < best)
    //        {
    //            best = min(d0.z, d1.z);
    //            axis = 2;
    //            sign = (uRd.z > 0) ? 4 : 5;
    //        }

    //        MyCasperIntersectionAttributes attr;
    //        attr.UnitSpaceHitPosition = uRo;
    //        // sign을 0/1로만 쓰고 싶으면 아래 라인처럼 지정:
    //        attr.Face = (axis * 2) + ((uRd[axis] > 0.0f) ? 0 : 1);

    //        // 오브젝트 t로 환산
    //        float tObjHit = UnitTToObjectT(tUnit, uRo, uRd);
    //        ReportHit(tObjHit, /*hitKind*/0, attr);
    //        return;
    //    }
    //}

    // 메인 DDA 루프 ---------------------------------------------
    int lastStepAxis = 2; // 초기값은 임의 (x=0,y=1,z=2), 첫 스텝에서 갱신

    [loop]
    for (int stepCount = 0; stepCount < MAX_STEPS; ++stepCount)
    {
        // 다음 경계까지 중 최소 축 선택
        uint axis;
        float tNext = tMax.x;
        axis = 0;
        if (tMax.y < tNext)
        {
            tNext = tMax.y;
            axis = 1;
        }
        if (tMax.z < tNext)
        {
            tNext = tMax.z;
            axis = 2;
        }

        // 유닛 파라미터 갱신
        tUnit = tNext;

        // 유닛 파라미터를 오브젝트 파라미터로 환산해서 Exit를 넘는지 조기 종료
        float tObjCandidate = UnitTToObjectT(tUnit, uRo, uRd);
        if (tObjCandidate > tExitObj + 1e-6f)
            break;

        // 해당 축으로 한 셀 전진
        if (axis == 0)
        {
            cell.x += step.x;
            tMax.x += tDelta.x;
        }
        else if (axis == 1)
        {
            cell.y += step.y;
            tMax.y += tDelta.y;
        }
        else
        {
            cell.z += step.z;
            tMax.z += tDelta.z;
        }
        lastStepAxis = axis;

        // 그리드 밖이면 종료
        if (any(cell < int3(0, 0, 0)) || any(cell > int3(N - 1, N - 1, N - 1)))
            break;

        // 새로 진입한 셀 검사
        float3 uMin = (float3(cell)) / N;
        float3 uMax = (float3(cell) + 1.0) / N;

        if (IsInsideGeometry(uMin, uMax, mipToTest))
        {
            // 히트 포지션 (유닛 공간)
            float3 uHit = uRo + uRd * tUnit;

            MyCasperIntersectionAttributes attr;
            attr.UnitSpaceHitPosition = uHit;

            // Face: 스텝한 축과 방향으로 결정
            // +X면 = 0, -X = 1, +Y = 2, -Y = 3, +Z = 4, -Z = 5
            int faceBase = lastStepAxis * 2;
            int face = faceBase + ((uRd[lastStepAxis] > 0.0f) ? 0 : 1);
            attr.Face = face;

            // 오브젝트 공간 t
            float tObjHit = UnitTToObjectT(tUnit, uRo, uRd);
            ReportHit(tObjHit, /*hitKind*/0, attr);
            return;
        }
    }

    // 여기까지 오면 히트 없음
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
