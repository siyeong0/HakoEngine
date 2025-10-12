#include "pch.h"
#include "QuickHull.h"
#include "QuickHullBuilder.h"

// ============================================================================
// Internal math utils
// ----------------------------------------------------------------------------
// Minimal 3D vector + basic operations (no dependencies).
// dot/cross/len are inlined helpers used throughout QuickHull.
// ============================================================================
struct QHVec3
{
	double x = 0, y = 0, z = 0;
	QHVec3() = default;
	QHVec3(double X, double Y, double Z) :x(X), y(Y), z(Z) {}
	QHVec3 operator+(const QHVec3& b) const { return { x + b.x,y + b.y,z + b.z }; }
	QHVec3 operator-(const QHVec3& b) const { return { x - b.x,y - b.y,z - b.z }; }
	QHVec3 operator*(double s) const { return { x * s,y * s,z * s }; }
};
inline static double dot(const QHVec3& a, const QHVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline static QHVec3 cross(const QHVec3& a, const QHVec3& b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
inline static double len(const QHVec3& a) { return std::sqrt(dot(a, a)); }

// ============================================================================
// Plane & Tetra utils
// ----------------------------------------------------------------------------
// QHPlane: plane in Hesse-like form with unit normal 'n' and offset 'd' where
// SignedDistance(p) = dot(n, p) + d. Positive means "in front of" the plane.
// makePlane() normalizes the normal to keep distances in world units.
//
// tetVol6(): 6x volume (signed) of tetra (a,b,c,d). Used to test degeneracy
// and orientation without expensive divisions.
// ============================================================================
struct QHPlane
{
	QHVec3 n;
	double d = 0;
	double SignedDistance(const QHVec3& p)const { return dot(n, p) + d; }
};

inline static QHPlane makePlane(const QHVec3& a, const QHVec3& b, const QHVec3& c)
{
	QHVec3 n = cross(b - a, c - a); double L = len(n); if (L > 0) n = n * (1.0 / L);
	QHPlane P; P.n = n; P.d = -dot(n, a); return P;
}

inline static double tetVol6(const QHVec3& a, const QHVec3& b, const QHVec3& c, const QHVec3& d)
{
	return dot(cross(b - a, c - a), d - a);
}

// ============================================================================
// QHFace
// ----------------------------------------------------------------------------
// One oriented triangular face of the current hull:
//  - (a,b,c): CCW order when viewed from outside the hull.
//  - Adj[3] : adjacency per edge: 
//      * edge 0 = (a,b), edge 1 = (b,c), edge 2 = (c,a)
//  - Plane  : outward plane; SignedDistance(apex) > 0 => apex sees this face.
//  - bAlive : false after face is removed (consumed) by expansion.
//  - Outside: indices of candidate points assigned outside this face.
// ============================================================================

struct QHFace
{
	int a = -1;
	int b = -1;
	int c = -1;	// CCW when viewed from outside
	int Adj[3] = { -1,-1,-1 };	// adjacency for edges: (a,b),(b,c),(c,a)

	QHPlane Plane;
	bool bAlive = true;
	std::vector<int> Outside;
};

// Return the oriented edge (u,v) of face f for local edge index e ∈ {0,1,2}.
static inline std::pair<int, int> edgeOf(const QHFace& f, int e)
{
	switch (e)
	{
	case 0:
		return { f.a,f.b };
	case 1:
		return { f.b,f.c };
	default:
		return { f.c,f.a };
	}
}

// ============================================================================
// QuickHullBuilder implementation
// ============================================================================

QuickHullBuilder::QuickHullBuilder()
	: m_pVertices(new std::vector<QHVec3>)
	, m_pFaces(new std::vector<QHFace>)
{

}

QuickHullBuilder::~QuickHullBuilder()
{
	SAFE_DELETE(m_pVertices);
	SAFE_DELETE(m_pFaces);
}

QuickHull QuickHullBuilder::Run(const std::vector<FLOAT3>& points)
{
	// Convert external FLOAT3s to internal QHVec3s.
	std::vector<QHVec3> qhPoints(points.size());
	for (size_t i = 0; i < points.size(); ++i)
	{
		qhPoints[i] = QHVec3(points[i].x, points[i].y, points[i].z);
	}

	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	m_Vertices = qhPoints;
	initEpsilon();
	if (m_Vertices.size() < 4)
	{
		// Fewer than 4 points cannot form a 3D hull.
		return {};
	}

	// ------------------------------------------------------------------------
	// 1) Build initial tetrahedron (seed hull)
	// ------------------------------------------------------------------------
	std::vector<int> all(m_Vertices.size()); std::iota(all.begin(), all.end(), 0);
	int i0, i1, i2, i3;
	if (!buildInitialTetra(all, i0, i1, i2, i3))
	{
		// Degenerate input (collinear/coplanar) beyond epsilon tolerance.
		return {};
	}

	// Interior reference point: centroid of the seed tetra.
	// Used to orient newly created faces "outward" later on.
	QHVec3 interior = (m_Vertices[i0] + m_Vertices[i1] + m_Vertices[i2] + m_Vertices[i3]) * 0.25;

	// ------------------------------------------------------------------------
	// 2) Distribute points: assign each non-hull vertex to one face's Outside
	//    set where it lies the farthest in front of that face.
	// ------------------------------------------------------------------------
	distributePoints(all);

	// ------------------------------------------------------------------------
	// 3) Main growth loop:
	//    - pick face with globally farthest outside point (apex)
	//    - collect all faces visible from apex (visible region)
	//    - extract the horizon (face/edge boundary between visible and non-visible)
	//    - stitch a cone of new faces from apex to the horizon ring
	//    - reassign orphaned outside points to the new faces
	// ------------------------------------------------------------------------
	while (true)
	{
		int f = pickFaceWithFarthestPoint(); if (f < 0) break;  // no more outside points
		int p = popFarthestPoint(f); if (p < 0) break;

		std::vector<int> visible;                                 // faces visible from apex
		std::vector<std::pair<int, int>> horizonFE;               // (faceIndex, edgeIndex) on the horizon
		findVisibleRegion(f, p, visible, horizonFE);

		// Convert (face,edge) pairs to oriented vertex-edge pairs (a,b)
		// using each visible face's CCW orientation. This yields a CCW ring
		// around the hole after removing 'visible' faces.
		std::vector<std::pair<int, int>> horizonAB; horizonAB.reserve(horizonFE.size());
		for (auto [fi, e] : horizonFE)
		{
			auto ab = edgeOf(m_Faces[fi], e);
			horizonAB.push_back(ab); // already in the visible face's CCW order
		}

		// Order edges into a single cyclic ring (a0,b0) -> (a1,b1) -> ...
		std::vector<std::pair<int, int>> ring = orderHorizonRing(horizonAB);
		if (ring.empty()) continue; // Safety: degenerate horizon (rare)

		// Remove visible faces (they will be replaced by the cone).
		for (int vf : visible) m_Faces[vf].bAlive = false;

		// Create new faces: (a,b,apex) along the ring, oriented outward.
		std::vector<int> newFaces;
		createConeOrdered(p, horizonFE, ring, newFaces, interior);

		// Gather all points belonging to removed faces (visible region),
		// excluding the apex itself, then reassign them to new faces.
		std::vector<int> orphan;
		for (int vf : visible)
		{
			auto& out = m_Faces[vf].Outside;
			orphan.insert(orphan.end(), out.begin(), out.end()); out.clear();
		}
		std::sort(orphan.begin(), orphan.end());
		orphan.erase(std::unique(orphan.begin(), orphan.end()), orphan.end());
		orphan.erase(std::remove(orphan.begin(), orphan.end(), p), orphan.end());
		reassignOutside(orphan, newFaces);
	}

	// ------------------------------------------------------------------------
	// 4) Export: gather surviving faces and their vertices into QuickHull.
	// ------------------------------------------------------------------------
	QuickHull out;
	out.Vertices.reserve(m_Vertices.size());
	for (auto& v : m_Vertices) out.Vertices.emplace_back((FLOAT)v.x, (FLOAT)v.y, (FLOAT)v.z);
	out.Indices.reserve(m_Faces.size() * 3);
	for (auto& face : m_Faces)
	{
		if (!face.bAlive) continue;
		out.Indices.emplace_back(face.a);
		out.Indices.emplace_back(face.b);
		out.Indices.emplace_back(face.c);
	}
	return out;
}

// ============================================================================
// initEpsilon
// ----------------------------------------------------------------------------
// Initialize geometric tolerance from input bounding-box diagonal:
//   epsilon = max(1e-12, 1e-9 * diag)
// This scales with scene size and protects orientation/sidedness checks.
// ============================================================================
void QuickHullBuilder::initEpsilon()
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	if (m_Vertices.empty())
	{
		m_HullEpsilon = 1e-12; return;
	}
	QHVec3 mn = m_Vertices[0], mx = m_Vertices[0];
	for (auto& p : m_Vertices)
	{
		mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
		mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
	}
	double diag = len(mx - mn);
	m_HullEpsilon = std::max(1e-12, 1e-9 * diag);
}

// ============================================================================
// buildInitialTetra
// ----------------------------------------------------------------------------
// Construct a robust initial tetrahedron:
//  - i0,i1: extremal by x (approx. diameter seed)
//  - i2   : farthest from line (i0,i1)
//  - i3   : maximizes |tetVol6(i0,i1,i2,i3)| (farthest from plane)
// Faces are oriented outward w.r.t. the opposite vertex.
// Returns false if degeneracy exceeds epsilon (line/plane cases).
// ============================================================================
bool QuickHullBuilder::buildInitialTetra(const std::vector<int>& idx, int& o0, int& o1, int& o2, int& o3)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	int i0 = -1, i1 = -1;
	{
		// Choose two extremal points along x as a cheap diameter proxy.
		double mn = +std::numeric_limits<double>::infinity(), mx = -mn;
		for (int i : idx)
		{
			double x = m_Vertices[i].x;
			if (x < mn)
			{
				mn = x;
				i0 = i;
			}
			if (x > mx)
			{
				mx = x;
				i1 = i;
			}
		}
		if (i0 == i1)
		{
			return false;
		}
	}
	int i2 = -1;
	{
		// Farthest from line (i0,i1) by area of triangle (i0,i1,i)
		QHVec3 a = m_Vertices[i0];
		QHVec3 b = m_Vertices[i1];
		QHVec3 ab = b - a;
		double best = 0;
		for (int i : idx)
		{
			if (i == i0 || i == i1) continue;
			double d = len(cross(m_Vertices[i] - a, ab));
			if (d > best)
			{
				best = d;
				i2 = i;
			}
		}
		if (i2 < 0 || best < m_HullEpsilon)
		{
			// Collinear beyond epsilon.
			return false;
		}
	}
	int i3 = -1;
	{
		// Farthest from plane (i0,i1,i2) by |6*volume|
		QHVec3 a = m_Vertices[i0];
		QHVec3 b = m_Vertices[i1];
		QHVec3 c = m_Vertices[i2];
		double best = 0;
		for (int i : idx)
		{
			if (i == i0 || i == i1 || i == i2) continue;
			double v6 = std::abs(tetVol6(a, b, c, m_Vertices[i]));
			if (v6 > best)
			{
				best = v6;
				i3 = i;
			}
		}
		if (i3 < 0 || best < m_HullEpsilon)
		{
			// Coplanar beyond epsilon.
			return false;
		}
	}
	// Create 4 faces; orient outward by checking the opposite vertex.
	m_Faces.clear(); m_Faces.reserve(64);
	auto addFace = [&](int a, int b, int c)->int
		{
			QHFace f;
			f.a = a;
			f.b = b;
			f.c = c;
			f.Plane = makePlane(m_Vertices[a], m_Vertices[b], m_Vertices[c]);
			m_Faces.emplace_back(std::move(f));
			return (int)m_Faces.size() - 1;
		};
	auto oriented = [&](int a, int b, int c, int opp)
		{
			QHPlane P = makePlane(m_Vertices[a], m_Vertices[b], m_Vertices[c]);
			if (P.SignedDistance(m_Vertices[opp]) > 0) std::swap(b, c); // ensure outward
			return addFace(a, b, c);
		};
	int f0 = oriented(i0, i1, i2, i3);
	int f1 = oriented(i0, i3, i1, i2);
	int f2 = oriented(i1, i3, i2, i0);
	int f3 = oriented(i2, i3, i0, i1);

	// Wire up initial adjacencies (by matching opposite edges).
	setAdj(f0, 0, f1, 2);
	setAdj(f0, 1, f2, 2);
	setAdj(f0, 2, f3, 2);
	setAdj(f1, 0, f3, 0);
	setAdj(f1, 1, f2, 0);
	setAdj(f2, 1, f3, 1);

	o0 = i0; o1 = i1; o2 = i2; o3 = i3;
	return true;
}

