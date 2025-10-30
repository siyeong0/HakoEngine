#pragma once
#include <vector>
#include "Vertex.h"
#include "Material.h"

struct UMeshSection
{
	std::vector<uint16_t> Indices;
	UMaterial Material;
	Bounds LocalBounds;
};

struct UStaticMesh
{
	std::vector<FVector3> Positions;
	std::vector<FVector3> Normals;
	std::vector<FVector3> Tangents;
	std::vector<FVector2> UVs;
	std::vector<FVector3> Colors;

	std::vector<UMeshSection> Sections;

	Bounds MeshBounds;

	void BeginCreate(
		const std::vector<FVector3>& vertices,
		const std::vector<FVector3>& normals = {},
		const std::vector<FVector3>& tangents = {},
		const std::vector<FVector2>& uvs = {},
		const std::vector<FVector3>& colors = {});
	void InsertSection(const std::vector<uint16_t> indices, const UMaterial&& material = {});
	void EndCreate();

	bool LoadFromFile(const char* filename, float scale = 1.0f);

	std::vector<Vertex> GetVertexArray() const;
	void FlipYAxis();

	static UStaticMesh CreateUnitCubeMesh();
	static UStaticMesh CreateBoxMesh(float width, float height, float depth);
	static UStaticMesh CreateSphereMesh(float radius, int segments, int rings);
	static UStaticMesh CreateGridMesh(float width, float height, int rows, int columns);
	static UStaticMesh CreateCylinderMesh(float radius, float height, int segments);
	static UStaticMesh CreateConeMesh(float radius, float height, int segments);
	static UStaticMesh CreatePlaneMesh(float width, float height);
};