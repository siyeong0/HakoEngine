#pragma once
#include <cstdint>
#include <vector>

struct Vertex;

struct MeshData
{
	std::vector<Vertex> Vertices;
	std::vector<uint16_t> Indices;
};

inline Bounds CalculateBounds(const MeshData& mesh)
{
	Bounds bounds;
	for (const auto& vertex : mesh.Vertices)
	{
		bounds.Encapsulate(vertex.Position);
	}
	return bounds;
}