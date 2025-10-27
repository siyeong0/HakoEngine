#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

struct MyCasperIntersectionAttributes
{
    float3 Normal;
};

ConstantBuffer<CONSTANT_BUFFER_RT_PROC> l_ProcGeomCB : register(b1, space0);
TextureCube<float4> l_DiffuseAtlasTexture : register(t0, space1);
TextureCube<float> l_DepthAtlasTexture : register(t1, space1);

// --------- 유틸: 레이 vs AABB slab 교차 (object space) ----------
bool RayBoxTSlab(float3 ro, float3 rd, float3 bmin, float3 bmax, out float tEnter, out float tExit)
{
    // rd 성분 0이어도 1/0=INF 로 min/max가 정리해줌
    float3 inv = 1.0 / rd;
    float3 t0 = (bmin - ro) * inv;
    float3 t1 = (bmax - ro) * inv;

    float3 tmin3 = min(t0, t1);
    float3 tmax3 = max(t0, t1);

    float tmin = max(max(tmin3.x, tmin3.y), tmin3.z);
    float tmax = min(min(tmax3.x, tmax3.y), tmax3.z);

    // 레이 시작 클램프 (다른 히트가 앞서 있으면 RayTMin가 올라있을 수 있음)
    tEnter = max(tmin, RayTMin());
    tExit = tmax;
    return (tEnter <= tExit);
}

// --------- 유틸: 0..1 depth → [-1,1] 좌표 한계로 복원 ----------
float2 DecodeAxisLimits(float3 pOS, int axis)
{
    // pOS의 (u,v)에 해당하는 +axis, -axis 얼굴을 샘플
    float3 dirP = pOS;
    dirP[axis] = 1.0;
    dirP = normalize(dirP);
    float3 dirN = pOS;
    dirN[axis] = -1.0;
    dirN = normalize(dirN);

    float dP = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, dirP, 0).x; // +face inward
    float dN = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, dirN, 0).x; // -face inward

    // 좌표 한계로 변환
    float minC = -1.0 + 2.0 * dN; // -face에서 들어온 만큼
    float maxC = 1.0 - 2.0 * dP; // +face에서 들어온 만큼
    return float2(minC, maxC);
}

// --------- 유틸: 내부 판정 ----------
bool InsideLimits(float3 p, float2 L[3])
{
    return (p.x >= L[0].x && p.x <= L[0].y) &&
           (p.y >= L[1].x && p.y <= L[1].y) &&
           (p.z >= L[2].x && p.z <= L[2].y);
}

// -------------------------------------------------------
// Occupancy 샘플 (주변이 fill돼있는지: inside=1, outside=0)
// -------------------------------------------------------
float OccupancyAt(float3 p)
{
    float2 Lx = DecodeAxisLimits(p, 0);
    float2 Ly = DecodeAxisLimits(p, 1);
    float2 Lz = DecodeAxisLimits(p, 2);
    return (p.x >= Lx.x && p.x <= Lx.y) &&
           (p.y >= Ly.x && p.y <= Ly.y) &&
           (p.z >= Lz.x && p.z <= Lz.y) ? 1.0 : 0.0;
}

// --------- 유틸: 축 노멀 근사 (경계까지 여유거리 최소 축) ----------
float3 EstimateAxisNormal(float3 p, float2 L[3])
{
    // 여유거리 6개 중 최솟값의 축/부호 선택
    float dx0 = p.x - L[0].x; // -X 경계까지
    float dx1 = L[0].y - p.x; // +X 경계까지
    float dy0 = p.y - L[1].x;
    float dy1 = L[1].y - p.y;
    float dz0 = p.z - L[2].x;
    float dz1 = L[2].y - p.z;

    float m = dx0;
    float3 n = float3(+1, 0, 0); // -X면 노멀은 +X
    if (dx1 < m)
    {
        m = dx1;
        n = float3(-1, 0, 0);
    } // +X면 노멀은 -X
    if (dy0 < m)
    {
        m = dy0;
        n = float3(0, +1, 0);
    }
    if (dy1 < m)
    {
        m = dy1;
        n = float3(0, -1, 0);
    }
    if (dz0 < m)
    {
        m = dz0;
        n = float3(0, 0, +1);
    }
    if (dz1 < m)
    {
        m = dz1;
        n = float3(0, 0, -1);
    }
    return n; // 이미 단위벡터
}

