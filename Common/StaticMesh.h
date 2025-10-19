#pragma once
#include <vector>
#include "Image.h"
#include "Common/Common.h"
#include "Shaders/HLSL_CPP_CommonTypes.h"

struct Material
{
	Image Diffuse;
	Image Normal;
	Image Specular;
	Image Metallic;
	Image Roughness;
	MATERIAL_TYPE Type = MATERIAL_TYPE_DEFAULT;
};

struct MeshSection
{
	std::vector<uint16_t> Indices;
	Material Material;
	Bounds LocalBounds;
};

struct StaticMesh
{
	std::vector<FVector3> Positions;
	std::vector<FVector3> Normals;
	std::vector<FVector3> Tangents;
	std::vector<FVector2> UVs;
	std::vector<FVector3> Colors;

	std::vector<MeshSection> Sections;

	Bounds MeshBounds;

	void BeginCreate(
		const std::vector<FVector3>& vertices, 
		const std::vector<FVector3>& normals = {},
		const std::vector<FVector3>& tangents = {},
		const std::vector<FVector2>& uvs = {},
		const std::vector<FVector3>& colors = {});
	void InsertSection(const std::vector<uint16_t> indices, const Material&& material = {});
	void EndCreate();

	bool LoadFromFile(const char* filename, float scale = 1.0f);

	std::vector<Vertex> GetVertexArray() const;

	static StaticMesh CreateUnitCubeMesh();
	static StaticMesh CreateBoxMesh(float width, float height, float depth);
	static StaticMesh CreateSphereMesh(float radius, int segments, int rings);
	static StaticMesh CreateGridMesh(float width, float height, int rows, int columns);
	static StaticMesh CreateCylinderMesh(float radius, float height, int segments);
	static StaticMesh CreateConeMesh(float radius, float height, int segments);
	static StaticMesh CreatePlaneMesh(float width, float height);
};