// Set face-to-face adjacency for specific local edges ea/eb.
void QuickHullBuilder::setAdj(int fa, int ea, int fb, int eb)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	m_Faces[fa].Adj[ea] = fb; 
	m_Faces[fb].Adj[eb] = fa;
}

// ============================================================================
// distributePoints
// ----------------------------------------------------------------------------
// Assign each non-seed vertex to one face's Outside set for which the point
// lies in front of the plane by more than epsilon and has the greatest
// signed distance among candidate faces. Faces with no outside points
// are "terminal" unless they become part of the visible region later.
// ============================================================================
void QuickHullBuilder::distributePoints(const std::vector<int>& idx)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	std::vector<char> used(m_Vertices.size(), 0);
	for (auto& f : m_Faces)
	{
		used[f.a] = used[f.b] = used[f.c] = 1;
	}
	for (int i = 0; i < (int)m_Vertices.size(); ++i)
	{
		if (used[i]) continue;
		int best = -1; double bd = 0;
		for (int fi = 0; fi < (int)m_Faces.size(); ++fi)
		{
			auto& f = m_Faces[fi]; if (!f.bAlive) continue;
			double d = f.Plane.SignedDistance(m_Vertices[i]);
			if (d > m_HullEpsilon && d > bd)
			{
				bd = d;
				best = fi;
			}
		}
		if (best >= 0)
		{
			m_Faces[best].Outside.emplace_back(i);
		}
	}
}

