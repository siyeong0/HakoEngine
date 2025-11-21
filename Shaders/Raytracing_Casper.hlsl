#ifndef RAYTRACING_PROC_HLSL
#define RAYTRACING_PROC_HLSL

#include "Raytracing_common.hlsl"

// ============================================================================
// Intersection attributes
// ============================================================================

struct MyCasperIntersectionAttributes
{
    float3 UnitSpaceHitPosition; // Hit position in [0,1]^3 inside the AABB
    int Face; // 0:+X,1:-X, 2:+Y,3:-Y, 4:+Z,5:-Z
    int MipLevel; // Mipmap level where the hit occurred
};

// ============================================================================
// Resources
// ============================================================================

ConstantBuffer<CONSTANT_BUFFER_RT_PROC> l_ProcGeomCB : register(b1, space0);

TextureCube<float4> l_DiffuseAtlasTexture : register(t0, space1);
TextureCube<float> l_DepthAtlasTexture : register(t1, space1);

struct AABB
{
    float3 Min;
    float3 Max;
};

StructuredBuffer<AABB> l_AABBBuffer : register(t2, space1);

// ============================================================================
// Ray vs AABB intersection (object space)
// ============================================================================

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

// ============================================================================
// Object-space <-> Unit-space helpers
// ============================================================================

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

// ============================================================================
// Unit-space t -> object-space t conversion
// ============================================================================
//
// We traverse in unit space [0,1]^3, but the DXR intersection has to be
// reported in object-space param t. This helper converts a unit-space
// parametric t back to object-space t along the original ray.
//

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

// ============================================================================
// CASPER depth-atlas sampling & volume query
// ============================================================================
//
// The depth atlas encodes, for each cube-map face direction, how far the solid
// region extends along that direction in unit space.
//
// Given a unit-space position p in [0,1]^3:
//   1) We sample the depth atlas for each cube face (±X, ±Y, ±Z).
//   2) We convert those 6 depths into per-axis [min,max] intervals.
//   3) We test whether p lies inside all three axis intervals → "inside volume".
//

float SampleAxisFaceDepth(float3 unitPosition, int axis, int sign, int mip)
{
    // Map [0,1]^3 → [-1,1]^3 for cube-map sampling,
    // then override the axis component with the desired face direction.
    float3 sampleDirection = unitPosition * 2.0f - 1.0f;
    sampleDirection[axis] = sign;
    return l_DepthAtlasTexture.SampleLevel(g_SamplerPoint, normalize(sampleDirection), mip);
}

void SampleCubeFaceDepths(float3 unitPosition, out float outEmpties[6], int mip)
{
    outEmpties[0] = SampleAxisFaceDepth(unitPosition, 0, -1.0, mip); // X-
    outEmpties[1] = SampleAxisFaceDepth(unitPosition, 0, +1.0, mip); // X+
    outEmpties[2] = SampleAxisFaceDepth(unitPosition, 1, -1.0, mip); // Y-
    outEmpties[3] = SampleAxisFaceDepth(unitPosition, 1, +1.0, mip); // Y+
    outEmpties[4] = SampleAxisFaceDepth(unitPosition, 2, -1.0, mip); // Z-
    outEmpties[5] = SampleAxisFaceDepth(unitPosition, 2, +1.0, mip); // Z+
}

// For a given unit-space position, compute valid [min,max] ranges along X/Y/Z
void ComputeAxisDepthRanges(out float2 outAxisRanges[3], float3 unitPosition, int mip)
{
    float faceDepths[6];
    SampleCubeFaceDepths(unitPosition, faceDepths, mip);

    // For each axis:
    //   min = coordinate of the inner boundary coming from the negative face
    //   max = coordinate of the inner boundary coming from the positive face
    //
    // Depth is stored in [0,1], where 0 is exactly on the face and 1 is at
    // the opposite side of the unit cube, so we remap the positive-face
    // depths via (1 - depth).
    outAxisRanges[0] = float2(faceDepths[0], 1.0 - faceDepths[1]); // X
    outAxisRanges[1] = float2(faceDepths[2], 1.0 - faceDepths[3]); // Y
    outAxisRanges[2] = float2(faceDepths[4], 1.0 - faceDepths[5]); // Z
}