// -------------------------------------------------------
// 점유 기반 노멀: central differences on occupancy
//  n ≈ [ O(p-εx)-O(p+εx), O(p-εy)-O(p+εy), O(p-εz)-O(p+εz) ]
//  - ε는 [-1,1] 오브젝트 공간에서 "한 픽셀" 정도로 설정
//  - 실패 시 축 노멀로 폴백
// -------------------------------------------------------
float3 NormalFromFill(float3 p, float epsObj)
{
    float3 ex = float3(epsObj, 0, 0);
    float3 ey = float3(0, epsObj, 0);
    float3 ez = float3(0, 0, epsObj);

    float nx = OccupancyAt(p - ex) - OccupancyAt(p + ex);
    float ny = OccupancyAt(p - ey) - OccupancyAt(p + ey);
    float nz = OccupancyAt(p - ez) - OccupancyAt(p + ez);

    float3 n = float3(nx, ny, nz);
    float len = length(n);

    if (len > 1e-5)
        return n / len;

    // 폴백: 경계가 너무 얇거나 판정이 애매할 때 축 노멀 근사
    float2 Lx = DecodeAxisLimits(p, 0);
    float2 Ly = DecodeAxisLimits(p, 1);
    float2 Lz = DecodeAxisLimits(p, 2);
    float2 Lhit[3] = { Lx, Ly, Lz };
    return EstimateAxisNormal(p, Lhit);
}

