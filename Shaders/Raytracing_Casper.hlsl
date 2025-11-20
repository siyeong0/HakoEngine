#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

struct MyCasperIntersectionAttributes
{
    float3 UnitSpaceHitPosition; // Hit position in [0,1]^3 inside the AABB
    int Face; // 0:+X,1:-X, 2:+Y,3:-Y, 4:+Z,5:-Z
    int MipLevel; // Mipmap level where the hit occurred
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
float3 ObjectToUnit(in float3 objectPosition)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    float3 inverseExtent = 1.0 / extent;
    return (objectPosition - aabb.Min) * inverseExtent;
}

float3 UnitToObject(in float3 unitPosition)
{
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 extent = aabb.Max - aabb.Min;
    return aabb.Min + unitPosition * extent;
}

// --------------------------------------------------
// Optimized UnitTToObjectT
//   - AABB / Ray를 외부에서 한 번만 계산하고 인자로 넘김
// --------------------------------------------------
float UnitTToObjectT(
    float tUnit,
    float3 unitRayOrigin,
    float3 unitRayDirection,
    float3 aabbMin,
    float3 aabbExtent,
    float3 objRayOrigin,
    float3 objRayDirection,
    float objRayDirLenSq)
{
    float3 unitHit = unitRayOrigin + unitRayDirection * tUnit;
    float3 objectHit = aabbMin + unitHit * aabbExtent;

    float3 diff = objectHit - objRayOrigin;
    float tObject = dot(diff, objRayDirection) / objRayDirLenSq;
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

// --------------------------------------------------
// NEW: IsInsideGeometry(pos, mip)
//  - 셀 박스 전체 대신 "셀 중심 포인트"가 안에 있는지만 검사
// --------------------------------------------------
bool IsInsideGeometry(float3 unitPos, int mip)
{
    float2 limits[3];
    GetLimits(unitPos, limits, mip);

    bool insideX = (unitPos.x >= limits[0].x && unitPos.x <= limits[0].y);
    bool insideY = (unitPos.y >= limits[1].x && unitPos.y <= limits[1].y);
    bool insideZ = (unitPos.z >= limits[2].x && unitPos.z <= limits[2].y);

    return insideX && insideY && insideZ;
}

// --------------------------------------------------
// 거리 + base voxel 대각선 길이 → "이 지점에서 적당한 mip" 계산
//   - distance: 카메라(레이 origin)에서 이 포인트까지 거리 (object-space)
//   - voxelDiag0: mip 0에서 voxel의 object-space 대각선 길이
//   - atlasMipCount: 전체 mip 개수
//   - 반환: 이 지점에서 사용해도 되는 "가장 coarse한" mip 인덱스
//           (이보다 더 coarse하면 각 크기(angular size)가 커져서 디테일 부족,
//            이보다 더 fine하면 디테일은 충분하지만 과한 샘플링)
// --------------------------------------------------
int PreferredMipFromDistance(float distance, float voxelDiag0, uint atlasMipCount)
{
    distance = max(distance, 1e-3f);
    voxelDiag0 = max(voxelDiag0, 1e-6f);

    // 원하는 "각 크기" (라디안 근사). 이 값으로 품질/성능 트레이드오프 튜닝.
    const float TARGET_ANGULAR_SIZE = 0.005f;

    // angularSize(mip) = voxelDiag0 * 2^mip / distance
    // TARGET_ANGULAR_SIZE ≈ voxelDiag0 * 2^mip / distance
    // => 2^mip ≈ TARGET_ANGULAR_SIZE * distance / voxelDiag0
    float ratio = TARGET_ANGULAR_SIZE * distance / voxelDiag0;
    float mipF = log2(ratio);

    // "이 지점에서 가장 coarse하게 쓸 수 있는 mip" = floor(mipF)
    int mip = (int) floor(mipF);
    return clamp(mip, 0, (int) (atlasMipCount - 1));
}

// --------------------------------------------------
// Determine entry face
// --------------------------------------------------
int DetermineEntryFace(in float3 unitEnterPos, in float3 unitRayDirection, int lastStepAxis)
{
    int outFaceIndex = 0;
    if (lastStepAxis < 0)
    {
        float3 distToMin = abs(unitEnterPos - 0.0);
        float3 distToMax = abs(1.0 - unitEnterPos);

        int entryAxis = 0;
        float best = 1e9;

        float candidate = min(distToMin.x, distToMax.x);
        if (candidate < best)
        {
            best = candidate;
            entryAxis = 0;
        }

        candidate = min(distToMin.y, distToMax.y);
        if (candidate < best)
        {
            best = candidate;
            entryAxis = 1;
        }

        candidate = min(distToMin.z, distToMax.z);
        if (candidate < best)
        {
            best = candidate;
            entryAxis = 2;
        }

        outFaceIndex = entryAxis * 2 + ((unitRayDirection[entryAxis] > 0.0f) ? 0 : 1);
    }
    else
    {
        outFaceIndex = (lastStepAxis * 2) + ((unitRayDirection[lastStepAxis] > 0.0f) ? 0 : 1);
    }

    return outFaceIndex;
}

#define DFS_DDA

#ifdef DFS_DDA

// =======================================================
// DDAState 간소화 버전 (mip, gridRes 제거)
// =======================================================
struct DDAState
{
    int3 cellIndex; // 현재 셀 인덱스
    float3 tMax; // 각 축의 다음 경계까지의 t (unit-space param)
    float3 tDelta; // 축별 셀 하나 이동할 때 증가하는 t
    float tAlong; // 현재 셀의 입구 t
    float tEnd; // 이 상태에서 허용되는 t 상한
    int lastStepAxis; // 마지막으로 step한 축 (0/1/2, -1 = 아직 없음)
};

// depth(=stackTop)와 coarsestMip로부터 현재 mip 계산
int MipFromDepth(int depth, int coarsestMip)
{
    return coarsestMip - depth;
}

// unit-space DDA 상태 초기화
void InitDDAState(
    out DDAState state,
    float tStart, // 이 상태의 시작 t (unit-space)
    float tEnd, // 이 상태의 끝 t (unit-space)
    float3 unitRayOrigin,
    float3 unitRayDirection,
    float3 unitRayDirAbs,
    uint gridResolution)
{
    state.tAlong = tStart;
    state.tEnd = tEnd;
    state.lastStepAxis = -1;

    float3 startPos = unitRayOrigin + unitRayDirection * tStart;
    float gridResF = (float) gridResolution;

    state.cellIndex = clamp(
        (int3) floor(startPos * gridResF),
        int3(0, 0, 0),
        int3((int) gridResolution - 1,
             (int) gridResolution - 1,
             (int) gridResolution - 1));

    float3 cellMin = (float3(state.cellIndex)) / gridResF;
    float3 cellMax = (float3(state.cellIndex) + 1.0) / gridResF;

    float3 dir = unitRayDirection;

    state.tMax.x = (dir.x > 0.0f)
        ? (cellMax.x - unitRayOrigin.x) / max(dir.x, 1e-30f)
        : (cellMin.x - unitRayOrigin.x) / min(dir.x, -1e-30f);

    state.tMax.y = (dir.y > 0.0f)
        ? (cellMax.y - unitRayOrigin.y) / max(dir.y, 1e-30f)
        : (cellMin.y - unitRayOrigin.y) / min(dir.y, -1e-30f);

    state.tMax.z = (dir.z > 0.0f)
        ? (cellMax.z - unitRayOrigin.z) / max(dir.z, 1e-30f)
        : (cellMin.z - unitRayOrigin.z) / min(dir.z, -1e-30f);

    float cellSize = 1.0f / gridResF;
    state.tDelta = abs((cellSize.xxx) / max(unitRayDirAbs, 1e-30f.xxx));
}

// =======================================================
// Intersection Shader (unit-space DDA + mipmap DFS, slim state)
// =======================================================

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    const int MAX_STEPS = 2048;
    const int MAX_MIP_DEPTH = 3;

    // 1) AABB entry/exit in object space
    float objectTEnter = RayTEnterObj();
    float objectTExit = RayTExitObj();

    if (objectTEnter >= objectTExit)
    {
        return; // miss
    }

    // Ray / AABB 정보 한 번만 계산
    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 objRayOrigin = ObjectRayOrigin();
    float3 objRayDir = ObjectRayDirection();
    float objRayDirLenSq = dot(objRayDir, objRayDir);
    float3 aabbExtent = aabb.Max - aabb.Min;

    float3 objectEnterPos = objRayOrigin + objectTEnter * objRayDir;
    float3 objectExitPos = objRayOrigin + objectTExit * objRayDir;

    // 2) Map entry/exit to unit space [0,1]^3
    float3 unitEnterPos = (objectEnterPos - aabb.Min) / aabbExtent;
    float3 unitExitPos = (objectExitPos - aabb.Min) / aabbExtent;

    float3 unitRayOrigin = unitEnterPos;
    float3 unitRayDirection = objRayDir / aabbExtent;
    float3 unitRayDirAbs = abs(unitRayDirection);

    // 3) Mipmap 정보
    uint atlasWidth, atlasHeight, atlasMipCount;
    l_DepthAtlasTexture.GetDimensions(0, atlasWidth, atlasHeight, atlasMipCount);

    float3x3 objToWorld3x3 = (float3x3) ObjectToWorld4x3();

    // row 벡터 기준 (네가 normal 계산할 때 쓰던 방식이랑 동일)
    float3 row0 = objToWorld3x3[0];
    float3 row1 = objToWorld3x3[1];
    float3 row2 = objToWorld3x3[2];

    float scaleX = length(row0);
    float scaleY = length(row1);
    float scaleZ = length(row2);

    // object-space voxel size
    float3 voxelSizeObj0 = aabbExtent / (float) atlasWidth;

    // world-space voxel size (축별)
    float3 voxelSizeWorld0 = float3(
    voxelSizeObj0.x * scaleX,
    voxelSizeObj0.y * scaleY,
    voxelSizeObj0.z * scaleZ
    );

    // 이걸로 LOD 기준 삼기
    float voxelDiag0 = length(voxelSizeWorld0);

    // object-space entry를 world-space로 변환
    float3 worldEnterPos = mul(float4(objectEnterPos, 1.0), ObjectToWorld4x3());

    // ★ 카메라 기준 거리로 LOD 결정 (Radiance / Shadow 모두 동일)
    float distEnterFromCamera = length(worldEnterPos - GetCameraPosition());
    int preferredMipAtEnter = PreferredMipFromDistance(
    distEnterFromCamera,
    voxelDiag0,
    atlasMipCount);

    // 이 레이에서 실제 사용할 mip 범위 [finestMip, coarsestMip]
    //   - finestMip: 가장 fine하게 내려갈 mip
    //   - coarsestMip: DFS 시작 지점 (더 coarse)
    // depth 0에서 coarsestMip, depth 증가할수록 mip 하나씩 내려가게 할거라,
    //   depth=MAX_MIP_DEPTH-1 에서 finestMip에 도달하도록 맞춰줌.
    int finestMip = preferredMipAtEnter;
    int coarsestMip = finestMip + (MAX_MIP_DEPTH - 1);
    coarsestMip = min(coarsestMip, (int) (atlasMipCount - 1));

    // 살짝 안쪽으로 밀기
    unitRayOrigin += unitRayDirection * EPSILON;

    // 공통 cell step
    int3 cellStep = int3(
        (unitRayDirection.x > 0.0f) ? 1 : -1,
        (unitRayDirection.y > 0.0f) ? 1 : -1,
        (unitRayDirection.z > 0.0f) ? 1 : -1);

    // 4) DFS 스택 초기화 (가장 coarse mip부터)
    DDAState stack[MAX_MIP_DEPTH];
    int stackTop = 0;

    float tStartTop = 0.0f;
    float tEndTop = 1e30f; // objectTExit는 object-space에서 따로 제한

    {
        int topMip = MipFromDepth(0, coarsestMip);
        uint topGridRes = max(atlasWidth >> topMip, 1u);
        InitDDAState(stack[0],
                     tStartTop,
                     tEndTop,
                     unitRayOrigin,
                     unitRayDirection,
                     unitRayDirAbs,
                     topGridRes);
        stack[0].lastStepAxis = -1;
    }

    int totalSteps = 0;

    // 5) DFS DDA
    [loop]
    while (stackTop >= 0 && totalSteps < MAX_STEPS)
    {
        int currDepth = stackTop;
        int currMip = MipFromDepth(currDepth, coarsestMip);
        uint gridRes = max(atlasWidth >> currMip, 1u);
        float gridResF = (float) gridRes;

        DDAState state = stack[stackTop];

        for (;;)
        {
            if (totalSteps++ >= MAX_STEPS)
                break;

            // 다음 경계
            uint axisToStep = state.tMax.x < state.tMax.y
                ? (state.tMax.x < state.tMax.z ? 0 : 2)
                : (state.tMax.y < state.tMax.z ? 1 : 2);

            float tAtNextBoundary = state.tMax[axisToStep];

            // 셀 중심 포인트
            float3 cellCenter = (float3(state.cellIndex) + 0.5) / gridResF;

            // 현재 셀 검사
            if (IsInsideGeometry(cellCenter, currMip))
            {
                if (currMip == finestMip)
                {
                    float3 unitHitPos = unitRayOrigin + unitRayDirection * state.tAlong;

                    MyCasperIntersectionAttributes attr;
                    attr.UnitSpaceHitPosition = unitHitPos;
                    attr.Face = DetermineEntryFace(unitHitPos, unitRayDirection, state.lastStepAxis);
                    attr.MipLevel = currMip;
                    float objectTHit = UnitTToObjectT(
                        state.tAlong,
                        unitRayOrigin,
                        unitRayDirection,
                        aabb.Min,
                        aabbExtent,
                        objRayOrigin,
                        objRayDir,
                        objRayDirLenSq);

                    ReportHit(objectTHit, /*hitKind*/0, attr);
                    return;
                }
                else
                {
                    // 더 fine mip로 DFS 내려가기 (depth+1 → mip-1)
                    int childDepth = currDepth + 1;
                    if (childDepth < MAX_MIP_DEPTH)
                    {
                        int childMip = MipFromDepth(childDepth, coarsestMip);
                        uint childGridRes = max(atlasWidth >> childMip, 1u);

                        // 부모 상태는 "이 셀 이후"로 업데이트해서 남겨둔다.
                        DDAState parentResume = state;
                        parentResume.lastStepAxis = (int) axisToStep;
                        parentResume.cellIndex[axisToStep] += cellStep[axisToStep];
                        parentResume.tMax[axisToStep] += parentResume.tDelta[axisToStep];
                        parentResume.tAlong = tAtNextBoundary;

                        stack[stackTop] = parentResume;

                        // child용 t 구간 [입구, 출구]
                        float childTStart = state.tAlong;
                        float childTEnd = tAtNextBoundary;

                        ++stackTop;
                        InitDDAState(
                            stack[stackTop],
                            childTStart,
                            childTEnd,
                            unitRayOrigin,
                            unitRayDirection,
                            unitRayDirAbs,
                            childGridRes);

                        // entry face 정보 유지
                        stack[stackTop].lastStepAxis = state.lastStepAxis;
                    }

                    // child로 내려갔으니 이 상태 루프 종료
                    break;
                }
            }

            // object-space AABB exit / tEnd 체크
            float objectTCandidate = UnitTToObjectT(
                tAtNextBoundary,
                unitRayOrigin,
                unitRayDirection,
                aabb.Min,
                aabbExtent,
                objRayOrigin,
                objRayDir,
                objRayDirLenSq);

            if (objectTCandidate > objectTExit + 1e-6f ||
                tAtNextBoundary > state.tEnd + 1e-6f)
            {
                // 더 볼 것 없음 → pop
                --stackTop;
                break;
            }

            // 다음 셀로 step
            state.cellIndex[axisToStep] += cellStep[axisToStep];
            state.tMax[axisToStep] += state.tDelta[axisToStep];

            state.tAlong = tAtNextBoundary;
            state.lastStepAxis = (int) axisToStep;

            // grid 밖이면 pop
            if (any(state.cellIndex < 0.xxx) ||
                any(state.cellIndex >= (int3) gridRes.xxx))
            {
                --stackTop;
                break;
            }

            // 상태 갱신 후 계속
            stack[stackTop] = state;
        }
    }

    // No hit
    return;
}

