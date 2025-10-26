#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"
#include "BxDF.hlsli"

ConstantBuffer<CONSTANT_BUFFER_RT_PROC> l_ProcGeomCB : register(b1, space0);
StructuredBuffer<Sphere> g_Spheres : register(t0, space1);

[shader("intersection")]
void MyIntersectionShader_Sphere()
{
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float tmin = max(RayTMin(), 1e-4);
    float tmax = RayTCurrent(); // FAR_PLANE 쓰면 다른 히트보다 먼 것도 리포트할 수 있음
    
    uint id = PrimitiveIndex();
    Sphere s = g_Spheres[id];

    float3 oc = ro - s.center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - s.radius * s.radius;
    float d = b * b - c; // 판별식

    if (d < 0.0)
        return; // 무교차

    float sqrtD = sqrt(d);
    float t0 = -b - sqrtD; // enter
    float t1 = -b + sqrtD; // exit  (t0 <= t1 보장)

    if (t0 >= tmin && t0 <= tmax)
    {
        SphereAttrib a0;
        float3 p0 = ro + t0 * rd;
        a0.normal = normalize(p0 - s.center);
        ReportHit(t0, /*hitKind*/0, a0); // enter
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
