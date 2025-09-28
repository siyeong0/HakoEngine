#pragma once
#include <cstdint>
#include <vector>

struct Vertex;

struct MeshData
{
	std::vector<Vertex> Vertices;
	std::vector<uint16_t> Indices;
};