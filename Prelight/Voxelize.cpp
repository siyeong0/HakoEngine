// ============================================================================
// Sparse-Grid (FULL/BITSET) 백엔드만 사용한 표면 복셀화 + Solid 채우기 (CPU)
//  - 타일 32x32x32, 오픈어드레싱 해시(선형 프로빙) → CUDA 포팅 용이
//  - GridSparse 래퍼 없이, GpuFriendlySparseGridFB 두 개(Surface, Solid)만 사용
// ============================================================================

#include "pch.h"
#include "Common/MeshData.h"
#include "ConvexDecomposition.h"
#include <fstream>
#include <algorithm>
#include <queue>
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// --------------------- SAT: tri-box overlap (그리드공간) ---------------------
static inline bool TriBoxOverlapGridF32(
	const float center[3], // 박스 중심(복셀 center: i+0.5)
	const float V0[3],
	const float V1[3],
	const float V2[3],
	const float eps = 1e-4f)
{
	// half는 고정 0.5 (그리드 공간 복셀 박스)
	const float hx = 0.5f + eps;
	const float hy = 0.5f + eps;
	const float hz = 0.5f + eps;

	float p0[3] = { V0[0] - center[0], V0[1] - center[1], V0[2] - center[2] };
	float p1[3] = { V1[0] - center[0], V1[1] - center[1], V1[2] - center[2] };
	float p2[3] = { V2[0] - center[0], V2[1] - center[1], V2[2] - center[2] };
	float e0[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
	float e1[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };
	float e2[3] = { p0[0] - p2[0], p0[1] - p2[1], p0[2] - p2[2] };

	auto axisTest = [&](
		float a, float b,
		float fa, float fb,
		float v0a, float v0b,
		float v1a, float v1b,
		float v2a, float v2b,
		float ha, float hb)->bool
		{
			float q0 = a * v0a - b * v0b;
			float q1 = a * v1a - b * v1b;
			float q2 = a * v2a - b * v2b;
			float mn = fminf(q0, fminf(q1, q2));
			float mx = fmaxf(q0, fmaxf(q1, q2));
			float rad = ha * fa + hb * fb + eps;
			return !(mn > rad || mx < -rad);
		};

	float fe0x = fabsf(e0[0]), fe0y = fabsf(e0[1]), fe0z = fabsf(e0[2]);
	float fe1x = fabsf(e1[0]), fe1y = fabsf(e1[1]), fe1z = fabsf(e1[2]);
	float fe2x = fabsf(e2[0]), fe2y = fabsf(e2[1]), fe2z = fabsf(e2[2]);

	// 9 cross axes
	if (!axisTest(e0[2], e0[1], fe0z, fe0y, p0[1], p0[2], p1[1], p1[2], p2[1], p2[2], hy, hz)) return false;
	if (!axisTest(e1[2], e1[1], fe1z, fe1y, p0[1], p0[2], p1[1], p1[2], p2[1], p2[2], hy, hz)) return false;
	if (!axisTest(e2[2], e2[1], fe2z, fe2y, p0[1], p0[2], p1[1], p1[2], p2[1], p2[2], hy, hz)) return false;

	if (!axisTest(e0[2], e0[0], fe0z, fe0x, p0[0], p0[2], p1[0], p1[2], p2[0], p2[2], hx, hz)) return false;
	if (!axisTest(e1[2], e1[0], fe1z, fe1x, p0[0], p0[2], p1[0], p1[2], p2[0], p2[2], hx, hz)) return false;
	if (!axisTest(e2[2], e2[0], fe2z, fe2x, p0[0], p0[2], p1[0], p1[2], p2[0], p2[2], hx, hz)) return false;

	if (!axisTest(e0[1], e0[0], fe0y, fe0x, p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], hx, hy)) return false;
	if (!axisTest(e1[1], e1[0], fe1y, fe1x, p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], hx, hy)) return false;
	if (!axisTest(e2[1], e2[0], fe2y, fe2x, p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], hx, hy)) return false;

	// box axes
	auto minmax3 = [](float a, float b, float c, float& mn, float& mx) { mn = fminf(a, fminf(b, c)); mx = fmaxf(a, fmaxf(b, c)); };
	float mn, mx;
	minmax3(p0[0], p1[0], p2[0], mn, mx); if (mn > hx + eps || mx < -hx - eps) return false;
	minmax3(p0[1], p1[1], p2[1], mn, mx); if (mn > hy + eps || mx < -hy - eps) return false;
	minmax3(p0[2], p1[2], p2[2], mn, mx); if (mn > hz + eps || mx < -hz - eps) return false;

	// triangle plane
	float n[3] =
	{
		e0[1] * e1[2] - e0[2] * e1[1],
		e0[2] * e1[0] - e0[0] * e1[2],
		e0[0] * e1[1] - e0[1] * e1[0]
	};
	float vmin[3], vmax[3];
	for (int i = 0; i < 3; ++i)
	{
		if (n[i] >= 0.f)
		{
			vmin[i] = -hx;
			vmax[i] = hx;
		}
		else
		{
			vmin[i] = hx;
			vmax[i] = -hx;
		}
	}
	float d = -(n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]);
	float distMin = n[0] * vmin[0] + n[1] * vmin[1] + n[2] * vmin[2] + d;
	float distMax = n[0] * vmax[0] + n[1] * vmax[1] + n[2] * vmax[2] + d;
	if (distMin > eps && distMax > eps) return false;
	if (distMin < -eps && distMax < -eps) return false;

	return true;
}