// ============================================================================
// 4) Point-in-volume test
// ============================================================================
//
// Instead of testing the entire cell box, we test only the cell center point.
// Given a unit-space position, we compute per-axis [min,max] intervals from
// the depth atlas and check if the point lies inside all three intervals.
//

bool IsPointInsideVolume(float3 unitPos, int mip)
{
    float2 axisRanges[3];
    ComputeAxisDepthRanges(axisRanges, unitPos, mip);

    bool insideX = (unitPos.x >= axisRanges[0].x && unitPos.x <= axisRanges[0].y);
    bool insideY = (unitPos.y >= axisRanges[1].x && unitPos.y <= axisRanges[1].y);
    bool insideZ = (unitPos.z >= axisRanges[2].x && unitPos.z <= axisRanges[2].y);

    return insideX && insideZ && insideY;
}

// ============================================================================
// LOD selection based on camera distance
// ============================================================================
//
//  - distance:     camera-to-point distance in world space
//  - voxelDiag0:   diagonal length of a voxel at mip 0 in world space
//  - atlasMipCount: number of mip levels in the atlas
//
// We approximate the angular size of one voxel and choose a mip level such
// that the voxel angular size ≈ TARGET_ANGULAR_SIZE.
//

int PreferredMipFromDistance(float distance, float voxelDiag0, uint atlasMipCount)
{
    distance = max(distance, 1e-3f);
    voxelDiag0 = max(voxelDiag0, 1e-6f);

    // Tunable angular size (radians) for "acceptable" level of detail.
    const float TARGET_ANGULAR_SIZE = 0.005f;

    // angularSize(mip) ≈ voxelDiag0 * 2^mip / distance
    // TARGET_ANGULAR_SIZE ≈ voxelDiag0 * 2^mip / distance
    // => 2^mip ≈ TARGET_ANGULAR_SIZE * distance / voxelDiag0
    float ratio = TARGET_ANGULAR_SIZE * distance / voxelDiag0;
    float mipF = log2(ratio);

    // Coarsest acceptable mip at this point
    int mip = (int) floor(mipF);
    return clamp(mip, 0, (int) (atlasMipCount - 1));
}

// ============================================================================
// Entry face determination
// ============================================================================
//
// lastStepAxis = -1  : we just entered the AABB, estimate from unitEnterPos
// lastStepAxis = 0/1/2 : we arrived by stepping along that axis.
//

int DetermineEntryFace(in float3 unitEnterPos, in float3 unitRayDirection, int lastStepAxis)
{
    int outFaceIndex = 0;

    if (lastStepAxis < 0)
    {
        // Estimate which face we entered by checking which unit-space
        // boundary (0 or 1) we are closest to.
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

        // Face index = axis * 2 + sign
        outFaceIndex = entryAxis * 2 + ((unitRayDirection[entryAxis] > 0.0f) ? 0 : 1);
    }
    else
    {
        // We know which axis we stepped along last to enter this cell
        outFaceIndex = (lastStepAxis * 2) + ((unitRayDirection[lastStepAxis] > 0.0f) ? 0 : 1);
    }

    return outFaceIndex;
}

// ============================================================================
// DFS-based mip traversal switch
// ============================================================================

#define DFS_DDA

#ifdef DFS_DDA

// ============================================================================
// DDA state (unit space, shared across mip levels)
// ============================================================================
//
// This structure stores the minimal information needed to resume a 3D DDA
// traversal in unit space. We keep it small because we push it onto an
// explicit stack for mip-level DFS.
//

struct DDAState
{
    int3 cellIndex; // Current cell index in the grid
    float3 tMax; // t at the next boundary along each axis (unit-space t)
    float3 tDelta; // t increment when stepping by one cell along each axis
    float tAlong; // t where we entered the current cell
    float tEnd; // Upper bound of t allowed in this state
    int lastStepAxis; // Last stepped axis (0/1/2, -1 = none yet)
};

// Compute current mip from DFS depth and coarsestMip.
// depth 0  -> coarsestMip
// depth 1  -> coarsestMip - 1
// ...
int MipFromDepth(int depth, int coarsestMip)
{
    return coarsestMip - depth;
}