// =======================================================
// Intersection Shader
// =======================================================
[shader("intersection")]
void MyIntersectionShader_Casper()
{
    // 레이 (object space)
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection(); // 정규화 안되었을 수 있음
    float rdLen = max(length(rd), 1e-6);
    float3 rdn = rd / rdLen;

    // AABB 상한 계산
    float tEnter, tExit;
    if (!RayBoxTSlab(ro, rd, float3(-1, -1, -1), float3(1, 1, 1), tEnter, tExit))
        return; // 박스와 교차 없음

    // t 진행 보폭(파라미터 공간). 구간을 균등 분할 + 안전한 상한
    const int MAX_STEPS = 128;
    float dt = max((tExit - tEnter) / MAX_STEPS, 1e-5);

    // 시작점
    float t0 = tEnter;
    float3 p0 = ro + rd * t0;

    // 초기 한계 계산
    float2 L0[3];
    L0[0] = DecodeAxisLimits(p0, 0);
    L0[1] = DecodeAxisLimits(p0, 1);
    L0[2] = DecodeAxisLimits(p0, 2);

    // 만약 시작이 이미 내부(박스 내부에서 시작한 경우 등)
    if (InsideLimits(p0, L0))
    {
        // 바로 이분탐색으로 경계 정밀화 (tEnter와 t0 사이는 동일하므로 tEnter~t0)
        float ta = tEnter, tb = t0;
        [unroll(6)]
        for (int k = 0; k < 6; ++k)
        {
            float tm = 0.5 * (ta + tb);
            float3 pm = ro + rd * tm;
            float2 Lm[3] = { DecodeAxisLimits(pm, 0), DecodeAxisLimits(pm, 1), DecodeAxisLimits(pm, 2) };
            if (InsideLimits(pm, Lm))
                tb = tm;
            else
                ta = tm;
        }
        float tHit = tb;
        float3 pHit = ro + rd * tHit;
        float2 Lhit[3] = { DecodeAxisLimits(pHit, 0), DecodeAxisLimits(pHit, 1), DecodeAxisLimits(pHit, 2) };

        const float epsObj = 2.0 / 512.0;
            
        MyCasperIntersectionAttributes attr;
        attr.Normal = NormalFromFill(pHit, epsObj);
        ReportHit(tHit, /*hitKind*/0, attr);
        return;
    }

    // 밖에서 안으로 들어가는 첫 지점 찾기
    float tPrev = t0;
    float3 pPrev = p0;
    float2 LPrev[3] = { L0[0], L0[1], L0[2] };

    bool found = false;
    float t = tPrev;
    float3 p = pPrev;

    [loop]
    for (int i = 0; i < MAX_STEPS && t <= tExit; ++i)
    {
        t += dt;
        p = ro + rd * t;

        float2 L[3];
        L[0] = DecodeAxisLimits(p, 0);
        L[1] = DecodeAxisLimits(p, 1);
        L[2] = DecodeAxisLimits(p, 2);

        if (InsideLimits(p, L))
        {
            found = true;
            // tPrev(밖) ~ t(안) 사이 이분탐색으로 경계 정밀화
            float ta = tPrev, tb = t;
            [unroll(6)]
            for (int k = 0; k < 6; ++k)
            {
                float tm = 0.5 * (ta + tb);
                float3 pm = ro + rd * tm;
                float2 Lm[3] = { DecodeAxisLimits(pm, 0), DecodeAxisLimits(pm, 1), DecodeAxisLimits(pm, 2) };
                if (InsideLimits(pm, Lm))
                    tb = tm;
                else
                    ta = tm;
            }
            float tHit = tb;
            float3 pHit = ro + rd * tHit;
            float2 Lhit[3] = { DecodeAxisLimits(pHit, 0), DecodeAxisLimits(pHit, 1), DecodeAxisLimits(pHit, 2) };

            const float epsObj = 2.0 / 512.0;
            
            MyCasperIntersectionAttributes attr;
            attr.Normal = NormalFromFill(pHit, epsObj); 
            ReportHit(tHit, /*hitKind*/0, attr);
            return;
        }

        // 다음 루프 위해 저장
        tPrev = t;
        pPrev = p;
        LPrev[0] = L[0];
        LPrev[1] = L[1];
        LPrev[2] = L[2];
    }

    // 못 찾았으면 히트 없음
    return;
}

//float2 calcLimit(float3 localPos, int axis)
//{
//    float3 sampleLocationPositive = localPos;
//    sampleLocationPositive[axis] = 1.0f;
//    float3 sampleLocationNegative = localPos;
//    sampleLocationNegative[axis] = -1.0f;
    
//    float dominantAxisLimitPositive = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(sampleLocationPositive), 0);
//    float dominantAxisLimitNegative = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(sampleLocationNegative), 0);
    
//    return float2(dominantAxisLimitPositive, 1.0f - dominantAxisLimitNegative);
//}

//bool isWithinLimits(float3 localPos, float2 limits[3])
//{
//    bool bWithinX = (localPos.x >= limits[0].x) && (localPos.x <= limits[0].y);
//    bool bWithinY = (localPos.y >= limits[1].x) && (localPos.y <= limits[1].y);
//    bool bWithinZ = (localPos.z >= limits[2].x) && (localPos.z <= limits[2].y);
    
//    return bWithinX && bWithinY && bWithinZ;
//}

//bool doIntersect(float3 pos, float3 bmin, float3 bmax)
//{
//    return all(pos >= bmin) && all(pos <= bmax);
//}

//[shader("intersection")]
//void MyIntersectionShader_Casper()
//{
//    float3 hitObjectPosition = ObjectRayOrigin() + RayTCurrent() * ObjectRayDirection();
//    float3 rayDir = normalize(ObjectRayDirection());
//    float3 absDir = abs(rayDir);
//    int dominantAxis = absDir.x > absDir.y ? (absDir.x > absDir.z ? 0 : 2) : (absDir.y > absDir.z ? 1 : 2);
    
//    float2 limits[3];
//    limits[0] = calcLimit(hitObjectPosition, 0);
//    limits[1] = calcLimit(hitObjectPosition, 1);
//    limits[2] = calcLimit(hitObjectPosition, 2);
   