// ============================================================================
// pickFaceWithFarthestPoint
// ----------------------------------------------------------------------------
// Select the face owning the globally farthest outside point (by plane distance).
// Returns -1 if no face has outside points.
// ============================================================================
int QuickHullBuilder::pickFaceWithFarthestPoint()
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	int best = -1;
	double bd = 0;
	for (int fi = 0; fi < (int)m_Faces.size(); ++fi)
	{
		auto& f = m_Faces[fi];
		if (!f.bAlive || f.Outside.empty()) continue;
		for (int pi : f.Outside)
		{
			double d = f.Plane.SignedDistance(m_Vertices[pi]);
			if (d > bd)
			{
				bd = d;
				best = fi;
			}
		}
	}
	return best;
}

// ============================================================================
// popFarthestPoint
// ----------------------------------------------------------------------------
// For a given face, remove and return the farthest point in its Outside set.
// Returns -1 if the face has no outside points.
// ============================================================================
int QuickHullBuilder::popFarthestPoint(int faceIndex)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	auto& f = m_Faces[faceIndex];
	if (f.Outside.empty())
	{
		return -1;
	}
	int bi = -1, bp = -1; double bd = 0;
	for (int k = 0; k < (int)f.Outside.size(); ++k)
	{
		int pi = f.Outside[k];
		double d = f.Plane.SignedDistance(m_Vertices[pi]);
		if (d > bd)
		{
			bd = d;
			bi = pi;
			bp = k;
		}
	}
	if (bp >= 0)
	{
		// Remove chosen point by swapping with back.
		f.Outside[bp] = f.Outside.back();
		f.Outside.pop_back();
	}
	return bi;
}

