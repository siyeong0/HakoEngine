// PickBestSplitPlane.cpp
#include "pch.h"
#include "SparseBinaryGrid.h"
#include "Voxelize.h"
#include "QuickHull.h"
#include "ConvexDecomposition.h"

static inline FLOAT3 ComputeVoxelCenterWS(const SparseBinaryGrid& g, int x, int y, int z) {
    const float h = g.GetCellSize(); const FLOAT3 o = g.GetOrigin();
    return { o.x + h * (x + 0.5f), o.y + h * (y + 0.5f), o.z + h * (z + 0.5f) };
}

static double ComputeConcavityGapSafe(const SparseBinaryGrid& solidM, const SparseBinaryGrid& solidCH)
{
    const size_t chCount = solidCH.NumVoxels(); if (!chCount) return 0.0;
    size_t gap = 0;
    auto Fn = [&](uint64_t, int chIdx, int tx, int ty, int tz) {
        const Brick& br = solidCH.GetBrick((size_t)chIdx);
        const int T = Brick::NUM_VOXELS_EDGE, bx = tx * T, by = ty * T, bz = tz * T;
        for (int lz = 0; lz < T; ++lz) for (int ly = 0; ly < T; ++ly) for (int lx = 0; lx < T; ++lx) {
            const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
            if (!br.GetBit(li)) continue;
            const int gx = bx + lx, gy = by + ly, gz = bz + lz;
            if (!solidM.GetVoxel(gx, gy, gz)) ++gap;
        }
        };
    solidCH.ForEachTile(Fn);
    return (double)gap / (double)chCount;
}

static void ComputeGridBounds(const SparseBinaryGrid& solid, int3* mn, int3* mx)
{
    const int INF = 0x3f3f3f3f; *mn = { +INF,+INF,+INF }; *mx = { -INF,-INF,-INF };
    auto Fn = [&](uint64_t, int idx, int tx, int ty, int tz) {
        const Brick& br = solid.GetBrick((size_t)idx);
        const int T = Brick::NUM_VOXELS_EDGE, bx = tx * T, by = ty * T, bz = tz * T;
        for (int lz = 0; lz < T; ++lz) for (int ly = 0; ly < T; ++ly) for (int lx = 0; lx < T; ++lx) {
            const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
            if (!br.GetBit(li)) continue;
            const int gx = bx + lx, gy = by + ly, gz = bz + lz;
            mn->x = std::min(mn->x, gx); mn->y = std::min(mn->y, gy); mn->z = std::min(mn->z, gz);
            mx->x = std::max(mx->x, gx); mx->y = std::max(mx->y, gy); mx->z = std::max(mx->z, gz);
        }
        };
    solid.ForEachTile(Fn);
    if (mn->x == +INF) { *mn = { 0,0,0 }; *mx = { -1,-1,-1 }; }
}

static void GenerateAxisSweepCandidates(const SparseBinaryGrid& solid, int sweepsPerAxis, std::vector<Plane>* out)
{
    int3 mn, mx; ComputeGridBounds(solid, &mn, &mx); if (mx.x < mn.x) return;
    const FLOAT3 axes[3] = { {1,0,0},{0,1,0},{0,0,1} };
    const float h = solid.GetCellSize(); const FLOAT3 o = solid.GetOrigin();
    const float amin[3] = { o.x + h * (mn.x + 0.5f), o.y + h * (mn.y + 0.5f), o.z + h * (mn.z + 0.5f) };
    const float amax[3] = { o.x + h * (mx.x + 0.5f), o.y + h * (mx.y + 0.5f), o.z + h * (mx.z + 0.5f) };
    for (int a = 0; a < 3; ++a) {
        const FLOAT3 n = axes[a];
        for (int i = 1; i <= sweepsPerAxis; ++i) {
            const float t = float(i) / float(sweepsPerAxis + 1);
            const float s = amin[a] * (1.0f - t) + amax[a] * t;
            out->emplace_back(Plane{ n,s });
        }
    }
}

static SparseBinaryGrid MakeSolidHullFor(const SparseBinaryGrid& solid)
{
    // 외곽 헐: 기존 QuickHull → Voxelize → Solid 방식 재사용
    std::vector<FLOAT3> verts; std::vector<uint32_t> inds;
    QuickHull(solid, &verts, &inds);
    SparseBinaryGrid hullSurf, hull;
    VoxelizeToSparse(verts, inds, Bounds{/*unused path → meshBounds 없음*/ }, solid.GetCellSize(), &hullSurf);
    MakeSolidFromSurfaceSparse(hullSurf, &hull);
    return hull;
}

