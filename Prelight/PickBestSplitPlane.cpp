#include "pch.h"
#include "SparseBinaryGrid.h"
#include "ConvexDecomposition.h"

// Return world-space center of voxel (x,y,z)
static inline FLOAT3 computeVoxelCenterWS(const SparseBinaryGrid& g, int x, int y, int z)
{
	const float h = g.GetCellSize();
	const FLOAT3 o = g.GetOrigin();
	return FLOAT3{ o.x + h * (x + 0.5f), o.y + h * (y + 0.5f), o.z + h * (z + 0.5f) };
}

// Compute concavity = |CH \ M| / |CH| using sparse grids (safe gap definition).
// TODO: GridConcavity.cpp으로 옮기기??
static double computeConcavityGapSafe(
	const SparseBinaryGrid& solidM,
	const SparseBinaryGrid& solidCH)
{
	const size_t chCount = solidCH.NumVoxels();
	if (chCount == 0)
	{
		return 0.0;
	}

	size_t gap = 0;
	// Iterate CH voxels and count those that are not in M
	solidCH.ForEachTile([&](uint64_t /*key*/, int chIdx, int tx, int ty, int tz)
		{
			const Brick& br = solidCH.GetBrick((size_t)chIdx);
			const int T = Brick::NUM_VOXELS_EDGE;
			const int bx = tx * T, by = ty * T, bz = tz * T;

			for (int lz = 0; lz < T; ++lz)
			{
				for (int ly = 0; ly < T; ++ly)
				{
					for (int lx = 0; lx < T; ++lx)
					{
						const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
						if (!br.GetBit(li)) continue;
						const int gx = bx + lx, gy = by + ly, gz = bz + lz;
						if (!solidM.GetVoxel(gx, gy, gz))
						{
							++gap;
						}
					}
				}
			}
		});
	return static_cast<double>(gap) / static_cast<double>(chCount);
}

// Compute integer-voxel AABB [min,max] for a solid grid.
static void computeGridBounds(const SparseBinaryGrid& solid, int3* outMin, int3* outMax)
{
	const int INF = (int)0x3f3f3f3f;
	int3 mn{ +INF,+INF,+INF };
	int3 mx{ -INF,-INF,-INF };

	solid.ForEachTile([&](uint64_t /*key*/, int idx, int tx, int ty, int tz)
		{
			const Brick& br = solid.GetBrick((size_t)idx);
			const int T = Brick::NUM_VOXELS_EDGE;
			const int bx = tx * T, by = ty * T, bz = tz * T;

			// Tighten by scanning bits inside brick
			for (int lz = 0; lz < T; ++lz)
			{
				for (int ly = 0; ly < T; ++ly)
				{
					for (int lx = 0; lx < T; ++lx)
					{
						const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
						if (!br.GetBit(li)) continue;
						const int gx = bx + lx, gy = by + ly, gz = bz + lz;
						mn.x = std::min(mn.x, gx);
						mn.y = std::min(mn.y, gy);
						mn.z = std::min(mn.z, gz);
						mx.x = std::max(mx.x, gx);
						mx.y = std::max(mx.y, gy);
						mx.z = std::max(mx.z, gz);
					}
				}
			}
		});

	if (mn.x == +0x3f3f3f3f) // empty guard
	{
		mn = int3{ 0,0,0 }; mx = int3{ -1,-1,-1 };
	}
	*outMin = mn; 
	*outMax = mx;
}

// Partition solid grid by plane → left/right parts (A/B). Also returns cutCount (optional metric).
struct Partition { SparseBinaryGrid A, B; uint64_t cutCount = 0; };

static Partition splitGridByPlane(const SparseBinaryGrid& solid, const Plane& P)
{
	Partition part;
	part.A.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());
	part.B.Reconfigure(solid.GetCellSize(), solid.GetOrigin(), solid.GetDim());

	const int T = Brick::NUM_VOXELS_EDGE;
	solid.ForEachTile([&](uint64_t /*key*/, int idx, int tx, int ty, int tz)
		{
			const Brick& br = solid.GetBrick((size_t)idx);
			const int bx = tx * T, by = ty * T, bz = tz * T;

			for (int lz = 0; lz < T; ++lz)
			{
				for (int ly = 0; ly < T; ++ly)
				{
					for (int lx = 0; lx < T; ++lx)
					{
						const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
						if (!br.GetBit(li)) continue;

						const int gx = bx + lx, gy = by + ly, gz = bz + lz;
						const FLOAT3 cw = computeVoxelCenterWS(solid, gx, gy, gz);
						const float side = FLOAT3::Dot(P.n, cw) - P.d;

						if (side >= 0.f) part.A.SetVoxel(gx, gy, gz, true);
						else             part.B.SetVoxel(gx, gy, gz, true);

						// Optional: count "cut" voxels if the plane is close to voxel center
						// (cheap proxy; could be refined with box-plane intersection)
						if (std::fabs(side) < 0.25f * solid.GetCellSize())
						{
							++part.cutCount;
						}
					}
				}
			}
		});

	return part;
}

