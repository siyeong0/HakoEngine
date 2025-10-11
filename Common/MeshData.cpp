#include "Common/Common.h"

#include <iostream>

#pragma push_macro("new")
#ifdef new
#undef new
#endif

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#pragma pop_macro("new")

#include "MeshData.h"

void ComputeMeshNormals(MeshData* outMesh)
{
	for (int triIdx = 0; triIdx < outMesh->Indices.size() / 3; ++triIdx)
	{
		const FVector3& v0 = outMesh->Vertices[outMesh->Indices[3 * triIdx + 0]].Position;
		const FVector3& v1 = outMesh->Vertices[outMesh->Indices[3 * triIdx + 1]].Position;
		const FVector3& v2 = outMesh->Vertices[outMesh->Indices[3 * triIdx + 2]].Position;
		FVector3 normal = (v1 - v0).Cross(v2 - v0).Normalized();
		outMesh->Vertices[outMesh->Indices[3 * triIdx + 0]].Normal += normal;
		outMesh->Vertices[outMesh->Indices[3 * triIdx + 1]].Normal += normal;
		outMesh->Vertices[outMesh->Indices[3 * triIdx + 2]].Normal += normal;
	}

	for (auto& vertex : outMesh->Vertices)
	{
		vertex.Normal.Normalize();
	}
}

bool LoadMesh(const std::string& path, float scale, MeshData* outMesh)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path,
		aiProcess_Triangulate |
		aiProcess_DropNormals |
		aiProcess_JoinIdenticalVertices);

	if (!scene || !scene->HasMeshes())
	{
		std::cout << "Failed to load model \"" << path << "\"\n";
		return false;
	}

	const aiMesh* mesh = scene->mMeshes[0];
	const int numVertices = mesh->mNumVertices;
	const int numFaces = mesh->mNumFaces;
	const int numIndices = numFaces * 3;

	// Vertices.
	std::vector<FVector3> vertices;
	std::vector<uint16_t> indices;
	std::vector<FVector3> normals;
	std::vector<FVector2> uvs;

	vertices.reserve(numVertices);
	for (int i = 0; i < numVertices; ++i)
	{
		aiVector3D vertex = mesh->mVertices[i];
		vertices.emplace_back(vertex.x * scale, vertex.y * scale, vertex.z * scale);
	}

	// Indices.
	indices.reserve(numIndices);
	for (int i = 0; i < numFaces; ++i)
	{
		aiFace face = mesh->mFaces[i];
		indices.emplace_back(face.mIndices[0]);
		indices.emplace_back(face.mIndices[1]);
		indices.emplace_back(face.mIndices[2]);
	}

	// Normals.
	if (mesh->HasNormals())
	{
		normals.reserve(numVertices);
		for (int i = 0; i < numVertices; ++i)
		{
			aiVector3D normal = mesh->mNormals[i];
			normals.emplace_back(normal.x, normal.y, normal.z);
		}
	}

	if (mesh->HasTextureCoords(0))
	{
		// UVs.
		uvs.clear();
		uvs.reserve(numVertices);
		for (int i = 0; i < numVertices; ++i)
		{
			aiVector3D uv = mesh->mTextureCoords[0][i];
			uvs.emplace_back(uv.x, uv.y);
		}

		// Textures.
		// TODO: Load textures if needed.
	}


	// Fill output mesh.
	outMesh->Vertices.resize(numVertices);
	outMesh->Indices = std::move(indices);
	for (int i = 0; i < numVertices; ++i)
	{
		outMesh->Vertices[i].Position = vertices[i];
		if (!normals.empty())
		{
			outMesh->Vertices[i].Normal = normals[i];
		}
		if (!uvs.empty())
		{
			outMesh->Vertices[i].TexCoord = uvs[i];
		}
	}

	if (normals.empty())
	{
		ComputeMeshNormals(outMesh);
	}

	return true;
}