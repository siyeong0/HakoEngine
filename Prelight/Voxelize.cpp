#include "pch.h"
#include "Common/MeshData.h"
#include "ConvexDecomposition.h"
#include <fstream>
#include <algorithm>

// ------------------------ Math utils ------------------------
static inline FLOAT3 Min3(const FLOAT3& a, const FLOAT3& b, const FLOAT3& c)
{
	return { std::min({a.x,b.x,c.x}), std::min({a.y,b.y,c.y}), std::min({a.z,b.z,c.z}) };
}
static inline FLOAT3 Max3(const FLOAT3& a, const FLOAT3& b, const FLOAT3& c)
{
	return { std::max({a.x,b.x,c.x}), std::max({a.y,b.y,c.y}), std::max({a.z,b.z,c.z}) };
}

// Tomas Akenine-Möller, "A Fast Triangle-Box Overlap Test", 2001.
// robust double-precision 버전 (그리드 축 기준 SAT)
static inline bool TriBoxOverlapGridF32(
	const float center[3],
	const float half[3],
	const float v0[3],
	const float v1[3],
	const float v2[3],
	const float eps)
{
	// 1) 삼각형을 박스 좌표계(박스 중심 원점)로 이동
	float p0[3] = { v0[0] - center[0], v0[1] - center[1], v0[2] - center[2] };
	float p1[3] = { v1[0] - center[0], v1[1] - center[1], v1[2] - center[2] };
	float p2[3] = { v2[0] - center[0], v2[1] - center[1], v2[2] - center[2] };
	// 2) 에지
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
			float p0 = a * v0a - b * v0b;
			float p1 = a * v1a - b * v1b;
			float p2 = a * v2a - b * v2b;
			float minp = fminf(p0, fminf(p1, p2));
			float maxp = fmaxf(p0, fmaxf(p1, p2));
			float rad = ha * fa + hb * fb + eps;
			return !(minp > rad || maxp < -rad);
		};

	// 3) 9개의 cross-product 축 테스트 (X×edges, Y×edges, Z×edges)
	// X축과의 조합
	float fe0x = fabsf(e0[0]), fe0y = fabsf(e0[1]), fe0z = fabsf(e0[2]);
	float fe1x = fabsf(e1[0]), fe1y = fabsf(e1[1]), fe1z = fabsf(e1[2]);
	float fe2x = fabsf(e2[0]), fe2y = fabsf(e2[1]), fe2z = fabsf(e2[2]);

	// X×edges
	if (!axisTest(e0[2], e0[1], fe0z, fe0y, p0[1], p0[2], p1[1], p1[2], p2[1], p2[2], half[1], half[2])) return false;
	if (!axisTest(e1[2], e1[1], fe1z, fe1y, p0[1], p0[2], p1[1], p1[2], p2[1], p2[2], half[1], half[2])) return false;
	if (!axisTest(e2[2], e2[1], fe2z, fe2y, p0[1], p0[2], p1[1], p1[2], p2[1], p2[2], half[1], half[2])) return false;
	// Y×edges
	if (!axisTest(e0[2], e0[0], fe0z, fe0x, p0[0], p0[2], p1[0], p1[2], p2[0], p2[2], half[0], half[2])) return false;
	if (!axisTest(e1[2], e1[0], fe1z, fe1x, p0[0], p0[2], p1[0], p1[2], p2[0], p2[2], half[0], half[2])) return false;
	if (!axisTest(e2[2], e2[0], fe2z, fe2x, p0[0], p0[2], p1[0], p1[2], p2[0], p2[2], half[0], half[2])) return false;
	// Z×edges
	if (!axisTest(e0[1], e0[0], fe0y, fe0x, p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], half[0], half[1])) return false;
	if (!axisTest(e1[1], e1[0], fe1y, fe1x, p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], half[0], half[1])) return false;
	if (!axisTest(e2[1], e2[0], fe2y, fe2x, p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], half[0], half[1])) return false;

	// 4) 박스 자체 축(X,Y,Z) 투영 간단 테스트
	auto findMinMax = [](float x0, float x1, float x2, float& mn, float& mx) {mn = fminf(x0, fminf(x1, x2)); mx = fmaxf(x0, fmaxf(x1, x2)); };
	float mn, mx;
	findMinMax(p0[0], p1[0], p2[0], mn, mx); if (mn > half[0] + eps || mx < -half[0] - eps) return false;
	findMinMax(p0[1], p1[1], p2[1], mn, mx); if (mn > half[1] + eps || mx < -half[1] - eps) return false;
	findMinMax(p0[2], p1[2], p2[2], mn, mx); if (mn > half[2] + eps || mx < -half[2] - eps) return false;

	// 5) 삼각형 평면과 박스의 교차 여부 (삼각형 노멀 축)
	float n[3] =
	{
		e0[1] * e1[2] - e0[2] * e1[1],
		e0[2] * e1[0] - e0[0] * e1[2],
		e0[0] * e1[1] - e0[1] * e1[0]
	};
	// 박스 반지름: |n| dot halfSize projected onto |n|-aligned axes (= |n|·halfSize on L1 norm of normalized n against axes)
	// 더 간단하게는 박스 8코너를 평면에 투영한 min/max가 0의 양/음에 걸치면 교차.
	// 여기서는 "plane-box overlap"의 빠른 근사:
	float vmin[3], vmax[3];
	for (int i = 0; i < 3; ++i)
	{
		if (n[i] >= 0.f)
		{
			vmin[i] = -half[i];
			vmax[i] = half[i];
		}
		else
		{
			vmin[i] = half[i];
			vmax[i] = -half[i];
		}
	}
	// 삼각형 한 점(v0)에서 평면 거리
	float d = -(n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]);
	float distMin = n[0] * vmin[0] + n[1] * vmin[1] + n[2] * vmin[2] + d;
	float distMax = n[0] * vmax[0] + n[1] * vmax[1] + n[2] * vmax[2] + d;

	if (distMin > eps && distMax > eps) return false;
	if (distMin < -eps && distMax < -eps) return false;

	return true;
}