//    int stepCount = 0;
//    const int LOOP_STEP_LIMIT = 512;
//    float tHit = 0.0f;
    
//    while (!isWithinLimits(rayDir * tHit, limits) && doIntersect(hitObjectPosition + rayDir * tHit, float3(-1.0f, -1.0f, -1.0f), float3(1.0f, 1.0f, 1.0f)))
//    {
//        if (stepCount >= LOOP_STEP_LIMIT)
//        {
//            break;
//        }
//        ++stepCount;
        
//        tHit += 0.01f; // 스텝 크기 조절 가능
           
//        limits[0] = calcLimit(hitObjectPosition, 0);
//        limits[1] = calcLimit(hitObjectPosition, 1);
//        limits[2] = calcLimit(hitObjectPosition, 2);
//    }
    
//    float tHit = length(endPoint - hitObjectPosition);
//    MyCasperIntersectionAttributes attr;
//    attr.Normal = float3(0, 0, 1); // 임시값
//    ReportHit(tHit, /*hitKind*/0, attr);
//    return;
    
    //Ray rayObject;
    //rayObject.origin = ObjectRayOrigin();
    //rayObject.direction = ObjectRayDirection();

    //float tminRay = max(RayTMin(), 1e-5);
    //float tmaxRay = RayTCurrent();

    //const float3 bmin = float3(-1.0, -1.0, -1.0);
    //const float3 bmax = float3(1.0, 1.0, 1.0);

    //float3 invRd = float3(rcp(rayObject.direction.x), rcp(rayObject.direction.y), rcp(rayObject.direction.z));
    //float3 t0 = (bmin - rayObject.origin) * invRd;
    //float3 t1 = (bmax - rayObject.origin) * invRd;
    //float3 tmin3 = min(t0, t1);
    //float tEnter = max(max(tmin3.x, tmin3.y), tmin3.z);

    //float tHit = max(tEnter, tminRay);
    //float3 enterPoint = rayObject.origin + rayObject.direction * tEnter;
    
    //uint axisEnter = 0;
    //if (abs(enterPoint.x) > 0.99 && abs(enterPoint.x) < 1.01)
    //    axisEnter = 0;
    //if (abs(enterPoint.y) > 0.99 && abs(enterPoint.y) < 1.01)
    //    axisEnter = 1;
    //if (abs(enterPoint.z) > 0.99 && abs(enterPoint.z) < 1.01)
    //    axisEnter = 2;
    
    //uint axis = axisEnter;

    //float3 n = float3(0, 0, 0);
    //n[axis] = (rayObject.direction[axis] >= 0.0f) ? -1.0f : 1.0f;

    //// 리포트
    //MyCasperIntersectionAttributes attr;
    //attr.Normal = n; // object-space
    //ReportHit(tHit, /*hitKind*/0, attr);
// }


[shader("closesthit")]
void MyClosestHitShader_RadianceRay_Casper(inout RadiancePayload rayPayload, in MyCasperIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    uint instanceID = InstanceID(); // The instance ID as specified in the instance desc.
    uint systemInstanceIndex = InstanceIndex(); // The autogenerated index of the current instance in the top-level structure.
    
    float3 hitObjectPosition = ObjectRayOrigin() + RayTCurrent() * ObjectRayDirection();
    float4 texDiffuse = l_DiffuseAtlasTexture.SampleLevel(g_SamplerClamp, normalize(hitObjectPosition), 0);
    float texDepth = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(hitObjectPosition), 0).x;
    
    float3 surfaceNormal = normalize(mul((float3x3) ObjectToWorld3x4(), attr.Normal));
    
    // Compute depth
    float4 projPos = mul(float4(hitPosition, 1.0), g_ViewProj);
    projPos /= projPos.w;
    rayPayload.depth = saturate(projPos.z);
    
    //rayPayload.radiance = texDepth.xxx;
    //return;
    
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