// ============================================================================
// findVisibleRegion
// ----------------------------------------------------------------------------
// DFS from 'startFace' over bAlive faces that are visible from 'apex':
//  - A face is visible if plane.SignedDistance(apex) > epsilon.
// Outputs:
//  - visible  : list of visible faces to be removed.
//  - horizonFE: boundary edges (faceIndex, edgeIndex) where the neighbor
//               is either absent or non-visible; these edges form the horizon.
// ============================================================================
void QuickHullBuilder::findVisibleRegion(
	int startFace,
	int apex,
	std::vector<int>& visible,
	std::vector<std::pair<int, int>>& horizonFE)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	std::vector<char> mark(m_Faces.size(), 0);
	std::vector<int> st; st.emplace_back(startFace);

	auto isVisible = [&](int fi)->bool { return m_Faces[fi].bAlive && (m_Faces[fi].Plane.SignedDistance(m_Vertices[apex]) > m_HullEpsilon); };

	while (!st.empty())
	{
		int fi = st.back(); st.pop_back();
		if (mark[fi]) continue;
		mark[fi] = 1;
		if (!isVisible(fi)) continue;
		visible.emplace_back(fi);

		for (int e = 0; e < 3; ++e)
		{
			int nb = m_Faces[fi].Adj[e];
			if (nb < 0 || !m_Faces[nb].bAlive)
			{
				// Open boundary: this edge belongs to the horizon.
				horizonFE.emplace_back(fi, e);
				continue;
			}
			if (!mark[nb])
			{
				if (isVisible(nb))
				{
					st.emplace_back(nb);
				}
				else
				{
					// Transition from visible to non-visible => horizon edge.
					horizonFE.emplace_back(fi, e);
				}
			}
		}
	}
}