// ------------------------------------------------------------
// FP32 SAT 기반 표면 복셀화
//  - 삼각형을 그리드 공간으로 변환 → tri AABB로 후보 복셀 범위 산출
//  - 후보 복셀(center, half)과 SAT 교차 → SetVoxel(center)
// ------------------------------------------------------------
void voxelize(const FLOAT3* vertices, const uint16_t* indices, int numTriangles, Grid* outGrid)
{
	const float s = outGrid->CellSize;
	const FLOAT3 origin = outGrid->Origin;

	// 보수화: halfSize/비교용
	const float eps = 1e-4f;              // 스케일 고정 ε (그리드 공간)
	const float halfX = 0.5f + 5e-5f;       // half(0.5)에 소량 팽창
	const float half[3] = { halfX, halfX, halfX };

	auto toGrid = [&](const FLOAT3& p)->FLOAT3 {return FLOAT3{ (p.x - origin.x) / s, (p.y - origin.y) / s, (p.z - origin.z) / s }; };

	for (int f = 0; f < numTriangles; ++f)
	{
		const uint16_t i0 = indices[3 * f + 0];
		const uint16_t i1 = indices[3 * f + 1];
		const uint16_t i2 = indices[3 * f + 2];

		// 그리드 공간 좌표로 변환
		const FLOAT3 a = toGrid(vertices[i0]);
		const FLOAT3 b = toGrid(vertices[i1]);
		const FLOAT3 c = toGrid(vertices[i2]);

		// tri AABB (그리드 공간)
		const float minx = fminf(a.x, fminf(b.x, c.x));
		const float miny = fminf(a.y, fminf(b.y, c.y));
		const float minz = fminf(a.z, fminf(b.z, c.z));
		const float maxx = fmaxf(a.x, fmaxf(b.x, c.x));
		const float maxy = fmaxf(a.y, fmaxf(b.y, c.y));
		const float maxz = fmaxf(a.z, fmaxf(b.z, c.z));

		// 복셀 center 인덱스 범위 (center=i+0.5, 박스 half=0.5) ⇒ ±0.5 확장 + 여유 1셀
		int gx0 = (int)floorf(minx - 0.5f) - 1;
		int gy0 = (int)floorf(miny - 0.5f) - 1;
		int gz0 = (int)floorf(minz - 0.5f) - 1;
		int gx1 = (int)ceilf(maxx + 0.5f) + 1;
		int gy1 = (int)ceilf(maxy + 0.5f) + 1;
		int gz1 = (int)ceilf(maxz + 0.5f) + 1;

		gx0 = std::max(gx0, 0);            gy0 = std::max(gy0, 0);            gz0 = std::max(gz0, 0);
		gx1 = std::min(gx1, outGrid->nx - 1); gy1 = std::min(gy1, outGrid->ny - 1); gz1 = std::min(gz1, outGrid->nz - 1);

		// float 배열 뷰 (SAT 함수 시그니처 맞춤)
		const float v0[3] = { a.x, a.y, a.z };
		const float v1[3] = { b.x, b.y, b.z };
		const float v2[3] = { c.x, c.y, c.z };

		for (int z = gz0; z <= gz1; ++z)
		{
			for (int y = gy0; y <= gy1; ++y)
			{
				for (int x = gx0; x <= gx1; ++x)
				{
					const float center[3] = { x + 0.5f, y + 0.5f, z + 0.5f };

					if (TriBoxOverlapGridF32(center, half, v0, v1, v2, eps))
					{
						// 월드 좌표로 되돌린 '복셀 중심'을 넘겨 SetVoxel (경계 체크 필수)
						const FLOAT3 wp{
							origin.x + center[0] * s,
							origin.y + center[1] * s,
							origin.z + center[2] * s
						};
						outGrid->SetVoxel(wp, 1);
					}
				}
			}
		}
	}
}