// Initialize a unit-space DDA state for a given [tStart, tEnd] interval.
//
void InitDDAState(
    out DDAState state,
    float tStart, // Start t (unit space)
    float tEnd, // End t (unit space)
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

    // Clamp to grid so that we do not start outside.
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

// ============================================================================
// Intersection Shader
//  - Unit-space 3D DDA
//  - Mipmap-aware DFS (coarse → fine)
// ============================================================================

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    const int MAX_STEPS = 2048; // Global safety cap for all DDA steps
    const int MAX_MIP_DEPTH = 3; // DFS stack depth (number of mip levels visited)

    // ------------------------------------------------------------------------
    // 1) Ray vs AABB in object space
    // ------------------------------------------------------------------------
    float objectTEnter = RayTEnterObj();
    float objectTExit = RayTExitObj();

    if (objectTEnter >= objectTExit)
    {
        return; // No intersection with the AABB
    }

    AABB aabb = l_AABBBuffer[PrimitiveIndex()];
    float3 objRayOrigin = ObjectRayOrigin();
    float3 objRayDir = ObjectRayDirection();
    float objRayDirLenSq = dot(objRayDir, objRayDir);
    float3 aabbExtent = aabb.Max - aabb.Min;

    float3 objectEnterPos = objRayOrigin + objectTEnter * objRayDir;
    float3 objectExitPos = objRayOrigin + objectTExit * objRayDir;

    // ------------------------------------------------------------------------
    // 2) Map entry/exit to unit space [0,1]^3
    // ------------------------------------------------------------------------
    float3 unitEnterPos = (objectEnterPos - aabb.Min) / aabbExtent;
    float3 unitExitPos = (objectExitPos - aabb.Min) / aabbExtent;

    float3 unitRayOrigin = unitEnterPos;
    float3 unitRayDirection = objRayDir / aabbExtent;
    float3 unitRayDirAbs = abs(unitRayDirection);

    // ------------------------------------------------------------------------
    // 3) Mipmap information and LOD selection
    // ------------------------------------------------------------------------
    uint atlasWidth, atlasHeight, atlasMipCount;
    l_DepthAtlasTexture.GetDimensions(0, atlasWidth, atlasHeight, atlasMipCount);

    // Extract object-to-world 3x3 for scale estimation
    float3x3 objToWorld3x3 = (float3x3) ObjectToWorld4x3();

    // Rows correspond to transformed basis vectors
    float3 row0 = objToWorld3x3[0];
    float3 row1 = objToWorld3x3[1];
    float3 row2 = objToWorld3x3[2];

    float scaleX = length(row0);
    float scaleY = length(row1);
    float scaleZ = length(row2);

    // Object-space voxel size at mip 0
    float3 voxelSizeObj0 = aabbExtent / (float) atlasWidth;

    // World-space voxel size (per axis)
    float3 voxelSizeWorld0 = float3(
        voxelSizeObj0.x * scaleX,
        voxelSizeObj0.y * scaleY,
        voxelSizeObj0.z * scaleZ);

    float voxelDiag0 = length(voxelSizeWorld0);

    // World-space entry position
    float3 worldEnterPos = mul(float4(objectEnterPos, 1.0), ObjectToWorld4x3());

    // LOD is determined in a camera-centric way and reused for all ray types
    float distEnterFromCamera = length(worldEnterPos - GetCameraPosition());
    int preferredMipAtEnter = PreferredMipFromDistance(
        distEnterFromCamera,
        voxelDiag0,
        atlasMipCount);

    // For this ray, we choose a mip interval [finestMip, coarsestMip]:
    //   - DFS starts at coarsestMip (depth 0)
    //   - Each depth step goes one mip finer
    //
    // We clamp so that (coarsestMip, ..., finestMip) lies within [0, atlasMipCount-1].
    int finestMip = preferredMipAtEnter;
    int coarsestMip = finestMip + (MAX_MIP_DEPTH - 1);
    coarsestMip = min(coarsestMip, (int) (atlasMipCount - 1));

    // Nudge the starting position slightly inside the volume to avoid
    // numerical issues exactly on the AABB boundary.
    unitRayOrigin += unitRayDirection * EPSILON;

    // Common cell step sign for all mip levels
    int3 cellStep = int3(
        (unitRayDirection.x > 0.0f) ? 1 : -1,
        (unitRayDirection.y > 0.0f) ? 1 : -1,
        (unitRayDirection.z > 0.0f) ? 1 : -1);

    // ------------------------------------------------------------------------
    // 4) DFS stack initialization (start from the coarsest mip)
    // ------------------------------------------------------------------------
    DDAState stack[MAX_MIP_DEPTH];
    int stackTop = 0;

    float tStartTop = 0.0f; // In unit-space parametric t
    float tEndTop = 1e30f; // Loose bound; actual exit is checked in object space

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

    // ------------------------------------------------------------------------
    // 5) DFS + DDA traversal
    // ------------------------------------------------------------------------
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

            // ----------------------------------------------------------------
            // 5.1) Choose the next boundary among X/Y/Z
            // ----------------------------------------------------------------
            uint axisToStep = state.tMax.x < state.tMax.y
                ? (state.tMax.x < state.tMax.z ? 0 : 2)
                : (state.tMax.y < state.tMax.z ? 1 : 2);

            float tAtNextBoundary = state.tMax[axisToStep];

            // Center of the current cell in unit space
            float3 cellCenter = (float3(state.cellIndex) + 0.5) / gridResF;

            // ----------------------------------------------------------------
            // 5.2) Test current cell
            // ----------------------------------------------------------------
            if (IsPointInsideVolume(cellCenter, currMip))
            {
                if (currMip == finestMip)
                {
                    // We reached the finest mip for this ray: report hit.
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
                    // Cell is inside at a coarse mip. We descend to a finer mip
                    // over the same t-interval [state.tAlong, tAtNextBoundary].
                    int childDepth = currDepth + 1;
                    if (childDepth < MAX_MIP_DEPTH)
                    {
                        int childMip = MipFromDepth(childDepth, coarsestMip);
                        uint childGridRes = max(atlasWidth >> childMip, 1u);

                        // Parent state is updated to resume *after* this cell.
                        DDAState parentResume = state;
                        parentResume.lastStepAxis = (int) axisToStep;
                        parentResume.cellIndex[axisToStep] += cellStep[axisToStep];
                        parentResume.tMax[axisToStep] += parentResume.tDelta[axisToStep];
                        parentResume.tAlong = tAtNextBoundary;

                        stack[stackTop] = parentResume;

                        // Child t-interval for finer mip
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

                        // Preserve entry-face information for finer mip
                        stack[stackTop].lastStepAxis = state.lastStepAxis;
                    }

                    // We just pushed a child and will process it next.
                    break;
                }
            }

            // ----------------------------------------------------------------
            // 5.3) Check if we are past the AABB exit or local tEnd
            // ----------------------------------------------------------------
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
                // Nothing more to see in this state → pop and return to parent
                --stackTop;
                break;
            }

            // ----------------------------------------------------------------
            // 5.4) Step to the next cell along axisToStep
            // ----------------------------------------------------------------
            state.cellIndex[axisToStep] += cellStep[axisToStep];
            state.tMax[axisToStep] += state.tDelta[axisToStep];

            state.tAlong = tAtNextBoundary;
            state.lastStepAxis = (int) axisToStep;

            // If we leave the grid, this mip-level segment is done.
            if (any(state.cellIndex < 0.xxx) ||
                any(state.cellIndex >= (int3) gridRes.xxx))
            {
                --stackTop;
                break;
            }

            // Commit updated state and continue the inner loop
            stack[stackTop] = state;
        }
    }

    // No hit in this AABB
    return;
}