static void SplitByPlane(const SparseBinaryGrid& solid, const Plane& P, float bandWS,
    SparseBinaryGrid* outA, SparseBinaryGrid* outB, uint64_t* cutCount)
{
    outA->Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());
    outB->Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());
    *cutCount = 0;
    auto Fn = [&](uint64_t, int idx, int tx, int ty, int tz) {
        const Brick& br = solid.GetBrick((size_t)idx);
        const int T = Brick::NUM_VOXELS_EDGE, bx = tx * T, by = ty * T, bz = tz * T;
        for (int lz = 0; lz < T; ++lz) for (int ly = 0; ly < T; ++ly) for (int lx = 0; lx < T; ++lx) {
            const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
            if (!br.GetBit(li)) continue;
            const int gx = bx + lx, gy = by + ly, gz = bz + lz;
            const FLOAT3 c = ComputeVoxelCenterWS(solid, gx, gy, gz);
            const float side = FLOAT3::Dot(P.n, c) - P.d;
            if (side >= 0) outA->SetVoxel(gx, gy, gz, true); else outB->SetVoxel(gx, gy, gz, true);
            if (std::fabs(side) < bandWS) ++(*cutCount);
        }
        };
    solid.ForEachTile(Fn);
}

static double Score(const SparseBinaryGrid& whole,
    const SparseBinaryGrid& hull,
    const SparseBinaryGrid& A,
    const SparseBinaryGrid& B,
    uint64_t cutCount,
    const SplitParams& params,
    const SplitWeights& W,
    double cWhole,
    double* outRatio)
{
    const double cA = ComputeConcavityGapSafe(A, hull);
    const double cB = ComputeConcavityGapSafe(B, hull);
    const double cSum = cA + cB;
    const double delta = std::max(0.0, cWhole - cSum);

    const double vA = (double)A.NumVoxels();
    const double vB = (double)B.NumVoxels();
    const double vT = std::max(1.0, vA + vB);
    const double ratio = vA / vT;
    const double balPenalty = std::abs(ratio - params.BalanceTarget);
    const double cutPenalty = (double)cutCount / vT;

    if (outRatio) *outRatio = ratio;
    return (delta)-(W.LambdaBalance * balPenalty + W.LambdaCut * cutPenalty);
}

std::optional<SplitResult>
PickBestSplitPlaneEx(const SparseBinaryGrid& solid,
    const SparseBinaryGrid& hull,
    const SplitParams& params,
    const SplitWeights& weights)
{
    // 전체 오목도가 이미 충분히 작으면 종료
    const double cWhole = ComputeConcavityGapSafe(solid, hull);
    if (cWhole <= params.ConcavityThreshold) return std::nullopt;

    std::vector<Plane> cand; cand.reserve(3 * params.SweepsPerAxis);
    GenerateAxisSweepCandidates(solid, params.SweepsPerAxis, &cand);
    if (cand.empty()) return std::nullopt;

    const float bandWS = (float)params.CutBandGrid * solid.GetCellSize();

    double bestScore = -std::numeric_limits<double>::infinity();
    SplitResult best;

    for (const Plane& P : cand)
    {
        SparseBinaryGrid A, B; uint64_t cutCount = 0;
        SplitByPlane(solid, P, bandWS, &A, &B, &cutCount);

        const size_t nA = A.NumVoxels(), nB = B.NumVoxels();
        if (nA < params.MinVoxelsPerPart || nB < params.MinVoxelsPerPart) continue;

        double ratio = 0.0;
        const double sc = Score(solid, hull, A, B, cutCount, params, weights, cWhole, &ratio);
        if (std::abs(ratio - params.BalanceTarget) > params.EarlyRejectBalance) continue;

        if (sc > bestScore) {
            bestScore = sc; best.plane = P; best.A = std::move(A); best.B = std::move(B);
            best.ratio = ratio;
            best.cA = ComputeConcavityGapSafe(best.A, hull);
            best.cB = ComputeConcavityGapSafe(best.B, hull);
        }
    }

    if (bestScore == -std::numeric_limits<double>::infinity()) return std::nullopt;
    best.score = bestScore;
    return best;
}
