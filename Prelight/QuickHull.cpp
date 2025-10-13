#include "pch.h"
#include "FaceLayer1024.h"
#include "SparseBinaryGrid.h"
#include "QuickHull.h"

// ===================== Math helpers =====================
// Lightweight double-precision vector (no external deps).
// - Operations keep code self-contained and deterministic.
// - Used for robustness inside hull computations (input is FLOAT3).
struct DOUBLE3
{
	double x = 0, y = 0, z = 0;
	DOUBLE3() = default; DOUBLE3(double X, double Y, double Z) :x(X), y(Y), z(Z) {}
	DOUBLE3 operator+(const DOUBLE3& b) const { return { x + b.x,y + b.y,z + b.z }; }
	DOUBLE3 operator-(const DOUBLE3& b) const { return { x - b.x,y - b.y,z - b.z }; }
	DOUBLE3 operator*(double s) const { return { x * s,y * s,z * s }; }
	DOUBLE3 operator/(double s) const { return { x / s,y / s,z / s }; }

	static inline double Dot(const DOUBLE3& a, const DOUBLE3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	static inline DOUBLE3 Cross(const DOUBLE3& a, const DOUBLE3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	static inline double Magnitude(const DOUBLE3& v) { return Dot(v, v); }
	static inline double Length(const DOUBLE3& v) { return std::sqrt(Magnitude(v)); }
	static inline DOUBLE3 Normalize(const DOUBLE3& v) { double L = Length(v); return (L > 0) ? v * (1.0 / L) : DOUBLE3{}; }
};

// Conversion helpers between app-level FLOAT3 and internal DOUBLE3
static inline DOUBLE3 cvtF3toD3(const FLOAT3& p) { return { (double)p.x,(double)p.y,(double)p.z }; }
static inline FLOAT3 cvtD3toF3(const DOUBLE3& p) { return { (float)p.x,(float)p.y,(float)p.z }; }

// ===================== Face =====================
// Convex hull triangle face:
// - a,b,c: CCW order as seen from outside the hull
// - n,d: plane equation n·x + d = 0 with unit-length n
struct Face
{
	uint32_t a, b, c; // CCW, outward normal
	DOUBLE3 n; double d; // n·x + d = 0
};

// Construct a face from three vertex indices (i,j,k) into point array P.
// Returns degenerate face (zero normal) if triangle area = 0.
static inline Face makeFace(uint32_t i, uint32_t j, uint32_t k, const std::vector<DOUBLE3>& P)
{
	const DOUBLE3& A = P[i];
	const DOUBLE3& B = P[j];
	const DOUBLE3& C = P[k];
	DOUBLE3 n = DOUBLE3::Cross(B - A, C - A);
	double L = DOUBLE3::Length(n);
	if (L == 0) return { i,j,k,{0,0,0},0 };
	n = n * (1.0 / L);
	double d = -DOUBLE3::Dot(n, A);
	return { i,j,k,n,d };
}

// Signed distance from point p to the face plane.
// > 0 : point is in front of the face (in the direction of n)
// < 0 : point is behind the face
static inline double faceDist(const Face& f, const DOUBLE3& p) { return DOUBLE3::Dot(f.n, p) + f.d; }

// ===================== Eps =====================
// Scale-aware tolerances used for robustness.
// - PointEpsilon: collinearity/coplanarity checks
// - PlaneEpsilon: visibility threshold for faces (apex-point tests)
struct ScaleEps
{
	double PointEpsilon = 1e-12;
	double PlaneEpsilon = 1e-12;
};

// Build epsilon values based on input bounding box diagonal.
static ScaleEps makeScaleEps(const std::vector<DOUBLE3>& P)
{
	DOUBLE3 mn{ +INFINITY,+INFINITY,+INFINITY }, mx{ -INFINITY,-INFINITY,-INFINITY };
	for (auto& p : P)
	{
		mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
		mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
	}
	double diag = DOUBLE3::Length(mx - mn);
	ScaleEps e;
	e.PointEpsilon = std::max(1e-12, diag * 1e-12);
	e.PlaneEpsilon = std::max(1e-12, diag * 1e-12); // use relatively tight threshold
	return e;
}

// ===================== Initial tetra =====================
// Pick four non-degenerate points to form an initial tetrahedron:
// 1) Choose min/max by x to get a long segment (separation).
// 2) Pick the point farthest from that segment to form a wide triangle.
// 3) Pick the point farthest from that triangle plane to form a full tetra.
// If any step degenerates, return invalid indices (~0u).
static std::array<uint32_t, 4> buildInitialTetra(const std::vector<DOUBLE3>& P, const ScaleEps& eps)
{
	const uint32_t N = (uint32_t)P.size();
	if (N < 4)
	{
		return { ~0u,~0u,~0u,~0u };
	}

	uint32_t iMin = 0, iMax = 0;
	for (uint32_t i = 1; i < N; ++i)
	{
		if (P[i].x < P[iMin].x) iMin = i;
		if (P[i].x > P[iMax].x) iMax = i;
	}
	if (iMin == iMax)
	{
		return { ~0u,~0u,~0u,~0u };
	}

	// Farthest from segment (maximize area wrt AB)
	DOUBLE3 A = P[iMin], B = P[iMax], AB = B - A;
	uint32_t i2 = ~0u; double bestArea2 = 0;
	for (uint32_t i = 0; i < N; ++i)
	{
		if (i == iMin || i == iMax) continue;
		double a2 = DOUBLE3::Magnitude(DOUBLE3::Cross(AB, P[i] - A));
		if (a2 > bestArea2)
		{
			bestArea2 = a2;
			i2 = i;
		}
	}
	if (i2 == ~0u || std::sqrt(bestArea2) <= eps.PointEpsilon)
	{
		// Collinear with AB
		return { ~0u,~0u,~0u,~0u };
	}

	// Farthest from plane (maximize distance to plane (A,B,P[i2]))
	DOUBLE3 n = DOUBLE3::Normalize(DOUBLE3::Cross(P[iMax] - A, P[i2] - A));
	double d = -DOUBLE3::Dot(n, A);
	uint32_t i3 = ~0u; double best = 0;
	for (uint32_t i = 0; i < N; ++i)
	{
		if (i == iMin || i == iMax || i == i2) continue;
		double dist = std::abs(DOUBLE3::Dot(n, P[i]) + d);
		if (dist > best)
		{
			best = dist;
			i3 = i;
		}
	}
	if (i3 == ~0u || best <= eps.PlaneEpsilon)
	{
		// Coplanar with the triangle
		return { ~0u,~0u,~0u,~0u };
	}

	// Make orientation consistent so that i3 is outside the base face (positive side).
	// If not, swap min/max to flip the base orientation — this keeps outward normals consistent.
	double side = DOUBLE3::Dot(n, P[i3]) + d;
	if (side > 0.0)
	{
		std::swap(iMin, iMax);
	}

	return { iMin,iMax,i2,i3 };
}

// Compute the centroid of the initial tetrahedron; used as a stable interior reference point
// (we enforce all faces to see this point on their negative side → outward normals).
static DOUBLE3 tetraCenter(const std::array<uint32_t, 4>& T, const std::vector<DOUBLE3>& P)
{
	DOUBLE3 c{};
	for (int k = 0; k < 4; ++k)
	{
		c = c + P[T[k]];
	}
	return c * (1.0 / 4.0);
}

// Build the initial 4 faces and enforce outward orientation
// by checking the signed distance to insideRef (interior point).
static std::vector<Face> makeInitialHull(
	const std::array<uint32_t, 4>& T,
	const std::vector<DOUBLE3>& P,
	const DOUBLE3& insideRef)
{
	const uint32_t i0 = T[0];
	const uint32_t i1 = T[1];
	const uint32_t i2 = T[2];
	const uint32_t i3 = T[3];

	std::vector<Face> F;
	F.reserve(4);

	Face f0 = makeFace(i0, i1, i2, P);
	Face f1 = makeFace(i0, i1, i3, P);
	Face f2 = makeFace(i1, i2, i3, P);
	Face f3 = makeFace(i2, i0, i3, P);

	// If insideRef lies on the positive side, the face normal points inward.
	// Swap (b,c) to flip winding and recompute the face to point outward.
	auto enforceOut = [&](Face& f)
		{
			if (faceDist(f, insideRef) > 0.0)
			{
				std::swap(f.b, f.c);
				f = makeFace(f.a, f.b, f.c, P);
			}
		};
	enforceOut(f0);
	enforceOut(f1);
	enforceOut(f2);
	enforceOut(f3);

	F.emplace_back(f0);
	F.emplace_back(f1);
	F.emplace_back(f2);
	F.emplace_back(f3);
	return F;
}

// ===================== Adjacency =====================
// Build an undirected edge key by sorting endpoints (a<=b).
// This maps an edge to the list of adjacent faces sharing it.
static inline uint64_t hashEdge(uint32_t a, uint32_t b)
{
	if (a > b) std::swap(a, b);
	return (uint64_t(a) << 32) | uint64_t(b);
}
using EdgeAdj = std::unordered_map<uint64_t, std::vector<int>>;

// Build adjacency (edge → faces).
// This is used to expand visible faces by BFS and to detect horizon edges.
static EdgeAdj buildAdj(const std::vector<Face>& faces)
{
	EdgeAdj adj;
	adj.reserve(faces.size() * 2);
	auto add = [&](uint32_t u, uint32_t v, int fi)
		{
			adj[hashEdge(u, v)].emplace_back(fi);
		};
	for (int fi = 0; fi < (int)faces.size(); ++fi)
	{
		const Face& f = faces[fi];
		if (DOUBLE3::Magnitude(f.n) == 0) continue;
		add(f.a, f.b, fi);
		add(f.b, f.c, fi);
		add(f.c, f.a, fi);
	}
	return adj;
}

// BFS from a seed face to collect the entire set of faces visible from 'apex'.
// A face is "visible" if its signed distance to 'apex' is > PlaneEpsilon.
static std::vector<int> collectVisibleFacesBFS(
	const std::vector<Face>& faces,
	const EdgeAdj& adj,
	int seedFace, uint32_t apex,
	const std::vector<DOUBLE3>& P,
	const ScaleEps& eps)
{
	std::vector<int> vis;
	std::vector<char> seen(faces.size(), 0);
	std::queue<int> q;
	auto isVisible = [&](int fi) { return faceDist(faces[fi], P[apex]) > eps.PlaneEpsilon; };

	if (seedFace >= 0 && isVisible(seedFace))
	{
		q.push(seedFace);
		seen[seedFace] = 1;
	}

	while (!q.empty())
	{
		int fi = q.front(); q.pop();
		vis.emplace_back(fi);
		const Face& f = faces[fi];
		uint64_t ekeys[3] = { hashEdge(f.a,f.b), hashEdge(f.b,f.c), hashEdge(f.c,f.a) };
		for (uint64_t k : ekeys)
		{
			auto it = adj.find(k); if (it == adj.end()) continue;
			for (int nb : it->second)
			{
				if (!seen[nb] && isVisible(nb))
				{
					seen[nb] = 1; q.emplace(nb);
				}
			}
		}
	}
	return vis;
}

// Build the "horizon" loop, i.e., the set of directed edges that separate
// visible faces from non-visible faces as seen from the current apex.
// Direction convention: keep the orientation as it appears in the visible face (u->v).
static std::vector<std::pair<uint32_t, uint32_t>> buildHorizon(
	const std::vector<Face>& faces,
	const EdgeAdj& adj,
	const std::vector<int>& visible)
{
	std::vector<bool> bVisible(faces.size(), false);
	for (int fi : visible)
	{
		if (fi >= 0 && fi < (int)faces.size())
		{
			bVisible[fi] = true;
		}
	}

	// Collect boundary edges (edges with one visible and one non-visible neighbor).
	struct HE { uint32_t u, v; int f; };
	std::vector<HE> hedges; hedges.reserve(visible.size() * 2);
	for (int fi : visible)
	{
		const Face& f = faces[fi];
		std::array<std::pair<uint32_t, uint32_t>, 3> E = { {{f.a,f.b},{f.b,f.c},{f.c,f.a}} };
		for (auto [u, v] : E)
		{
			auto it = adj.find(hashEdge(u, v));
			int other = -1;
			if (it != adj.end())
			{
				for (int f2 : it->second)
				{
					if (f2 != fi)
					{
						other = f2;
						break;
					}
				}
			}
			if (other == -1 || !bVisible[other])
			{
				// This is a boundary (horizon) edge. Keep (u->v) direction from the visible face.
				hedges.emplace_back(u, v, fi);
			}
		}
	}

	// Try to order horizon edges into a single loop for stable triangulation.
	// Fallback: if ordering fails (degenerate cases), return raw edges.
	std::unordered_multimap<uint32_t, HE*> out;
	out.reserve(hedges.size());
	for (auto& e : hedges)
	{
		out.emplace(e.u, &e);
	}

	std::vector<std::pair<uint32_t, uint32_t>> loop;
	loop.reserve(hedges.size());
	if (!hedges.empty())
	{
		HE* start = &hedges[0];
		loop.emplace_back(start->u, start->v);
		uint32_t curr = start->v;
		for (size_t i = 1; i < hedges.size() + 4; ++i)
		{
			auto range = out.equal_range(curr);
			HE* next = nullptr;
			for (auto it = range.first; it != range.second; ++it)
			{
				if (it->second->u == curr)
				{
					next = it->second;
					break;
				}
			}
			if (!next) break;
			loop.emplace_back(next->u, next->v);
			curr = next->v;
			if (curr == loop.front().first) break; // closed loop
		}
	}
	if (loop.empty())
	{
		// Loop ordering failed (rare numeric cases) — use the raw set as-is.
		for (auto& e : hedges)
		{
			loop.emplace_back(e.u, e.v);
		}
	}
	return loop;
}

// ===================== Farthest point search =====================
// For all current faces, find the globally farthest point (by signed distance)
// that lies outside the hull (> PlaneEpsilon).
// Returns face index that "sees" the point, and point index.
struct MaxOutInfo { double dist = 0; int face = -1; uint32_t pid = 0; };
static MaxOutInfo findFarthest(
	const std::vector<Face>& faces,
	const std::vector<DOUBLE3>& P,
	const ScaleEps& eps)
{
	MaxOutInfo info;
	for (int fi = 0; fi < (int)faces.size(); ++fi)
	{
		const Face& f = faces[fi];
		if (DOUBLE3::Magnitude(f.n) == 0) continue;

		double best = 0; uint32_t idx = ~0u;
		for (uint32_t i = 0; i < P.size(); ++i)
		{
			double d = faceDist(f, P[i]);
			if (d > eps.PlaneEpsilon && d > best)
			{
				best = d;
				idx = i;
			}
		}
		if (idx != ~0u && best > info.dist)
		{
			info = { best,fi,idx };
		}
	}
	return info;
}

// Pack 3×21-bit signed ints with +2^20 bias (same scheme as your grid keys).
static inline uint64_t packCornerKey(int x, int y, int z)
{
	auto pack = [](int v)->uint64_t { return (uint64_t)(v + (1 << 20)) & ((1ull << 21) - 1ull); };
	return (pack(x) << 42) | (pack(y) << 21) | pack(z);
}

// Returns a 6-bit mask of exposed faces for a set voxel at local (lx,ly,lz).
// Bit order: 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z (1 = face open to exterior).
static inline uint8_t computeExposureMask6(
	const Brick* self,
	const Brick* nb[6],
	int lx, int ly, int lz) // local [0,32)
{
	const int T = Brick::NUM_VOXELS_EDGE;
	uint8_t m = 0;

	// +X
	if (lx + 1 >= T)
	{
		if (!nb[0]) m |= 1 << 0;
		else if (!nb[0]->GetBit(SparseBinaryGrid::GetLocalIndex(0, ly, lz))) m |= 1 << 0;
	}
	else
	{
		if (!self->GetBit(SparseBinaryGrid::GetLocalIndex(lx + 1, ly, lz))) m |= 1 << 0;
	}
	// -X
	if (lx - 1 < 0)
	{
		if (!nb[1]) m |= 1 << 1;
		else if (!nb[1]->GetBit(SparseBinaryGrid::GetLocalIndex(T - 1, ly, lz))) m |= 1 << 1;
	}
	else
	{
		if (!self->GetBit(SparseBinaryGrid::GetLocalIndex(lx - 1, ly, lz))) m |= 1 << 1;
	}
	// +Y
	if (ly + 1 >= T)
	{
		if (!nb[2]) m |= 1 << 2;
		else if (!nb[2]->GetBit(SparseBinaryGrid::GetLocalIndex(lx, 0, lz))) m |= 1 << 2;
	}
	else
	{
		if (!self->GetBit(SparseBinaryGrid::GetLocalIndex(lx, ly + 1, lz))) m |= 1 << 2;
	}
	// -Y
	if (ly - 1 < 0)
	{
		if (!nb[3]) m |= 1 << 3;
		else if (!nb[3]->GetBit(SparseBinaryGrid::GetLocalIndex(lx, T - 1, lz))) m |= 1 << 3;
	}
	else
	{
		if (!self->GetBit(SparseBinaryGrid::GetLocalIndex(lx, ly - 1, lz))) m |= 1 << 3;
	}
	// +Z
	if (lz + 1 >= T)
	{
		if (!nb[4]) m |= 1 << 4;
		else if (!nb[4]->GetBit(SparseBinaryGrid::GetLocalIndex(lx, ly, 0))) m |= 1 << 4;
	}
	else
	{
		if (!self->GetBit(SparseBinaryGrid::GetLocalIndex(lx, ly, lz + 1))) m |= 1 << 4;
	}
	// -Z
	if (lz - 1 < 0)
	{
		if (!nb[5]) m |= 1 << 5;
		else if (!nb[5]->GetBit(SparseBinaryGrid::GetLocalIndex(lx, ly, T - 1))) m |= 1 << 5;
	}
	else
	{
		const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz - 1);
		if (!self->GetBit(li)) m |= 1 << 5;
	}
	return m;
}

// Gathers a deduplicated set of world-space corners from boundary voxels in a sparse grid.
// Intended for convex hull input; corners are derived from exposed faces only.
std::vector<FLOAT3> collectHullCornersFromSparseGrid(const SparseBinaryGrid& grid)
{
	const int T = Brick::NUM_VOXELS_EDGE;
	const float h = grid.GetCellSize();
	const FLOAT3& O = grid.GetOrigin();

	std::unordered_set<uint64_t> cornerSet;
	cornerSet.reserve(grid.Size() * 64); // heuristic

	// Reusable helpers for face work
	const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
	const FACE_DIR facePos[6] = { POSX, NEGX, POSY, NEGY, POSZ, NEGZ };
	const FACE_DIR faceNeg[6] = { NEGX, POSX, NEGY, POSY, NEGZ, POSZ };

	grid.ForEachTile([&](uint64_t /*key*/, int brickIdx, int tx, int ty, int tz)
		{
			const Brick& B = grid.GetBrick((size_t)brickIdx);
			const int baseX = tx * T, baseY = ty * T, baseZ = tz * T;

			// Cache neighbor bricks per face (for boundary tests / exposure)
			const Brick* NB[6] = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr };
			for (int k = 0; k < 6; ++k)
			{
				const int ntx = tx + d6[k][0], nty = ty + d6[k][1], ntz = tz + d6[k][2];
				const int ni = grid.FindTileIndex(ntx, nty, ntz);
				if (ni >= 0)
				{
					NB[k] = &grid.GetBrick((size_t)ni);
				}
			}

			if (B.Mode == Brick::FULL)
			{
				// Only faces can be boundary. Build exposed 32x32 masks per face.
				// exposed = currentface(=all 1s) minus neighborFaceOpp
				for (int k = 0; k < 6; ++k)
				{
					// current face layer: all ones for FULL (32x32)
					// neighbor face layer: extract from NB[k] if BITSET, else:
					//   - if NB[k]==nullptr => exposed = all ones
					//   - if NB[k] FULL     => exposed = zeros
					FaceLayer1024 exposed;
					if (!NB[k])
					{
						exposed.SetAll();
					}
					else if (NB[k]->Mode == Brick::FULL)
					{
						exposed.Clear();
					}
					else
					{
						FaceLayer1024 nlayer; extractFaceLayer(*NB[k], faceNeg[k], nlayer);
						// FULL face minus neighbor occupancy → bitwise NOT(nei) masked to 1024 bits
						exposed = FaceLayer1024::BitwiseNOT(nlayer);
					}

					if (exposed.IsZero()) continue;

					// For every exposed face cell, emit its 4 corners.
					// Map face-cell (u,v) to global (gx,gy,gyz) depending on face k.
					for (int v = 0; v < T; ++v)
					{
						for (int u = 0; u < T; ++u)
						{
							if (!exposed.Get(u, v)) continue;

							// Determine the voxel on this face (lx,ly,lz) and then emit its corners.
							int lx = 0, ly = 0, lz = 0;
							switch (k)
							{
							case 0: lx = T - 1; ly = v; lz = u; break; // +X face
							case 1: lx = 0;   ly = v; lz = u; break; // -X face
							case 2: lx = u;   ly = T - 1; lz = v; break; // +Y
							case 3: lx = u;   ly = 0;   lz = v; break; // -Y
							case 4: lx = u;   ly = v;   lz = T - 1; break; // +Z
							default:lx = u;   ly = v;   lz = 0; break;   // -Z
							}
							const int gx = baseX + lx;
							const int gy = baseY + ly;
							const int gz = baseZ + lz;

							// Insert 4 corners on the face (8 corners would duplicate the interior deeply).
							// It’s fine to emit 8 as well; the set will deduplicate. 4 is a bit cheaper.
							for (int sx = 0; sx <= 1; ++sx)
							{
								for (int sy = 0; sy <= 1; ++sy)
								{
									// Choose the two varying axes on this face:
									// For +X/-X, vary (y,z). For +Y/-Y, vary (x,z). For +Z/-Z, vary (x,y).
									int cx = gx + (k == 0 ? 1 : (k == 1 ? 0 : sx));
									int cy = gy + (k == 2 ? 1 : (k == 3 ? 0 : (k >= 4 ? sy : sy)));
									int cz;
									if (k == 4)			cz = gz + 1;
									else if (k == 5)	cz = gz + 0;
									else				cz = gz + (k <= 1 ? sy : sx);

									cornerSet.insert(packCornerKey(cx, cy, cz));
								}
							}
						}
					}
				}
				return; // FULL brick done
			}

			// BITSET brick: iterate only set voxels (word-scan). You need a small
			// iterator; below is schematic—adapt to your Brick bit layout.
			for (int lz = 0; lz < T; ++lz)
			{
				for (int ly = 0; ly < T; ++ly)
				{
					// Assume Brick exposes row-wise 32-bit/64-bit words; otherwise fall back to lx loop
					for (int lx = 0; lx < T; ++lx)
					{
						const uint16_t li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
						if (!B.GetBit(li)) continue;

						// exposure mask for this boundary voxel; skip if fully internal
						const uint8_t mask = computeExposureMask6(&B, NB, lx, ly, lz);
						if (!mask) continue;

						const int gx = baseX + lx, gy = baseY + ly, gz = baseZ + lz;

						// required side per axis: -1 = free, 0 = negative side, 1 = positive side
						const int reqX = (mask & (1 << 0)) ? 1 : ((mask & (1 << 1)) ? 0 : -1);
						const int reqY = (mask & (1 << 2)) ? 1 : ((mask & (1 << 3)) ? 0 : -1);
						const int reqZ = (mask & (1 << 4)) ? 1 : ((mask & (1 << 5)) ? 0 : -1);

						// Emit only outward corners that satisfy all required sides.
						for (int sx = 0; sx <= 1; ++sx)
						{
							if (reqX != -1 && sx != reqX) continue;
							for (int sy = 0; sy <= 1; ++sy)
							{
								if (reqY != -1 && sy != reqY) continue;
								for (int sz = 0; sz <= 1; ++sz)
								{
									if (reqZ != -1 && sz != reqZ) continue;
									cornerSet.insert(packCornerKey(gx + sx, gy + sy, gz + sz));
								}
							}
						}
					}
				}
			}
		});