#else  // !DFS_DDA (단일 mip 버전도 IsInsideGeometry(pos)로 수정)

// =======================================================
// Intersection Shader (unit-space DDA traversal only)
// =======================================================

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    const int MAX_STEPS = 2048;

    float objectTEnter = RayTEnterObj();
    float objectTExit  = RayTExitObj();

    if (objectTEnter >= objectTExit)
    {
        return; // miss
    }

    AABB  aabb            = l_AABBBuffer[PrimitiveIndex()];
    float3 objRayOrigin   = ObjectRayOrigin();
    float3 objRayDir      = ObjectRayDirection();
    float  objRayDirLenSq = dot(objRayDir, objRayDir);
    float3 aabbExtent     = aabb.Max - aabb.Min;

    float3 objectEnterPos = objRayOrigin + objectTEnter * objRayDir;
    float3 objectExitPos  = objRayOrigin + objectTExit  * objRayDir;

    float3 unitEnterPos = (objectEnterPos - aabb.Min) / aabbExtent;
    float3 unitExitPos  = (objectExitPos  - aabb.Min) / aabbExtent;

    float3 unitRayOrigin    = unitEnterPos;
    float3 unitRayDirection = objRayDir / aabbExtent;
    float3 unitRayDirAbs    = abs(unitRayDirection);

    uint atlasWidth, atlasHeight, atlasMipCount;
    l_DepthAtlasTexture.GetDimensions(0, atlasWidth, atlasHeight, atlasMipCount);

    int  mipLevelToTest = 0;
    uint gridResolution = max(atlasWidth >> mipLevelToTest, 1u);

    unitRayOrigin += unitRayDirection * EPSILON;

    int3 cellIndex = clamp(
        (int3)floor(unitRayOrigin * gridResolution),
        int3(0, 0, 0),
        int3(gridResolution - 1, gridResolution - 1, gridResolution - 1));

    int3 cellStep = int3(
        (unitRayDirection.x > 0.0f) ?  1 : -1,
        (unitRayDirection.y > 0.0f) ?  1 : -1,
        (unitRayDirection.z > 0.0f) ?  1 : -1);

    float3 currentCellUnitMin = (float3(cellIndex)) / gridResolution;
    float3 currentCellUnitMax = (float3(cellIndex) + 1.0) / gridResolution;

    float tMaxX = (unitRayDirection.x > 0.0f)
        ? (currentCellUnitMax.x - unitRayOrigin.x) / max(unitRayDirection.x, 1e-30f)
        : (currentCellUnitMin.x - unitRayOrigin.x) / min(unitRayDirection.x, -1e-30f);

    float tMaxY = (unitRayDirection.y > 0.0f)
        ? (currentCellUnitMax.y - unitRayOrigin.y) / max(unitRayDirection.y, 1e-30f)
        : (currentCellUnitMin.y - unitRayOrigin.y) / min(unitRayDirection.y, -1e-30f);

    float tMaxZ = (unitRayDirection.z > 0.0f)
        ? (currentCellUnitMax.z - unitRayOrigin.z) / max(unitRayDirection.z, 1e-30f)
        : (currentCellUnitMin.z - unitRayOrigin.z) / min(unitRayDirection.z, -1e-30f);

    float3 tMaxPerAxis   = float3(tMaxX, tMaxY, tMaxZ);
    float3 tDeltaPerAxis =
        abs((1.0 / gridResolution) / max(unitRayDirAbs, 1e-30f.xxx));

    float tAlongUnitRay = 0.0f;
    int   lastStepAxis  = -1;

    [loop]
    for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex)
    {
        uint axisToStep = tMaxPerAxis.x < tMaxPerAxis.y
            ? (tMaxPerAxis.x < tMaxPerAxis.z ? 0 : 2)
            : (tMaxPerAxis.y < tMaxPerAxis.z ? 1 : 2);

        float tAtNextBoundary = tMaxPerAxis[axisToStep];

        float3 cellCenter = (float3(cellIndex) + 0.5) / gridResolution;

        if (IsInsideGeometry(cellCenter, mipLevelToTest))
        {
            float3 unitHitPos = unitRayOrigin + unitRayDirection * tAlongUnitRay;

            MyCasperIntersectionAttributes attr;
            attr.UnitSpaceHitPosition = unitHitPos;
            attr.Face = DetermineEntryFace(unitHitPos, unitRayDirection, lastStepAxis);
            attr.MipLevel = mipLevelToTest;
            float objectTHit = UnitTToObjectT(
                tAlongUnitRay,
                unitRayOrigin,
                unitRayDirection,
                aabb.Min,
                aabbExtent,
                objRayOrigin,
                objRayDir,
                objRayDirLenSq);

            ReportHit(objectTHit, /*hitKind*/0, attr);
            return;
        }

        float objectTCandidate = UnitTToObjectT(
            tAtNextBoundary,
            unitRayOrigin,
            unitRayDirection,
            aabb.Min,
            aabbExtent,
            objRayOrigin,
            objRayDir,
            objRayDirLenSq);

        if (objectTCandidate > objectTExit + 1e-6f)
        {
            break;
        }

        cellIndex[axisToStep]      += cellStep[axisToStep];
        tMaxPerAxis[axisToStep]    += tDeltaPerAxis[axisToStep];

        tAlongUnitRay = tAtNextBoundary;
        lastStepAxis  = (int)axisToStep;

        if (any(cellIndex < 0.xxx) ||
            any(cellIndex >= gridResolution.xxx))
        {
            break;
        }
    }

    return;
}