// ------------------------ Grid build + Dump (대칭 정렬 유지) ------------------------
Grid Voxelize(const MeshData& mesh, float voxelSize)
{
	Bounds bounds = CalculateBounds(mesh);

	const float s = voxelSize;
	// 반 셀 패딩 + 원점 스냅 + ceil 해상도 (대칭 정렬)
	bounds.Min -= FLOAT3{ s * 0.5f, s * 0.5f, s * 0.5f };
	bounds.Max += FLOAT3{ s * 0.5f, s * 0.5f, s * 0.5f };

	FLOAT3 snappedMin = bounds.Min;
	snappedMin.x = std::floor(bounds.Min.x / s) * s;
	snappedMin.y = std::floor(bounds.Min.y / s) * s;
	snappedMin.z = std::floor(bounds.Min.z / s) * s;

	const int nx = (int)std::ceil((bounds.Max.x - snappedMin.x) / s);
	const int ny = (int)std::ceil((bounds.Max.y - snappedMin.y) / s);
	const int nz = (int)std::ceil((bounds.Max.z - snappedMin.z) / s);

	Grid grid;
	grid.CellSize = s;
	grid.nx = nx; grid.ny = ny; grid.nz = nz;
	grid.Origin = snappedMin;
	grid.Voxels.resize((size_t)nx * ny * nz, 0);

	// 입력 포지션 복사
	std::vector<FLOAT3> vertices(mesh.Vertices.size());
	for (size_t i = 0; i < mesh.Vertices.size(); ++i)
		vertices[i] = mesh.Vertices[i].Position;

	voxelize(vertices.data(), mesh.Indices.data(), (int)mesh.Indices.size() / 3, &grid);

	// 디버그 덤프 (Unity 시각화용)
	{
		std::ofstream ofs("C:\\Dev\\VoxVis\\Assets\\voxels.txt");
		ofs << "Voxel Grid (" << grid.nx << " x " << grid.ny << " x " << grid.nz << "), Cell Size: " << grid.CellSize << "\n";
		ofs << "Bounds"
			<< " Min(" << bounds.Min.x << ", " << bounds.Min.y << ", " << bounds.Min.z << ")"
			<< " Max(" << bounds.Max.x << ", " << bounds.Max.y << ", " << bounds.Max.z << ")\n";
		for (int z = 0; z < grid.nz; ++z)
		{
			ofs << "Layer " << z << ":\n";
			for (int y = 0; y < grid.ny; ++y)
			{
				for (int x = 0; x < grid.nx; ++x)
					ofs << (grid.GetVoxel(x, y, z) ? '#' : ' ') << " ";
				ofs << "\n";
			}
			ofs << "\n";
		}
	}

	return grid;
}
