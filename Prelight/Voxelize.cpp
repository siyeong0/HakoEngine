#include "pch.h"
#include "Common/MeshData.h"
#include "ConvexDecomposition.h"

inline FLOAT2 project(FLOAT3 p, FLOAT3 center, FLOAT3 tangent, FLOAT3 bitangent)
{
	FLOAT3 d = p - center;
	return { FLOAT3::Dot(d, tangent), FLOAT3::Dot(d, bitangent) };
}

inline FLOAT3 backProject(FLOAT2 uv, float d, FLOAT3 center, FLOAT3 tangent, FLOAT3 bitangent, FLOAT3 normal)
{
	return center + uv.x * tangent + uv.y * bitangent + d * normal;
}

// Check if point p is inside the triangle defined by vertices a, b, c in 2D space.
inline bool isInsideTriangle2D(FLOAT2 p, FLOAT2 a, FLOAT2 b, FLOAT2 c)
{
	FLOAT2 ab = b - a;
	FLOAT2 bc = c - b;
	FLOAT2 ca = a - c;
	FLOAT2 ap = p - a;
	FLOAT2 bp = p - b;
	FLOAT2 cp = p - c;

	float w1 = ab.x * ap.y - ab.y * ap.x;
	float w2 = bc.x * bp.y - bc.y * bp.x;
	float w3 = ca.x * cp.y - ca.y * cp.x;
	return (w1 >= 0 && w2 >= 0 && w3 >= 0) || (w1 <= 0 && w2 <= 0 && w3 <= 0);
}

// Compute barycentric coordinate of point p with respect to triangle defined by vertices a, b, c in 2D space.
	// What is "Barycentric Coordinate"? :https://en.wikipedia.org/wiki/Barycentric_coordinate_system
inline FLOAT3 computeBarycentric2D(FLOAT2 p, FLOAT2 a, FLOAT2 b, FLOAT2 c)
{
	FLOAT2 v0 = b - a, v1 = c - a, v2 = p - a;
	float d00 = FLOAT2::Dot(v0, v0), d01 = FLOAT2::Dot(v0, v1), d11 = FLOAT2::Dot(v1, v1);
	float d20 = FLOAT2::Dot(v2, v0), d21 = FLOAT2::Dot(v2, v1);
	float denom = d00 * d11 - d01 * d01;

	if (denom == 0.0f) return { 0.0f, 0.0f, 1.0f };
	float v = (d11 * d20 - d01 * d21) / denom;
	float w = (d00 * d21 - d01 * d20) / denom;
	float u = 1.0f - v - w;
	return { u, v, w };
}

// Compute incenter(³»½É) of a triangle defined by three vertices.
inline FLOAT3 computeIncenter(FLOAT3 v0, FLOAT3 v1, FLOAT3 v2)
{
	float a = FVector3::Length(v1 - v2);
	float b = FVector3::Length(v2 - v0);
	float c = FVector3::Length(v0 - v1);

	float sum = a + b + c;
	ASSERT(sum != 0.0f, "Invalid triangle. Cannot compute incenter.");
	return (a * v0 + b * v1 + c * v2) / sum;
}

// Compute intersection point of two lines defined by FVector4{pointA.x, pointA.y, pointB.x, pointB.y}.
inline FLOAT2 computeLineIntersection(FLOAT4 line0, FLOAT4 line1)
{
	FLOAT2 a{ line0.x, line0.y };
	FLOAT2 b{ line0.z, line0.w };
	FLOAT2 c{ line1.x, line1.y };
	FLOAT2 d{ line1.z, line1.w };

	FLOAT2 d1 = b - a;
	FLOAT2 d2 = d - c;

	float denom = d1.x * d2.y - d1.y * d2.x;

	// ASSERT(denom != 0.0f, "Lines are parallel or coincident, cannot compute intersection.");
	//if (denom == 0.0f)
	//	return FVector2::Zero(); // Lines are parallel or coincident, return zero vector.

	FLOAT2 ac = c - a;
	float t = (ac.x * d2.y - ac.y * d2.x) / denom;
	return a + d1 * t;
}

inline FLOAT2 outwardEdgeNormal(FLOAT2 p0, FLOAT2 p1, FLOAT2 opp)
{
	FLOAT2 edge = p1 - p0;
	FLOAT2 perp = { -edge.y, edge.x }; // Perpendicular.

	FLOAT2 center = (p0 + p1) * 0.5f;
	FLOAT2 toOpp = opp - center;

	perp = perp * (FLOAT2::Dot(perp, toOpp) > 0 ? -1.0f : 1.0f);
	return FLOAT2::Normalize(perp);
};

inline FLOAT4 expandedEdge(FLOAT2 p0, FLOAT2 p1, float dist, FLOAT2 opp)
{
	FLOAT2 dir = outwardEdgeNormal(p0, p1, opp);
	FLOAT2 offset = dir * dist * 2.0f; // TODO: Why 2.0??????
	return { p0.x + offset.x, p0.y + offset.y, p1.x + offset.x, p1.y + offset.y };
}