// Score = concavity drop - penalties (balance, cut cost)
static double scoreSplit(
	const SparseBinaryGrid& whole,
	const SparseBinaryGrid& hull,
	const Partition& part,
	double lambdaBalance,
	double lambdaCut,
	double balanceTarget,
	double baselineConcavity,
	double* outDelta,
	double* outBalance)
{
	// concavity drop (using hull reused for both parts as an approximation)
	const double cA = computeConcavityGapSafe(part.A, hull);
	const double cB = computeConcavityGapSafe(part.B, hull);
	const double cSum = cA + cB;
	const double delta = std::max(0.0, baselineConcavity - cSum);

	// balance penalty: |ratio - target|
	const double vA = (double)part.A.NumVoxels();
	const double vB = (double)part.B.NumVoxels();
	const double vT = std::max(1.0, vA + vB);
	const double ratio = vA / vT;
	const double balPenalty = std::abs(ratio - balanceTarget);

	const double cutPenalty = (double)part.cutCount / vT;

	*outDelta = delta;
	*outBalance = ratio;
	return delta - lambdaBalance * balPenalty - lambdaCut * cutPenalty;
}

// Generate axis-aligned sweep plane candidates along the grid AABB.
// n in {+X,+Y,+Z}, d at N equal fractions between projected min/max.
static void generateAxisSweepCandidates(
	const SparseBinaryGrid& solid,
	int sweepsPerAxis,
	std::vector<Plane>* out)
{
	int3 mn, mx;
	computeGridBounds(solid, &mn, &mx);
	if (mx.x < mn.x)
	{
		return; // empty
	}

	const FLOAT3 axes[3] = { FLOAT3{1,0,0}, FLOAT3{0,1,0}, FLOAT3{0,0,1} };
	const float h = solid.GetCellSize();
	const FLOAT3 o = solid.GetOrigin();

	// Project min/max (world) along each axis
	const float amin[3] = 
	{
		o.x + h * (mn.x + 0.5f),
		o.y + h * (mn.y + 0.5f),
		o.z + h * (mn.z + 0.5f)
	};
	const float amax[3] = 
	{
		o.x + h * (mx.x + 0.5f),
		o.y + h * (mx.y + 0.5f),
		o.z + h * (mx.z + 0.5f)
	};

	for (int a = 0; a < 3; ++a)
	{
		const FLOAT3 n = axes[a];
		for (int i = 1; i <= sweepsPerAxis; ++i)
		{
			const float t = float(i) / float(sweepsPerAxis + 1);
			const float s = amin[a] * (1.0f - t) + amax[a] * t; // position along axis in world
			out->emplace_back(Plane{ n, s });
		}
	}
}

// ---------- Main entry ----------

std::optional<Plane> PickBestSplitPlane(
	const SparseBinaryGrid& solid,
	const SparseBinaryGrid& hull,
	double concavityThreshold,
	size_t minVoxelsPerPart)
{
	// Early exit: if current concavity already small enough, do not split.
	const double cWhole = computeConcavityGapSafe(solid, hull);
	if (cWhole <= concavityThreshold)
		return std::nullopt;

	// Candidate generation (simple & robust: axis sweeps). You can add more sources later.
	std::vector<Plane> candidates;
	candidates.reserve(128);
	constexpr int kSweepsPerAxis = 16; // tune
	generateAxisSweepCandidates(solid, kSweepsPerAxis, &candidates);
	if (candidates.empty())
		return std::nullopt;

	// Scoring weights (tune to taste)
	constexpr double kLambdaBalance = 0.25; // penalize unbalanced splits
	constexpr double kLambdaCut = 0.00; // enable if you want to penalize cut length
	constexpr double kBalanceTarget = 0.5;

	// Evaluate candidates
	double bestScore = -std::numeric_limits<double>::infinity();
	Plane  bestPlane{};
	bool   found = false;

	for (const Plane& P : candidates)
	{
		Partition part = splitGridByPlane(solid, P);

		// Hard constraint: minimum voxels per part
		if (part.A.NumVoxels() < minVoxelsPerPart) continue;
		if (part.B.NumVoxels() < minVoxelsPerPart) continue;

		double delta = 0.0, balance = 0.0;
		const double score = scoreSplit(
			solid, hull, part,
			kLambdaBalance, kLambdaCut, kBalanceTarget, cWhole,
			&delta, &balance);

		if (score > bestScore)
		{
			bestScore = score;
			bestPlane = P;
			found = true;
		}
	}

	// If no improving split found, or improvement is negligible, stop.
	if (!found) return std::nullopt;

	// Optional: require that the best split brings combined concavity close to threshold
	// (commented out; enable if you want stricter stopping)
	// Partition bestPart = SplitGridByPlane(solid, bestPlane);
	// double cA = ComputeConcavityGapSafe(bestPart.A, hull);
	// double cB = ComputeConcavityGapSafe(bestPart.B, hull);
	// if (cA <= concavityThreshold && cB <= concavityThreshold)
	//     return bestPlane;

	return bestPlane;
}