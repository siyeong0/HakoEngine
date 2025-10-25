#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"
#include "BxDF.hlsli"

ConstantBuffer<CONSTANT_BUFFER_RT_PROC> l_ProcGeomCB : register(b1, space0);
StructuredBuffer<Sphere> g_Spheres : register(t0, space1);

[shader("intersection")]
void MyIntersectionShader_Sphere()
{
    // 레이 구간
    float3 ro = WorldRayOrigin();
    float3 rd = WorldRayDirection();
    float tmin = max(RayTMin(), 1e-4); // self-hit 방지용 ε
    float tmax = RayTCurrent(); // 현재 최근접 히트까지(상한)

    // 프림(구) 파라미터
    uint id = (uint)(l_ProcGeomCB.Material.Opacity) + PrimitiveIndex();
    Sphere s = g_Spheres[id];

    // 레이-구 교차 (rd 정규화 여부와 무관)
    float3 oc = ro - s.center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - s.radius * s.radius;
    float d = b * b - c; // 판별식

    if (d < 0.0)
        return; // 무교차

    float sqrtD = sqrt(d);
    float t0 = -b - sqrtD; // enter
    float t1 = -b + sqrtD; // exit  (t0 <= t1 보장)

    // t0 보고
    if (t0 >= tmin && t0 <= tmax)
    {
        SphereAttrib a0;
        float3 p0 = ro + t0 * rd;
        a0.normal = normalize(p0 - s.center);
        ReportHit(t0, /*hitKind*/0, a0); // enter
    }

    // t1 보고 (필요 시)
    if (t1 >= tmin && t1 <= tmax)
    {
        SphereAttrib a1;
        float3 p1 = ro + t1 * rd;
        a1.normal = normalize(p1 - s.center);
        ReportHit(t1, /*hitKind*/1, a1); // exit
    }
}


[shader("closesthit")]
void MyClosestHitShader_RadianceRay_Proc(inout RadiancePayload payload, in SphereAttrib attr)
{
    float3 N = normalize(attr.normal);
    float3 V = -normalize(WorldRayDirection());
    // 간단한 셰이딩 (원하면 기존 머티리얼/라이트 경로에 연결)
    float NoV = saturate(dot(N, V));
    float3 base = float3(0.7, 0.75, 1.0);
    payload.radiance = base * (0.2 + 0.8 * NoV);
}

[shader("closesthit")]
void MyClosestHitShader_ShadowRay_Proc(inout RadiancePayload payload, in SphereAttrib attr)
{
    // 그림자용: 히트하면 차단
    payload.radiance = 0;
}

#endif // RAYTRACING_HLSL