#else // !DFS_DDA

// ============================================================================
// Intersection Shader (single-mip DDA traversal)
// ============================================================================
//
// Simpler version that only traverses a single mip level. Still uses the
// IsInsideGeometry(...) test but skips DFS logic.
//

[shader("intersection")]
void MyIntersectionShader_Casper()
{
    const int MAX_STEPS = 2048;

    // Object-space AABB intersection
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

    // Unit-space mapping
    float3 unitEnterPos = (objectEnterPos - aabb.Min) / aabbExtent;
    float3 unitExitPos  = (objectExitPos  - aabb.Min) / aabbExtent;

    float3 unitRayOrigin    = unitEnterPos;
    float3 unitRayDirection = objRayDir / aabbExtent;
    float3 unitRayDirAbs    = abs(unitRayDirection);

    uint atlasWidth, atlasHeight, atlasMipCount;
    l_DepthAtlasTexture.GetDimensions(0, atlasWidth, atlasHeight, atlasMipCount);

    int  mipLevelToTest = 0;
    uint gridResolution = max(atlasWidth >> mipLevelToTest, 1u);

    // Nudge origin slightly inside
    unitRayOrigin += unitRayDirection * EPSILON;

    int3 cellIndex = clamp(
        (int3) floor(unitRayOrigin * gridResolution),
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
        // Next boundary among X/Y/Z
        uint axisToStep = tMaxPerAxis.x < tMaxPerAxis.y
            ? (tMaxPerAxis.x < tMaxPerAxis.z ? 0 : 2)
            : (tMaxPerAxis.y < tMaxPerAxis.z ? 1 : 2);

        float tAtNextBoundary = tMaxPerAxis[axisToStep];

        float3 cellCenter = (float3(cellIndex) + 0.5) / gridResolution;

        // Cell test
        if (IsInsideGeometry(cellCenter, mipLevelToTest))
        {
            float3 unitHitPos = unitRayOrigin + unitRayDirection * tAlongUnitRay;

            MyCasperIntersectionAttributes attr;
            attr.UnitSpaceHitPosition = unitHitPos;
            attr.Face                 = DetermineEntryFace(unitHitPos, unitRayDirection, lastStepAxis);
            attr.MipLevel             = mipLevelToTest;

            float objectTHit = UnitTToObjectT(
                tAlongUnitRay,
                unitRayOrigin,
                unitRayDirection,
                aabb.Min,
                aabbExtent,
                objRayOrigin,
                objRayDir,
                objRayDirLenSq);

            ReportHit(objectTHit, /*hitKind*/ 0, attr);
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

        // Step to next cell
        cellIndex[axisToStep]   += cellStep[axisToStep];
        tMaxPerAxis[axisToStep] += tDeltaPerAxis[axisToStep];

        tAlongUnitRay = tAtNextBoundary;
        lastStepAxis  = (int) axisToStep;

        // Out of grid
        if (any(cellIndex < 0.xxx) ||
            any(cellIndex >= gridResolution.xxx))
        {
            break;
        }
    }

    // No hit
    return;
}

#endif // DFS_DDA

// ============================================================================
// Closest-hit shaders
// ============================================================================
//
// Radiance ray:
//   - Sample color from diffuse atlas at the hit mip level
//   - Reconstruct object-space normal from depth atlas
//   - Run PBR shading
//
// Shadow ray:
//   - Only store tHit (any hit blocks the light)
// ============================================================================

[shader("closesthit")]
void MyClosestHitShader_RadianceRay_Casper(
    inout RadiancePayload rayPayload,
    in MyCasperIntersectionAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    // Depth in clip space (for compositing / reprojection)
    float4 projPos = mul(float4(hitPosition, 1.0), g_ViewProj);
    projPos /= projPos.w;
    rayPayload.depth = saturate(projPos.z);

    // Decode face axes from face index
    int axisP = attr.Face / 2;
    int axisU = (axisP + 1) % 3;
    int axisV = (axisP + 2) % 3;

    bool bFaceSign = (attr.Face % 2) != 0;

    // Direction for sampling the cube textures
    float3 location = attr.UnitSpaceHitPosition * 2.0 - 1.0;
    location[axisP] = bFaceSign ? 1.0 : -1.0;

    // Sample diffuse color at the same mip level as the intersection
    float4 texDiffuse = l_DiffuseAtlasTexture.SampleLevel(
        g_SamplerPoint,
        normalize(location),
        attr.MipLevel);

    // Normal reconstruction from depth atlas around the hit
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

    // Two tangent vectors in object space
    float3 vec1 = UnitToObject(unitPos1) - UnitToObject(unitPos0);
    float3 vec2 = UnitToObject(unitPos3) - UnitToObject(unitPos2);

    // Winding is flipped depending on which side of the face we hit
    float3 objectNormal =
        bFaceSign ? normalize(cross(vec1, vec2)) :
                    normalize(cross(vec2, vec1));

    // Transform to world space
    float3 surfaceNormal = normalize(mul(objectNormal, (float3x3) ObjectToWorld4x3()));

    // Basic material parameters
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
    // For shadow rays we only care about the existence of a hit.
    rayPayload.tHit = RayTCurrent();
}

#endif // RAYTRACING_PROC_HLSL
