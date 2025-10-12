#pragma once

struct QuickHull
{
	std::vector<FLOAT3> Vertices;
	std::vector<uint32_t> Indices;

	QuickHull() = default;
	~QuickHull() = default;

	static QuickHull Create(const std::vector<FLOAT3>& points);

	bool SaveAsOBJ(const std::string& path) const;
};