#endif // DFS_DDA

// ============================================================================
// Closest hit 셰이더 (기존과 동일)
// ============================================================================

[shader("closesthit")]
void MyClosestHitShader_RadianceRay_Casper(
    inout RadiancePayload rayPayload,
    in MyCasperIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    float4 projPos = mul(float4(hitPosition, 1.0), g_ViewProj);
    projPos /= projPos.w;
    rayPayload.depth = saturate(projPos.z);

    int axisP = attr.Face / 2;
    int axisU = (axisP + 1) % 3;
    int axisV = (axisP + 2) % 3;

    bool bFaceSign = (attr.Face % 2) != 0;

    float3 location = attr.UnitSpaceHitPosition * 2.0 - 1.0;
    location[axisP] = bFaceSign ? 1.0 : -1.0;
    float4 texDiffuse = l_DiffuseAtlasTexture.SampleLevel(g_SamplerPoint, normalize(location), attr.MipLevel);
    
    uint atlasWidth, atlasHeight, atlasMipCount;
    l_DepthAtlasTexture.GetDimensions(0, atlasWidth, atlasHeight, atlasMipCount);

    float ddim = atlasWidth >> attr.MipLevel;

    float3 loc0 = location;
    loc0[axisU] -= 1.0 / ddim;
    float3 loc1 = location;
    loc1[axisU] += 1.0 / ddim;
    float3 loc2 = location;
    loc2[axisV] -= 1.0 / ddim;
    float3 loc3 = location;
    loc3[axisV] += 1.0 / ddim;

    float d0 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc0), attr.MipLevel);
    float d1 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc1), attr.MipLevel);
    float d2 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc2), attr.MipLevel);
    float d3 = l_DepthAtlasTexture.SampleLevel(g_SamplerClamp, normalize(loc3), attr.MipLevel);

    float3 unitPos0 = loc0 * 0.5 + 0.5;
    unitPos0[axisP] = !bFaceSign ? d0 : (1.0 - d0);
    float3 unitPos1 = loc1 * 0.5 + 0.5;
    unitPos1[axisP] = !bFaceSign ? d1 : (1.0 - d1);
    float3 unitPos2 = loc2 * 0.5 + 0.5;
    unitPos2[axisP] = !bFaceSign ? d2 : (1.0 - d2);
    float3 unitPos3 = loc3 * 0.5 + 0.5;
    unitPos3[axisP] = !bFaceSign ? d3 : (1.0 - d3);

    float3 vec1 = UnitToObject(unitPos1) - UnitToObject(unitPos0);
    float3 vec2 = UnitToObject(unitPos3) - UnitToObject(unitPos2);

    float3 objectNormal =
        bFaceSign ? normalize(cross(vec1, vec2)) :
                    normalize(cross(vec2, vec1));

    float3 surfaceNormal = normalize(mul(objectNormal, (float3x3) ObjectToWorld4x3()));

    BasicMaterial mtl = l_ProcGeomCB.Material;
    float3 baseColor = mtl.BaseColor * texDiffuse.xyz;
    float metallic = mtl.MetallicFactor;
    float roughness = max(0.04f, saturate(mtl.RoughnessFactor));

    ShadingInfo info;
    info.Kd = (1.0 - mtl.MetallicFactor) * baseColor;
    info.Ks = saturate(lerp(mtl.SpecularColor, baseColor, metallic) * mtl.SpecularFactor);
    info.Kr = (metallic > 0.5) ? baseColor : 1.0.xxx;
    info.Kt = (1.0f - saturate(mtl.Opacity)) * baseColor;
    info.Roughness = roughness;
    info.AmbientOcclusionStrength = mtl.AmbientOcclusionStrength;

    rayPayload.radiance = Shade(rayPayload, surfaceNormal, hitPosition, info);
}

[shader("closesthit")]
void MyClosestHitShader_ShadowRay_Casper(
    inout ShadowPayload rayPayload,
    in MyCasperIntersectionAttributes attr)
{
    rayPayload.tHit = RayTCurrent();
}

#endif // RAYTRACING_PROC_HLSL