void voxelize(const FLOAT3* vertices, const uint16_t* indices, int numTriangles, Grid* outGrid)
{
	float voxelSize = outGrid->CellSize;
	for (int faceIdx = 0; faceIdx < numTriangles; ++faceIdx)
	{
		// Dummy rasterization logic for illustration purposes
		uint16_t idx0 = indices[faceIdx * 3 + 0];
		uint16_t idx1 = indices[faceIdx * 3 + 1];
		uint16_t idx2 = indices[faceIdx * 3 + 2];
		const FLOAT3 v0 = vertices[idx0];
		const FLOAT3 v1 = vertices[idx1];
		const FLOAT3 v2 = vertices[idx2];

		const FLOAT3 normal = FLOAT3::Normalize(FLOAT3::Cross(v1 - v0, v2 - v0));
		const FLOAT3 center = computeIncenter(v0, v1, v2);

		// Projection biasis
		const FLOAT3 tangent = (std::fabsf(normal.x) < 0.9f)
			? FLOAT3::Normalize(FLOAT3::Cross(normal, { 1.0f, 0.0f, 0.0f }))
			: FLOAT3::Normalize(FLOAT3::Cross(normal, { 0.0f, 1.0f, 0.0f }));
		const FLOAT3 bitangent = FLOAT3::Normalize(FLOAT3::Cross(normal, tangent)); // guaranteed orthogonal

		// Proeject vertices to dominant axis.
		FLOAT2 p0 = project(v0, center, tangent, bitangent);
		FLOAT2 p1 = project(v1, center, tangent, bitangent);
		FLOAT2 p2 = project(v2, center, tangent, bitangent);
		float d0 = FLOAT3::Dot(v0 - center, normal);
		float d1 = FLOAT3::Dot(v1 - center, normal);
		float d2 = FLOAT3::Dot(v2 - center, normal);

		// Compute rasterization bounds.
		FLOAT2 minP = FLOAT2::Min(FLOAT2::Min(p0, p1), p2);
		FLOAT2 maxP = FLOAT2::Max(FLOAT2::Max(p0, p1), p2);

		FLOAT2 minGrid = {
			std::floor(minP.x / voxelSize) * voxelSize - voxelSize * 0.5f, // Center of the voxel.
			std::floor(minP.y / voxelSize) * voxelSize - voxelSize * 0.5f
		};
		FLOAT2 maxGrid = {
			std::ceil(maxP.x / voxelSize) * voxelSize + voxelSize * 0.5f,
			std::ceil(maxP.y / voxelSize) * voxelSize + voxelSize * 0.5f
		};

		// To ensure conservative rasterization,
		// exapnd projected triangle for checking voxel coverage.
		FLOAT4 edge01 = expandedEdge(p0, p1, voxelSize, { 0.0f, 0.0f });
		FLOAT4 edge12 = expandedEdge(p1, p2, voxelSize, { 0.0f, 0.0f });
		FLOAT4 edge20 = expandedEdge(p2, p0, voxelSize, { 0.0f, 0.0f });

		FLOAT2 ep0 = computeLineIntersection(edge01, edge20);
		FLOAT2 ep1 = computeLineIntersection(edge01, edge12);
		FLOAT2 ep2 = computeLineIntersection(edge12, edge20);

		// Rasterize the triangle in 2D space
		for (float y = minGrid.y; y <= maxGrid.y; y += voxelSize)
		{
			for (float x = minGrid.x; x <= maxGrid.x; x += voxelSize)
			{
				FLOAT2 p = { x, y };
				if (!isInsideTriangle2D(p, ep0, ep1, ep2)) continue;

				FLOAT3 barycentric = computeBarycentric2D(p, p0, p1, p2);
				float d = (barycentric.x * d0 + barycentric.y * d1 + barycentric.z * d2);

				//for (int dz = -1; dz <= 1; ++dz) // For conservative rasterization, sample 3 layers.
				//{
				//	FLOAT3 voxelPos = backProject(p, d + dz * voxelSize, center, tangent, bitangent, normal);
				//	outGrid->SetVoxel(voxelPos, 1);
				//}
				FLOAT3 voxelPos = backProject(p, d, center, tangent, bitangent, normal);
				outGrid->SetVoxel(voxelPos, 1);
			}
		}
	}
}

#include <fstream>

Grid Voxelize(const MeshData& mesh, float voxelSize)
{
	Bounds bounds = CalculateBounds(mesh);
	// Dummy implementation for illustration purposes
	Grid grid;
	grid.CellSize = voxelSize;
	grid.nx = static_cast<int>((bounds.Max.x - bounds.Min.x) / voxelSize);
	grid.ny = static_cast<int>((bounds.Max.y - bounds.Min.y) / voxelSize);
	grid.nz = static_cast<int>((bounds.Max.z - bounds.Min.z) / voxelSize);
	grid.Origin = bounds.Min;
	grid.Voxels.resize(grid.nx * grid.ny * grid.nz, 0);

	std::vector<FLOAT3> vertices(mesh.Vertices.size());
	for (size_t i = 0; i < mesh.Vertices.size(); ++i)
	{
		vertices[i] = mesh.Vertices[i].Position;
	}

	voxelize(vertices.data(), mesh.Indices.data(), mesh.Indices.size() / 3, &grid);

	// Save to txt for debugging
	{
		std::ofstream ofs("voxels.txt");
		for (int z = 0; z < grid.nz; ++z)
		{
			ofs << "Layer " << z << ":\n";
			for (int y = 0; y < grid.ny; ++y)
			{
				for (int x = 0; x < grid.nx; ++x)
				{
					ofs << (grid.GetVoxel(x, y, z) ? '#' : ' ') << " ";
				}
				ofs << "\n";
			}
			ofs << "\n";
		}
	}

	return grid;
}