// ============================================================================
// orderHorizonRing
// ----------------------------------------------------------------------------
// Given a set of oriented edges (a,b) that lie on the horizon (each coming
// from a visible face in CCW orientation), order them into a single cyclic
// ring: (a0,b0)->(a1,b1)->... such that b_i == a_{i+1}.
// Note: Uses a simple multimap walk; assumes a single loop horizon.
// ============================================================================
std::vector<std::pair<int, int>> QuickHullBuilder::orderHorizonRing(const std::vector<std::pair<int, int>>& edges)
{
	if (edges.empty())
	{
		return {};
	}

	std::unordered_multimap<int, int> nextMap; nextMap.reserve(edges.size() * 2);
	std::unordered_multimap<int, int> prevMap;
	for (auto& e : edges)
	{
		nextMap.emplace(e.first, e.second);
		prevMap.emplace(e.second, e.first);
	}

	std::vector<std::pair<int, int>> ring;
	ring.reserve(edges.size());
	int a = edges[0].first;
	int b = edges[0].second;
	ring.emplace_back(a, b);

	while ((int)ring.size() < (int)edges.size())
	{
		auto range = nextMap.equal_range(b);
		bool found = false;
		for (auto it = range.first; it != range.second; ++it)
		{
			int nb = it->second;
			bool used = false;
			for (auto& r : ring)
			{
				if (r.first == b && r.second == nb)
				{
					used = true;
					break;
				}
			}
			if (!used)
			{
				ring.emplace_back(b, nb);
				b = nb;
				found = true;
				break;
			}
		}
		if (!found) break;
	}
	return ring;
}

// ============================================================================
// addFace / addFaceOutward
// ----------------------------------------------------------------------------
// addFace: create a face with (a,b,c) and compute its plane.
// addFaceOutward: ensure the new face is oriented outward w.r.t. 'interior'.
// ============================================================================
int QuickHullBuilder::addFace(int a, int b, int c)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	QHFace f;
	f.a = a;
	f.b = b;
	f.c = c;
	f.Plane = makePlane(m_Vertices[a], m_Vertices[b], m_Vertices[c]);
	m_Faces.push_back(std::move(f));
	return (int)m_Faces.size() - 1;
}

int QuickHullBuilder::addFaceOutward(int a, int b, int c, const QHVec3& interior)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	int id = addFace(a, b, c);
	if (m_Faces[id].Plane.SignedDistance(interior) > 0)
	{
		// If interior is in front, the normal points inward; flip (b,c).
		std::swap(m_Faces[id].b, m_Faces[id].c);
		m_Faces[id].Plane = makePlane(m_Vertices[m_Faces[id].a], m_Vertices[m_Faces[id].b], m_Vertices[m_Faces[id].c]);
	}
	return id;
}

// ============================================================================
// findMatchingEdge
// ----------------------------------------------------------------------------
// On neighbor 'oldNbr', find the local edge index whose oriented edge equals
// (b,a). The horizon stores edges as (a,b) from the visible side; the neighbor
// must match the reverse orientation.
// Returns {0,1,2} or -1 if not found (defensive).
// ============================================================================
int QuickHullBuilder::findMatchingEdge(int oldNbr, int a, int b)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;

	const QHFace& f = m_Faces[oldNbr];
	std::pair<int, int> e0 = edgeOf(f, 0);
	std::pair<int, int> e1 = edgeOf(f, 1);
	std::pair<int, int> e2 = edgeOf(f, 2);
	if (e0.first == b && e0.second == a) return 0;
	if (e1.first == b && e1.second == a) return 1;
	if (e2.first == b && e2.second == a) return 2;
	return -1;
}