	// Convert to world coordinates
	std::vector<FLOAT3> pts; pts.reserve(cornerSet.size());
	for (uint64_t key : cornerSet)
	{
		// Unpack (inverse of PackCornerKey)
		auto unpack = [](uint64_t v)->int
			{
				int s = (int)(v & ((1ull << 21) - 1ull));
				return s - (1 << 20);
			};
		const int kz = unpack(key);
		const int ky = unpack(key >> 21);
		const int kx = unpack(key >> 42);
		pts.emplace_back(
			O.x + h * (float)kx,
			O.y + h * (float)ky,
			O.z + h * (float)kz);
	}
	return pts;
}

// Main entry (voxel-grid version):
// - Extracts a sparse set of surface voxel corners and computes convex hull over them.
// - Delegates to point-set QuickHull below.
void QuickHull(
	const SparseBinaryGrid& voxelGrid,
	std::vector<FLOAT3>* outVertices,
	std::vector<uint32_t>* outIndices)
{
	const std::vector<FLOAT3> points = collectHullCornersFromSparseGrid(voxelGrid);
	QuickHull(points, outVertices, outIndices);
}

// Main entry (point-set version):
// - Builds initial tetrahedron, then iteratively:
//   1) Find farthest outside point (apex).
//   2) Collect all faces visible from apex via BFS (using adjacency).
//   3) Build horizon (visible/non-visible boundary).
//   4) Remove visible faces; add new faces (horizon edges + apex).
// - Emits CCW triangles with outward normals.
void QuickHull(const std::vector<FLOAT3>& points,
	std::vector<FLOAT3>* outVertices,
	std::vector<uint32_t>* outIndices)
{
	ASSERT(outVertices && outIndices, "Invalid output ptr");

	outVertices->clear(); outIndices->clear();
	const uint32_t N = (uint32_t)points.size();
	ASSERT(N >= 3, "No input points");

	// Use double precision internally for numerical robustness.
	std::vector<DOUBLE3> P;
	P.reserve(N);
	for (auto& p : points)
	{
		P.emplace_back(cvtF3toD3(p));
	}
	ScaleEps eps = makeScaleEps(P);

	// Try to form an initial tetrahedron.
	auto T = buildInitialTetra(P, eps);
	if (T[0] == ~0u)
	{
		// Coplanar (or worse) fallback:
		// Emit a simple triangle fan over all points to avoid failing hard.
		for (auto& p : points) outVertices->emplace_back(p);
		if (points.size() >= 3)
		{
			for (uint32_t k = 1; k + 1 < (uint32_t)points.size(); ++k)
			{
				outIndices->emplace_back(0);
				outIndices->emplace_back(k);
				outIndices->emplace_back(k + 1);
			}
		}
		return;
	}

	// Compute an interior reference point from tetra center for consistent outward normals.
	DOUBLE3 insideRef = tetraCenter(T, P);
	auto faces = makeInitialHull(T, P, insideRef);

	// -------- Main loop --------
	while (true)
	{
		// 1) Global farthest outside point and a face that sees it.
		MaxOutInfo info = findFarthest(faces, P, eps);
		if (info.face < 0) break; // No outside points -> hull is complete.

		uint32_t apex = info.pid;

		// 2) Build adjacency and BFS-expand all faces visible from the apex.
		EdgeAdj adj = buildAdj(faces);
		std::vector<int> visible = collectVisibleFacesBFS(faces, adj, info.face, apex, P, eps);
		if (visible.empty())
		{
			// Numerical corner-case. Graceful exit is acceptable for practical usage.
			break;
		}

		// 3) Build horizon loop (directed boundary edges).
		auto horizon = buildHorizon(faces, adj, visible);

		// 4) Remove all visible faces.
		std::vector<bool> bDead(faces.size(), false);
		for (int fi : visible)
		{
			if (fi >= 0 && fi < (int)faces.size())
			{
				bDead[fi] = true;
			}
		}
		std::vector<Face> kept; kept.reserve(faces.size());
		for (size_t i = 0; i < faces.size(); ++i)
		{
			if (!bDead[i])
			{
				kept.emplace_back(faces[i]);
			}
		}
		faces.swap(kept);

		// 5) Create new faces from horizon edges and the apex.
		//    Use orientation (u,v,apex), and enforce outward by testing insideRef.
		for (auto [u, v] : horizon)
		{
			Face nf = makeFace(u, v, apex, P);
			if (faceDist(nf, insideRef) > 0.0)
			{ // If insideRef appears in front of the face, flip to make it outward.
				std::swap(nf.b, nf.c);
				nf = makeFace(nf.a, nf.b, nf.c, P);
			}
			faces.emplace_back(nf);
		}
	}

	// ------- Emit unique vertices & triangles -------
	// Collect used vertex indices across remaining faces.
	std::unordered_set<uint32_t> used;
	for (const auto& f : faces)
	{
		if (DOUBLE3::Magnitude(f.n) == 0) continue;
		used.insert(f.a);
		used.insert(f.b);
		used.insert(f.c);
	}
	if (used.empty())
	{
		// Degenerate fallback: emit one vertex.
		outVertices->emplace_back(points[0]);
		return;
	}

	// Remap original indices to compact [0..M) range for output vertices.
	std::vector<uint32_t> remap(N, ~0u);
	outVertices->reserve(used.size());
	uint32_t nxt = 0;
	for (uint32_t i = 0; i < N; ++i)
	{
		if (used.count(i))
		{
			remap[i] = nxt++;
			outVertices->emplace_back(cvtD3toF3(P[i]));
		}
	}
	// Emit CCW triangle index list (no duplicate/degenerate triangles).
	outIndices->reserve(faces.size() * 3);
	for (const auto& f : faces)
	{
		if (DOUBLE3::Magnitude(f.n) == 0) continue;
		uint32_t ia = remap[f.a], ib = remap[f.b], ic = remap[f.c];
		if (ia == ~0u || ib == ~0u || ic == ~0u) continue;
		if (ia == ib || ib == ic || ic == ia) continue;
		outIndices->emplace_back(ia);
		outIndices->emplace_back(ib);
		outIndices->emplace_back(ic);
	}
}

// Utility: Save hull (vertices + triangles) in Wavefront OBJ format.
// Indices are written 1-based as per OBJ spec.
bool SaveHullAsObj(
	const std::string& path,
	const std::vector<FLOAT3>& vertices,
	const std::vector<uint32_t>& indices)
{
	FILE* f = nullptr;
#if defined(_MSC_VER)
	fopen_s(&f, path.c_str(), "wb");
#else
	f = std::fopen(path.c_str(), "wb");
#endif
	if (!f) return false;
	std::fprintf(f, "# QuickHull OBJ export\n");
	for (const auto& v : vertices)
	{
		std::fprintf(f, "v %.9f %.9f %.9f\n", v.x, v.y, v.z);
	}
	const size_t T = indices.size() / 3;
	for (size_t t = 0; t < T; ++t)
	{
		int i0 = indices[3 * t + 0] + 1, i1 = indices[3 * t + 1] + 1, i2 = indices[3 * t + 2] + 1;
		std::fprintf(f, "f %d %d %d\n", i0, i1, i2);
	}
	std::fclose(f);
	return true;
}
