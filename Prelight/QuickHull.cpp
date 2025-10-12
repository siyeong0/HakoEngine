#include "pch.h"
#include "QuickHull.h"
#include "SparseBinaryGrid.h"

// ===== Internal math utils =====
struct QHVec3
{
	double x = 0, y = 0, z = 0; QHVec3() = default; QHVec3(double X, double Y, double Z) :x(X), y(Y), z(Z) {}
	QHVec3 operator+(const QHVec3& b) const { return { x + b.x,y + b.y,z + b.z }; }
	QHVec3 operator-(const QHVec3& b) const { return { x - b.x,y - b.y,z - b.z }; }
	QHVec3 operator*(double s) const { return { x * s,y * s,z * s }; }
};
inline static double dot(const QHVec3& a, const QHVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline static QHVec3 cross(const QHVec3& a, const QHVec3& b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
inline static double len(const QHVec3& a) { return std::sqrt(dot(a, a)); }

// ===== Plane & Tetra utils =====
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

struct QHFace
{
	int a = -1;
	int b = -1;
	int c = -1;	// CCW (바깥에서 볼 때)
	int Adj[3] = { -1,-1,-1 };	// (a,b),(b,c),(c,a)

	QHPlane Plane;
	bool bAlive = true;
	std::vector<int> Outside;
};

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

static inline int nextEdge(int e) { return (e + 1) % 3; }
static inline int prevEdge(int e) { return (e + 2) % 3; }

class QuickHullBuilder
{
public:
	double m_HullEpsilon = -1.0;

	QuickHull Run(const std::vector<FLOAT3>& points)
	{
		std::vector<QHVec3> qhPoints(points.size());
		for (size_t i = 0; i < points.size(); ++i) qhPoints[i] = QHVec3(points[i].x, points[i].y, points[i].z);
		return Run(qhPoints);
	}

	QuickHull Run(const std::vector<QHVec3>& points)
	{
		m_Vertices = points;
		initEpsilon();
		if (m_Vertices.size() < 4)
		{
			return {};
		}

		// Initial Tetra
		std::vector<int> all(m_Vertices.size()); std::iota(all.begin(), all.end(), 0);
		int i0, i1, i2, i3;
		if (!buildInitialTetra(all, i0, i1, i2, i3))
		{
			return {};
		}

		// Interior point. Centroid of initial tetra
		m_Interior = (m_Vertices[i0] + m_Vertices[i1] + m_Vertices[i2] + m_Vertices[i3]) * 0.25;

		distributePoints(all);

		// Main loop
		while (true)
		{
			int f = pickFaceWithFarthestPoint(); if (f < 0) break;
			int p = popFarthestPoint(f); if (p < 0) break;

			std::vector<int> visible;
			std::vector<std::pair<int, int>> horizonFE; // (face, edge)
			findVisibleRegion(f, p, visible, horizonFE);

			std::vector<std::pair<int, int>> horizonAB; horizonAB.reserve(horizonFE.size());
			for (auto [fi, e] : horizonFE)
			{
				auto ab = edgeOf(m_Faces[fi], e);
				horizonAB.push_back(ab); // 이미 보이는 페이스의 CCW 순서
			}

			std::vector<std::pair<int, int>> ring = orderHorizonRing(horizonAB);
			if (ring.empty()) continue; // 안전

			for (int vf : visible) m_Faces[vf].bAlive = false;

			std::vector<int> newFaces;
			createConeOrdered(p, horizonFE, ring, newFaces);

			std::vector<int> orphan;
			for (int vf : visible)
			{
				auto& out = m_Faces[vf].Outside;
				orphan.insert(orphan.end(), out.begin(), out.end()); out.clear();
			}
			std::sort(orphan.begin(), orphan.end()); orphan.erase(std::unique(orphan.begin(), orphan.end()), orphan.end());
			orphan.erase(std::remove(orphan.begin(), orphan.end(), p), orphan.end());
			reassignOutside(orphan, newFaces);
		}

		// Export
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

private:
	std::vector<QHVec3> m_Vertices;
	std::vector<QHFace> m_Faces;
	QHVec3 m_Interior{ 0,0,0 };

	// Initi epsilon based on bounding box diagonal
	void initEpsilon()
	{
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

	bool buildInitialTetra(const std::vector<int>& idx, int& o0, int& o1, int& o2, int& o3)
	{
		int i0 = -1, i1 = -1;
		{
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
				return false;
			}
		}
		int i3 = -1;
		{
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
				return false;
			}
		}
		m_Faces.clear(); m_Faces.reserve(64);
		auto addFace = [&](int a, int b, int c)->int {
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
				if (P.SignedDistance(m_Vertices[opp]) > 0) std::swap(b, c);
				return addFace(a, b, c);
			};
		int f0 = oriented(i0, i1, i2, i3);
		int f1 = oriented(i0, i3, i1, i2);
		int f2 = oriented(i1, i3, i2, i0);
		int f3 = oriented(i2, i3, i0, i1);

		setAdj(f0, 0, f1, 2);
		setAdj(f0, 1, f2, 2);
		setAdj(f0, 2, f3, 2);
		setAdj(f1, 0, f3, 0);
		setAdj(f1, 1, f2, 0);
		setAdj(f2, 1, f3, 1);

		o0 = i0; o1 = i1; o2 = i2; o3 = i3;
		return true;
	}

	void setAdj(int fa, int ea, int fb, int eb) { m_Faces[fa].Adj[ea] = fb; m_Faces[fb].Adj[eb] = fa; }

	void distributePoints(const std::vector<int>& idx)
	{
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

	int pickFaceWithFarthestPoint()
	{
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
	int popFarthestPoint(int fi)
	{
		auto& f = m_Faces[fi];
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
			f.Outside[bp] = f.Outside.back();
			f.Outside.pop_back();
		}
		return bi;
	}

	void findVisibleRegion(
		int startFace,
		int apex,
		std::vector<int>& visible,
		std::vector<std::pair<int, int>>& horizonFE)
	{
		std::vector<char> mark(m_Faces.size(), 0);
		std::vector<int> st; st.emplace_back(startFace);

		auto isVis = [&](int fi)->bool { return m_Faces[fi].bAlive && (m_Faces[fi].Plane.SignedDistance(m_Vertices[apex]) > m_HullEpsilon); };

		while (!st.empty())
		{
			int fi = st.back(); st.pop_back();
			if (mark[fi]) continue;
			mark[fi] = 1;
			if (!isVis(fi)) continue;
			visible.emplace_back(fi);

			for (int e = 0; e < 3; ++e)
			{
				int nb = m_Faces[fi].Adj[e];
				if (nb < 0 || !m_Faces[nb].bAlive)
				{
					horizonFE.emplace_back(fi, e);
					continue;
				}
				if (!mark[nb])
				{
					if (isVis(nb))
					{
						st.emplace_back(nb);
					}
					else
					{
						horizonFE.emplace_back(fi, e);
					}
				}
			}
		}
	}

	std::vector<std::pair<int, int>> orderHorizonRing(const std::vector<std::pair<int, int>>& edges)
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

	int addFace(int a, int b, int c)
	{
		QHFace f;
		f.a = a;
		f.b = b;
		f.c = c;
		f.Plane = makePlane(m_Vertices[a], m_Vertices[b], m_Vertices[c]);
		m_Faces.push_back(std::move(f));
		return (int)m_Faces.size() - 1;
	}
	int addFaceOutward(int a, int b, int c)
	{
		int id = addFace(a, b, c);
		if (m_Faces[id].Plane.SignedDistance(m_Interior) > 0)
		{
			std::swap(m_Faces[id].b, m_Faces[id].c);
			m_Faces[id].Plane = makePlane(m_Vertices[m_Faces[id].a], m_Vertices[m_Faces[id].b], m_Vertices[m_Faces[id].c]);
		}
		return id;
	}

	int findMatchingEdge(int oldNbr, int a, int b)
	{
		const QHFace& f = m_Faces[oldNbr];
		std::pair<int, int> e0 = edgeOf(f, 0);
		std::pair<int, int> e1 = edgeOf(f, 1);
		std::pair<int, int> e2 = edgeOf(f, 2);
		if (e0.first == b && e0.second == a) return 0;
		if (e1.first == b && e1.second == a) return 1;
		if (e2.first == b && e2.second == a) return 2;
		return -1;
	}

	void createConeOrdered(
		int apex,
		const std::vector<std::pair<int, int>>& horizonFE,
		const std::vector<std::pair<int, int>>& ringAB,
		std::vector<int>& newFaces)
	{
		std::unordered_map<long long, int> newByEdge;
		newByEdge.reserve(ringAB.size() * 2);
		auto keyAB = [](int a, int b)->long long
			{
				return ((long long)a << 32) ^ (long long)b;
			};

		newFaces.clear();
		newFaces.reserve(ringAB.size());

		// 1) 링을 따라 새 면 생성: (a,b,apex) (방향은 addFaceOutward에서 보정)
		for (auto& ab : ringAB)
		{
			int a = ab.first, b = ab.second;
			int nf = addFaceOutward(a, b, apex);
			newFaces.push_back(nf);
			newByEdge.emplace(keyAB(a, b), nf);
		}

		// 2) 각 새 면 edge0=(a,b) 를 과거 “비가시” 이웃(face)와 연결
		// horizonFE에는 (보이는face, edge) 저장. 그 edge의 이웃이 비가시 페이스.
		for (auto [fi, e] : horizonFE)
		{
			auto ab = edgeOf(m_Faces[fi], e); // 보이는 face의 CCW 에지방향
			int oldNbr = m_Faces[fi].Adj[e];  // 비가시 이웃
			auto it = newByEdge.find(keyAB(ab.first, ab.second));
			if (it == newByEdge.end()) continue; // 정렬 링에 없으면 skip (이상 케이스)
			int nf = it->second;

			if (oldNbr >= 0)
			{
				m_Faces[nf].Adj[0] = oldNbr; // (a,b) 경계
				int mate = findMatchingEdge(oldNbr, ab.first, ab.second);
				if (mate >= 0)
				{
					m_Faces[oldNbr].Adj[mate] = nf;
				}
			}
		}

		// 3) 새 면들끼리 apex를 공유하는 옆변들 연결
		// 링이 (a0,b0),(a1,b1),... 이고 b_i == a_{i+1}.  
		// 새면 i: (a_i,b_i,apex) → edge1=(b_i,apex), edge2=(apex,a_i)
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

	void reassignOutside(const std::vector<int>& pts, const std::vector<int>& newFaces)
	{
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
};

// ===== Public API =====
QuickHull QuickHull::Create(const std::vector<FLOAT3>& points)
{
	QuickHullBuilder qh;
	return qh.Run(points);
}

struct CornerKey 
{
	int kx, ky, kz; // 격자 코너 정수좌표 (origin 기준, 칸 크기 = Cell)
	bool operator==(const CornerKey& o) const noexcept {
		return kx == o.kx && ky == o.ky && kz == o.kz;
	}
};
struct CornerKeyHash 
{
	size_t operator()(const CornerKey& k) const noexcept 
	{
		uint64_t h = (uint64_t)(k.kx * 73856093) ^ (uint64_t)(k.ky * 19349663) ^ (uint64_t)(k.kz * 83492791);
		return (size_t)h;
	}
};

// ---- 경계 판정: 채워져 있고, 6-이웃 중 하나라도 비어 있으면 boundary ----
static inline bool isBoundaryVoxel(const SparseBinaryGrid& g, int x, int y, int z)
{
	ASSERT(x >= 0 && y >= 0 && z >= 0);
	if (!g.GetVoxel(x, y, z)) return false;
	static const int d6[6][3] = { {+1,0,0},{-1,0,0},{0,+1,0},{0,-1,0},{0,0,+1},{0,0,-1} };
	for (auto& d : d6) {
		if (!g.GetVoxel(x + d[0], y + d[1], z + d[2])) return true;
	}
	return false;
}

// ---- 메인: 경계 복셀들의 8 코너를 수집해 월드 좌표 반환 ----
std::vector<FLOAT3> collectHullCornersFromSurface(const SparseBinaryGrid& grid)
{
	const int T = Brick::NUM_VOXELS_EDGE; // 타일 한 변(예: 32)
	const float h = grid.GetCellSize();          // 복셀 크기
	const FLOAT3 O = grid.GetOrigin();       // 그리드 원점

	std::unordered_set<CornerKey, CornerKeyHash> cornerSet;
	cornerSet.reserve((size_t)grid.Size() * 64); // 대충 여유 잡기

	// 타일 단위 순회
	grid.ForEachTile([&](uint64_t /*key*/, int tileIdx, int tx, int ty, int tz)
		{
			const Brick& tile = grid.GetBrick(tileIdx);
			const int baseX = tx * T;
			const int baseY = ty * T;
			const int baseZ = tz * T;

			// 간단/안전: 타일 내부 전수 검사 (T^3 = 32768) + tile.Get(li)
			// (TileCPU::FULL일 땐 모두 true)
			for (int lz = 0; lz < T; ++lz)
			{
				for (int ly = 0; ly < T; ++ly)
				{
					for (int lx = 0; lx < T; ++lx)
					{
						const int li = SparseBinaryGrid::GetLocalIndex(lx, ly, lz);
						bool filled = (tile.Mode == Brick::FULL) ? true : tile.GetBit((uint16_t)li);
						if (!filled) continue;

						const int gx = baseX + lx;
						const int gy = baseY + ly;
						const int gz = baseZ + lz;

						if (!isBoundaryVoxel(grid, gx, gy, gz)) continue;

						// 경계 복셀의 8 코너 정수 좌표 (격자 코너 키: 복셀 [ix,ix+1] × [iy,iy+1] × [iz,iz+1])
						// sx,sy,sz = 0 or 1
						for (int sx = 0; sx <= 1; ++sx)
						{
							for (int sy = 0; sy <= 1; ++sy)
							{
								for (int sz = 0; sz <= 1; ++sz)
								{
									CornerKey ck{ gx + sx, gy + sy, gz + sz };
									cornerSet.insert(ck);
								}
							}
						}
					}
				}
			}
		});

	// 키 → 월드 좌표로 변환
	std::vector<FLOAT3> pts;
	pts.reserve(cornerSet.size());
	for (const CornerKey& k : cornerSet)
	{
		// world = Origin + Cell * (k)
		const float wx = O.x + h * (float)k.kx;
		const float wy = O.y + h * (float)k.ky;
		const float wz = O.z + h * (float)k.kz;
		pts.push_back({ wx, wy, wz });
	}
	return pts;
}

QuickHull QuickHull::Create(const SparseBinaryGrid& grid)
{
	const std::vector<FLOAT3> conrerPoints = collectHullCornersFromSurface(grid);
	return Create(conrerPoints);
}

bool QuickHull::SaveAsOBJ(const std::string& path) const
{
	FILE* f = nullptr;
#if defined(_MSC_VER)
	fopen_s(&f, path.c_str(), "wb");
#else
	f = std::fopen(path.c_str(), "wb");
#endif
	if (!f) return false;
	std::fprintf(f, "# QuickHull OBJ export\n");
	for (const auto& v : Vertices)
	{
		std::fprintf(f, "v %.9f %.9f %.9f\n", v.x, v.y, v.z);
	}
	const size_t T = Indices.size() / 3;
	for (size_t t = 0; t < T; ++t)
	{
		int i0 = Indices[3 * t + 0] + 1, i1 = Indices[3 * t + 1] + 1, i2 = Indices[3 * t + 2] + 1;
		std::fprintf(f, "f %d %d %d\n", i0, i1, i2);
	}
	std::fclose(f);
	return true;
}