// ============================================================================
// createConeOrdered
// ----------------------------------------------------------------------------
// Given the horizon (as both (face,edge) and ordered vertex pairs (a,b)),
// create the "cone" of new faces (a,b,apex) around the ring, then wire up:
//  - edge0=(a,b) of each new face to the old non-visible neighbor across
//    the horizon edge,
//  - edge1=(b,apex) and edge2=(apex,a) to the next/prev new faces.
// 'interior' helps enforce outward orientation of the new faces.
// ============================================================================
void QuickHullBuilder::createConeOrdered(
	int apex,
	const std::vector<std::pair<int, int>>& horizonFE,
	const std::vector<std::pair<int, int>>& ringAB,
	std::vector<int>& newFaces,
	const QHVec3& interior)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;

	std::unordered_map<long long, int> newByEdge;
	newByEdge.reserve(ringAB.size() * 2);
	auto keyAB = [](int a, int b)->long long
		{
			return ((long long)a << 32) ^ (long long)b;
		};

	newFaces.clear();
	newFaces.reserve(ringAB.size());

	// 1) Create new faces along the ring: (a,b,apex).
	//    addFaceOutward() flips if needed so normals point outward.
	for (auto& ab : ringAB)
	{
		int a = ab.first, b = ab.second;
		int nf = addFaceOutward(a, b, apex, interior);
		newFaces.push_back(nf);
		newByEdge.emplace(keyAB(a, b), nf);
	}

	// 2) Connect each new face's edge0=(a,b) to the old non-visible neighbor.
	// horizonFE stores (visibleFace, edge) where the neighbor across that edge
	// is non-visible (or absent). We map (a,b) to the newly created face and
	// link adjacency with the old neighbor.
	for (auto [fi, e] : horizonFE)
	{
		auto ab = edgeOf(m_Faces[fi], e); // oriented as in visible face (CCW)
		int oldNbr = m_Faces[fi].Adj[e];  // the non-visible neighbor
		auto it = newByEdge.find(keyAB(ab.first, ab.second));
		if (it == newByEdge.end()) continue; // Horizon edge not in ordered ring (rare)
		int nf = it->second;

		if (oldNbr >= 0)
		{
			m_Faces[nf].Adj[0] = oldNbr; // (a,b) boundary becomes edge0 of new face
			int mate = findMatchingEdge(oldNbr, ab.first, ab.second);
			if (mate >= 0)
			{
				m_Faces[oldNbr].Adj[mate] = nf;
			}
		}
	}

	// 3) Wire internal adjacencies among new faces around the apex.
	// For ring (a_i,b_i) with b_i == a_{i+1}:
	//   face i:   v = (a_i,b_i,apex)
	//             edge1=(b_i,apex)  -> connect to edge2=(apex,a_{i+1}) of face j
	const int M = (int)ringAB.size();
	for (int i = 0; i < M; ++i)
	{
		int j = (i + 1) % M;
		int fi = newByEdge[keyAB(ringAB[i].first, ringAB[i].second)];
		int fj = newByEdge[keyAB(ringAB[j].first, ringAB[j].second)];
		// (b_i,apex) of fi  <->  (apex,a_{i+1}) of fj
		setAdj(fi, 1, fj, 2);
	}
}

// ============================================================================
// reassignOutside
// ----------------------------------------------------------------------------
// After removing the visible region, reassign its pooled outside points to the
// most suitable new face (closest by positive signed distance > epsilon).
// Points with non-positive distance (<= epsilon) are discarded here.
// ============================================================================
void QuickHullBuilder::reassignOutside(const std::vector<int>& pts, const std::vector<int>& newFaces)
{
	std::vector<QHFace>& m_Faces = *m_pFaces;
	std::vector<QHVec3>& m_Vertices = *m_pVertices;

	for (int pi : pts)
	{
		int best = -1; double bd = 0;
		for (int nf : newFaces)
		{
			double d = m_Faces[nf].Plane.SignedDistance(m_Vertices[pi]);
			if (d > m_HullEpsilon && d > bd)
			{
				bd = d;
				best = nf;
			}
		}
		if (best >= 0)
		{
			m_Faces[best].Outside.push_back(pi);
		}
	}
}