// ----------------------- 표면 복셀화 (Surface만 세팅) -----------------------
static void VoxelizeSurface_SAT_ToSparse(
	const FLOAT3* vertices,
	const uint16_t* indices,
	int numTriangles,
	int nx, int ny, int nz,
	float cell,
	const FLOAT3& origin,
	GpuFriendlySparseGridFB& surface)
{
	auto toGrid = [&](const FLOAT3& p)->FLOAT3
		{
			return FLOAT3{ (p.x - origin.x) / cell, (p.y - origin.y) / cell, (p.z - origin.z) / cell };
		};

	for (int f = 0; f < numTriangles; ++f)
	{
		const uint16_t i0 = indices[3 * f + 0];
		const uint16_t i1 = indices[3 * f + 1];
		const uint16_t i2 = indices[3 * f + 2];

		const FLOAT3 a = toGrid(vertices[i0]);
		const FLOAT3 b = toGrid(vertices[i1]);
		const FLOAT3 c = toGrid(vertices[i2]);

		const float minx = fminf(a.x, fminf(b.x, c.x));
		const float miny = fminf(a.y, fminf(b.y, c.y));
		const float minz = fminf(a.z, fminf(b.z, c.z));
		const float maxx = fmaxf(a.x, fmaxf(b.x, c.x));
		const float maxy = fmaxf(a.y, fmaxf(b.y, c.y));
		const float maxz = fmaxf(a.z, fmaxf(b.z, c.z));

		int gx0 = (int)std::floor(minx - 0.5f) - 1;
		int gy0 = (int)std::floor(miny - 0.5f) - 1;
		int gz0 = (int)std::floor(minz - 0.5f) - 1;
		int gx1 = (int)std::ceil(maxx + 0.5f) + 1;
		int gy1 = (int)std::ceil(maxy + 0.5f) + 1;
		int gz1 = (int)std::ceil(maxz + 0.5f) + 1;

		gx0 = std::max(gx0, 0);
		gy0 = std::max(gy0, 0);
		gz0 = std::max(gz0, 0);
		gx1 = std::min(gx1, nx - 1);
		gy1 = std::min(gy1, ny - 1);
		gz1 = std::min(gz1, nz - 1);

		const float V0[3] = { a.x, a.y, a.z };
		const float V1[3] = { b.x, b.y, b.z };
		const float V2[3] = { c.x, c.y, c.z };

		for (int z = gz0; z <= gz1; ++z)
		{
			for (int y = gy0; y <= gy1; ++y)
			{
				for (int x = gx0; x <= gx1; ++x)
				{
					const float center[3] = { x + 0.5f, y + 0.5f, z + 0.5f };
					if (TriBoxOverlapGridF32(center, V0, V1, V2))
					{
						surface.SetVoxelIndex(x, y, z, true);
					}
				}
			}
		}
	}
}

// ----------------------------- Solid 만들기 -----------------------------
static void MakeSolidFromSurfaceSparse(
	int nx, int ny, int nz,
	const GpuFriendlySparseGridFB& surface,
	GpuFriendlySparseGridFB* outSolid)
{
	// outside 방문표시는 임시 dense 벡터 (결과 저장은 sparse)
	std::vector<uint8_t> outside((size_t)nx * ny * nz, 0);
	auto idOf = [&](int x, int y, int z)->size_t { return (size_t)(z * ny + y) * nx + x; };
	auto push = [&](std::queue<int3>& q, int x, int y, int z)
		{
			if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return;
			const size_t id = idOf(x, y, z);
			if (outside[id]) return;
			if (surface.GetVoxelIndex(x, y, z)) return; // 표면은 벽
			outside[id] = 1; q.push({ x,y,z });
		};

	std::queue<int3> q;
	// 경계 씨드
	for (int x = 0; x < nx; x++) for (int y = 0; y < ny; y++) { push(q, x, y, 0); push(q, x, y, nz - 1); }
	for (int z = 0; z < nz; z++) for (int x = 0; x < nx; x++) { push(q, x, 0, z); push(q, x, ny - 1, z); }
	for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) { push(q, 0, y, z); push(q, nx - 1, y, z); }

	const int off[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
	while (!q.empty())
	{
		auto p = q.front(); q.pop();
		for (auto& d : off)
		{
			push(q, p.x + d[0], p.y + d[1], p.z + d[2]);
		}
	}

	// 내부 채움 + 표면 포함
	for (int z = 0; z < nz; ++z)
	{
		for (int y = 0; y < ny; ++y)
		{
			for (int x = 0; x < nx; ++x)
			{
				size_t id = idOf(x, y, z);
				//if (!outside[id] || surface.GetVoxelIndex(x, y, z)) // 내부 or 표면
				//{
				//	outSolid->SetVoxelIndex(x, y, z, true);
				//}
				if (!outside[id]) // 내부만 (표면 제외)
				{
					outSolid->SetVoxelIndex(x, y, z, true);
				}
			}
		}
	}
}

// ---------------------- 엔트리: Sparse로 직접 생성 ----------------------
// 사용법:
//   GpuFriendlySparseGridFB surface(voxelSize, originInit), solid(voxelSize, originInit);
//   VoxelizeToSparse(mesh, voxelSize, surface, solid, result);
//   (옵션) DumpSolidToUnityTxt(surface/solid, result);
void VoxelizeToSparse(const MeshData& mesh, float voxelSize, GpuFriendlySparseGridFB* outSolidVoxelGrid)
{
	GpuFriendlySparseGridFB surface;
	// Bounds & 그리드 배치(대칭 정렬)
	Bounds bounds = CalculateBounds(mesh);
	const float s = voxelSize;

	bounds.Min -= FLOAT3{ s * 0.5f, s * 0.5f, s * 0.5f };
	bounds.Max += FLOAT3{ s * 0.5f, s * 0.5f, s * 0.5f };

	FLOAT3 snappedMin = bounds.Min;
	snappedMin.x = std::floor(bounds.Min.x / s) * s;
	snappedMin.y = std::floor(bounds.Min.y / s) * s;
	snappedMin.z = std::floor(bounds.Min.z / s) * s;

	const int nx = (int)std::ceil((bounds.Max.x - snappedMin.x) / s);
	const int ny = (int)std::ceil((bounds.Max.y - snappedMin.y) / s);
	const int nz = (int)std::ceil((bounds.Max.z - snappedMin.z) / s);


	// 그리드 재설정
	surface.Clear();
	outSolidVoxelGrid->Clear();
	surface.Reconfigure(s, snappedMin);
	outSolidVoxelGrid->Reconfigure(s, snappedMin);

	// 입력 포지션 복사
	std::vector<FLOAT3> vertices(mesh.Vertices.size());
	for (size_t i = 0; i < mesh.Vertices.size(); ++i) vertices[i] = mesh.Vertices[i].Position;

	// 표면 복셀화 → Surface
	VoxelizeSurface_SAT_ToSparse(vertices.data(), mesh.Indices.data(), (int)mesh.Indices.size() / 3,
		nx, ny, nz, s, snappedMin, surface);

	// Solid 만들기
	MakeSolidFromSurfaceSparse(nx, ny, nz, surface, outSolidVoxelGrid);
}
