#include <iostream>
#include <limits>
#include <vector>
#include <string>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "StaticMesh.h"

void StaticMesh::BeginCreate(
	const std::vector<FVector3>& vertices,
	const std::vector<FVector3>& normals,
	const std::vector<FVector3>& tangents,
	const std::vector<FVector2>& uvs,
	const std::vector<FVector3>& colors)
{
	Positions.clear();
	Normals.clear();
	Tangents.clear();
	UVs.clear();
	Colors.clear();
	Sections.clear();
	MeshBounds = Bounds();

	Positions = vertices;
	Normals = normals;
	Tangents = tangents;
	Colors = colors;
	UVs = uvs;

	for (const FVector3& v : Positions)
	{
		MeshBounds.Encapsulate(v);
	}
}

void StaticMesh::InsertSection(const std::vector<uint16_t> indices, const Material&& material)
{
	MeshSection section;
	section.Indices = indices;
	section.Material = material;
	for (uint16_t index : indices)
	{
		if (index < Positions.size())
		{
			section.LocalBounds.Encapsulate(Positions[index]);
		}
	}
	Sections.emplace_back(section);
}

void StaticMesh::EndCreate()
{
	// Compute normals if not provided
	if (Normals.empty())
	{
		// Compute normals from geometry
		Normals.resize(Positions.size(), FVector3(0.0f, 0.0f, 0.0f));
		for (const MeshSection& section : Sections)
		{
			for (size_t i = 0; i < section.Indices.size(); i += 3)
			{
				uint16_t i0 = section.Indices[i];
				uint16_t i1 = section.Indices[i + 1];
				uint16_t i2 = section.Indices[i + 2];
				if (i0 < Positions.size() && i1 < Positions.size() && i2 < Positions.size())
				{
					FVector3 v0 = Positions[i0];
					FVector3 v1 = Positions[i1];
					FVector3 v2 = Positions[i2];
					FVector3 normal = FVector3::Cross(v1 - v0, v2 - v0);
					Normals[i0] += normal;
					Normals[i1] += normal;
					Normals[i2] += normal;
				}
			}
		}
		for (FVector3& n : Normals)
		{
			ASSERT(n.Magnitude() > 1e-7, "Zero-length normal.");
			n.Normalize();
		}
	}

	// Compute tangents if not provided
	if (Tangents.empty() && !UVs.empty())
	{
		// Compute tangents from geometry and UVs
		Tangents.resize(Positions.size(), FVector3(0.0f, 0.0f, 0.0f));
		for (const MeshSection& section : Sections)
		{
			for (size_t i = 0; i < section.Indices.size(); i += 3)
			{
				uint16_t i0 = section.Indices[i];
				uint16_t i1 = section.Indices[i + 1];
				uint16_t i2 = section.Indices[i + 2];
				if (i0 < Positions.size() && i1 < Positions.size() && i2 < Positions.size() &&
					i0 < UVs.size() && i1 < UVs.size() && i2 < UVs.size())
				{
					FVector3 v0 = Positions[i0];
					FVector3 v1 = Positions[i1];
					FVector3 v2 = Positions[i2];
					FVector2 uv0 = UVs[i0];
					FVector2 uv1 = UVs[i1];
					FVector2 uv2 = UVs[i2];
					FVector3 deltaPos1 = v1 - v0;
					FVector3 deltaPos2 = v2 - v0;
					FVector2 deltaUV1 = uv1 - uv0;
					FVector2 deltaUV2 = uv2 - uv0;
					float r = 1.0f;
					float denom = (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
					if (denom != 0.0f)
					{
						r = 1.0f / denom;
					}
					FVector3 tangent = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * r;
					Tangents[i0] += tangent;
					Tangents[i1] += tangent;
					Tangents[i2] += tangent;
				}
			}
		}
		for (FVector3& t : Tangents)
		{
			t.Normalize();
		}
	}

	// If no UVs and no Colors, paint to white color
	if (UVs.empty() && Colors.empty())
	{
		Colors.resize(Positions.size(), FVector3(1.0f, 1.0f, 1.0f));
	}
}

// -----------------------------
// Helpers: Image loaders
// -----------------------------
inline void FillImageRGBA(Image& img, int w, int h, const unsigned char* rgba8)
{
	img.Width = w;
	img.Height = h;
	img.Channels = 4;
	img.Data.resize(size_t(w) * size_t(h));
	for (size_t i = 0; i < img.Data.size(); ++i)
	{
		const unsigned char* p = rgba8 + i * 4;
		img.Data[i].r = p[0] / 255.0f;
		img.Data[i].g = p[1] / 255.0f;
		img.Data[i].b = p[2] / 255.0f;
		img.Data[i].a = p[3] / 255.0f;
	}
}

// Load external file (relative to model dir) via stb_image → RGBA8
bool LoadExternalTexture(
	const std::filesystem::path& modelDir,
	const std::string& relPath,
	Image& outImg)
{
	if (relPath.empty()) return false;

	// Assimp material path can be absolute/relative; try relative first
	std::filesystem::path full = modelDir / relPath;
	if (!std::filesystem::exists(full))
	{
		// Fallback: as is (maybe absolute)
		full = relPath;
		if (!std::filesystem::exists(full))
		{
			std::fprintf(stderr, "[StaticMesh] Texture not found: %s\n", relPath.c_str());
			return false;
		}
	}

	int w = 0, h = 0, comp = 0;
	unsigned char* pixels = stbi_load(full.string().c_str(), &w, &h, &comp, 4);
	if (!pixels)
	{
		std::fprintf(stderr, "[StaticMesh] stbi_load failed: %s\n", full.string().c_str());
		return false;
	}

	FillImageRGBA(outImg, w, h, pixels);
	stbi_image_free(pixels);

	return true;
}

// Load embedded texture (*N) from aiScene → RGBA8/float
static bool LoadEmbeddedTexture(
	const aiScene* scene,
	const std::string& starPath,
	Image& outImg)
{
	// "*0" → 0
	int texIndex = 0;
	try { texIndex = std::stoi(starPath.substr(1)); }
	catch (...) { return false; }

	if (texIndex < 0 || texIndex >= int(scene->mNumTextures)) return false;
	const aiTexture* tex = scene->mTextures[texIndex];
	if (!tex) return false;

	if (tex->mHeight == 0)
	{
		// Compressed data (PNG/JPG/…)
		int w = 0, h = 0, comp = 0;
		const unsigned char* src = reinterpret_cast<const unsigned char*>(tex->pcData);
		unsigned char* pixels = stbi_load_from_memory(src, tex->mWidth, &w, &h, &comp, 4);
		if (!pixels)
		{
			std::fprintf(stderr, "[StaticMesh] stbi_load_from_memory failed for embedded texture %s\n", starPath.c_str());
			return false;
		}
		FillImageRGBA(outImg, w, h, pixels);
		stbi_image_free(pixels);
		return true;
	}
	else
	{
		// Uncompressed (aiTexel RGBA8888)
		int w = int(tex->mWidth);
		int h = int(tex->mHeight);
		std::vector<unsigned char> rgba; rgba.resize(size_t(w) * size_t(h) * 4);
		const aiTexel* src = tex->pcData;
		for (int i = 0; i < w * h; ++i)
		{
			rgba[i * 4 + 0] = src[i].r;
			rgba[i * 4 + 1] = src[i].g;
			rgba[i * 4 + 2] = src[i].b;
			rgba[i * 4 + 3] = src[i].a;
		}
		FillImageRGBA(outImg, w, h, rgba.data());
		return true;
	}
}

static bool TryLoadMaterialTexture(
	const aiScene* scene,
	const aiMaterial* mat,
	aiTextureType type,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (!mat) return false;

	aiString path;
	if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) return false;

	std::string p = path.C_Str();
	if (p.empty()) return false;

	// Embedded?
	if (p[0] == '*')
	{
		return LoadEmbeddedTexture(scene, p, outImg);
	}
	// External
	return LoadExternalTexture(modelDir, p, outImg);
}

// Many assets put normal map in aiTextureType_HEIGHT. Try NORMALS first, then HEIGHT.
static bool TryLoadNormalLike(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_NORMALS, modelDir, outImg)) return true;
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_HEIGHT, modelDir, outImg)) return true;
	return false;
}

// Metallic & Roughness can be stored in aiTextureType_METALNESS / aiTextureType_DIFFUSE_ROUGHNESS.
// Some exporters put PBR maps under aiTextureType_UNKNOWN too; you could add fallback if needed.
static bool TryLoadMetallic(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_METALNESS, modelDir, outImg)) return true;
	// Optional: UNKNOWN fallback or combined ORM parsing could go here
	return false;
}

static bool TryLoadRoughness(
	const aiScene* scene,
	const aiMaterial* mat,
	const std::filesystem::path& modelDir,
	Image& outImg)
{
	if (TryLoadMaterialTexture(scene, mat, aiTextureType_DIFFUSE_ROUGHNESS, modelDir, outImg)) return true;
	return false;
}


// -----------------------------
// StaticMesh::LoadFromFile
// -----------------------------
bool StaticMesh::LoadFromFile(const char* filename, float scale)
{
	if (!filename || !*filename)
	{
		std::fprintf(stderr, "[StaticMesh] LoadFromFile: invalid filename\n");
		return false;
	}

	Assimp::Importer importer;

	// 기본 파이프라인: 삼각형화, 중복 정점 머지, (필요시) UV flip 등
	const unsigned flags =
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType; // 안전한 정리

	const aiScene* scene = importer.ReadFile(filename, flags);
	if (!scene || !scene->HasMeshes())
	{
		std::fprintf(stderr, "[StaticMesh] Assimp failed: %s\n", importer.GetErrorString());
		return false;
	}

	// 정점/면 수 합산 및 16bit 인덱스 가드
	size_t totalVerts = 0;
	size_t totalFaces = 0;
	for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
	{
		const aiMesh* m = scene->mMeshes[mi];
		totalVerts += m->mNumVertices;
		totalFaces += m->mNumFaces; // triangles
	}
	if (totalVerts > std::numeric_limits<uint16_t>::max())
	{
		std::fprintf(stderr, "[StaticMesh] Too many vertices for uint16 indices: %zu\n", totalVerts);
		return false;
	}

	Positions.resize(totalVerts);
	Normals.clear();
	Tangents.clear();
	UVs.clear();
	Colors.clear();
	Sections.clear();

	bool anyNormals = false;
	bool anyTangents = false;
	bool anyUVs = false;
	bool anyColors = false;

	Bounds totalBounds;

	const std::filesystem::path modelPath(filename);
	const std::filesystem::path modelDir = modelPath.parent_path();

	uint16_t base = 0;
	Sections.reserve(scene->mNumMeshes);

	// 미리 normals/tangents/uv/colors 벡터 사이즈는 “필요해지면” 한번만 잡는다
	auto ensureNormals = [&]() { if (!anyNormals) { Normals.resize(totalVerts);  anyNormals = true; } };
	auto ensureTangents = [&]() { if (!anyTangents) { Tangents.resize(totalVerts); anyTangents = true; } };
	auto ensureUVs = [&]() { if (!anyUVs) { UVs.resize(totalVerts);      anyUVs = true; } };
	auto ensureColors = [&]() { if (!anyColors) { Colors.resize(totalVerts);   anyColors = true; } };

	for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
	{
		const aiMesh* m = scene->mMeshes[mi];
		const uint32_t nv = m->mNumVertices;
		const uint32_t nf = m->mNumFaces;

		// Positions (+ AABB)
		for (uint32_t i = 0; i < nv; ++i)
		{
			const aiVector3D& p = m->mVertices[i];
			Positions[base + i] = FVector3(p.x * scale, p.y * scale, p.z * scale);
			totalBounds.Encapsulate(Positions[base + i]);
		}

		// Normals
		if (m->HasNormals())
		{
			ensureNormals();
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiVector3D& n = m->mNormals[i];
				Normals[base + i] = FVector3(n.x, n.y, n.z);
			}
		}
		// Tangents
		if (m->HasTangentsAndBitangents())
		{
			ensureTangents();
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiVector3D& t = m->mTangents[i];
				Tangents[base + i] = FVector3(t.x, t.y, t.z);
			}
		}
		// UV0
		if (m->HasTextureCoords(0))
		{
			ensureUVs();
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiVector3D& uvw = m->mTextureCoords[0][i];
				UVs[base + i] = FVector2(uvw.x, uvw.y);
			}
		}
		// Vertex colors (0)
		if (m->HasVertexColors(0))
		{
			ensureColors();
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiColor4D& c = m->mColors[0][i];
				Colors[base + i] = FVector3(c.r, c.g, c.b); // A는 무시
			}
		}

		// Create section (indices + material)
		MeshSection sec = {};
		sec.Indices.resize(size_t(nf) * 3u);

		sec.LocalBounds.Min = FVector3::FMaxValue();
		sec.LocalBounds.Max = FVector3::FMinValue();

		for (uint32_t fi = 0; fi < nf; ++fi)
		{
			const aiFace& f = m->mFaces[fi]; // Triangulate → 3
			const uint32_t w = fi * 3u;

			uint16_t i0 = static_cast<uint16_t>(base + f.mIndices[0]);
			uint16_t i1 = static_cast<uint16_t>(base + f.mIndices[1]);
			uint16_t i2 = static_cast<uint16_t>(base + f.mIndices[2]);

			sec.Indices[w + 0] = i0;
			sec.Indices[w + 1] = i1;
			sec.Indices[w + 2] = i2;

			sec.LocalBounds.Encapsulate(Positions[i0]);
			sec.LocalBounds.Encapsulate(Positions[i1]);
			sec.LocalBounds.Encapsulate(Positions[i2]);
		}

		// Load material. if possible
		const aiMaterial* aimat = scene->mMaterials[m->mMaterialIndex];
		if (aimat)
		{
			// Diffuse
			TryLoadMaterialTexture(scene, aimat, aiTextureType_DIFFUSE, modelDir, sec.Material.Diffuse);
			// Normal (NORMALS or HEIGHT)
			TryLoadNormalLike(scene, aimat, modelDir, sec.Material.Normal);
			// Specular
			TryLoadMaterialTexture(scene, aimat, aiTextureType_SPECULAR, modelDir, sec.Material.Specular);
			// Metallic / Roughness (PBR)
			TryLoadMetallic(scene, aimat, modelDir, sec.Material.Metallic);
			TryLoadRoughness(scene, aimat, modelDir, sec.Material.Roughness);
			// 필요하면 aiTextureType_UNKNOWN 에서 ORM(OCclusionRoughnessMetallic) 같은 텍스처를 파싱하는 로직을 추가 가능
		}

		Sections.emplace_back(std::move(sec));

		base = static_cast<uint16_t>(base + nv);
	}

	MeshBounds = totalBounds;

	EndCreate(); // TODO: EndCreate 사용하지 말고 여기서 바로 처리??

	return true;
}

std::vector<Vertex> StaticMesh::GetVertexArray() const
{
	size_t numVertices = Positions.size();
	std::vector<Vertex> out(numVertices);

	bool bHasNormal = !Normals.empty();
	bool bHasTexCoord = !UVs.empty();
	bool bHasTangent = !Tangents.empty();
	bool bHasColor = !Colors.empty();
	for (size_t i = 0; i < numVertices; i++)
	{
		out[i].Position = Positions[i];
		out[i].Normal = bHasNormal ? Normals[i] : FLOAT3(0.0f, 0.0f, 0.0f);
		out[i].TexCoord = bHasTexCoord ? UVs[i] : FLOAT2(0.0f, 0.0f);
		out[i].Tangent = bHasTangent ? Tangents[i] : FLOAT3(0.0f, 0.0f, 0.0f);
		// out[i].Color = bHasColor ? Colors[i] : FLOAT3(1.0f, 1.0f, 1.0f); // TODO: Vertex에 Color 추가
	}

	return out;
}

// Predefined mesh creation static functions
StaticMesh StaticMesh::CreateUnitCubeMesh()
{
	return CreateBoxMesh(1.0f, 1.0f, 1.0f);
}

StaticMesh StaticMesh::CreateBoxMesh(float width, float height, float depth)
{
	StaticMesh m;

	const float ex = width * 0.5f;
	const float ey = height * 0.5f;
	const float ez = depth * 0.5f;

	const int NV = 24;
	std::vector<FVector3> positions(NV);
	std::vector<FVector3> normals(NV);
	std::vector<FVector3> tangents(NV);
	std::vector<FVector2> uvs(NV);

	// +Z (Front)  N=(0,0,+1), T=(+1,0,0)
	positions[0] = { -ex,+ey,+ez }; uvs[0] = { 0,0 }; normals[0] = { 0,0,+1 }; tangents[0] = { +1,0,0 };
	positions[1] = { -ex,-ey,+ez }; uvs[1] = { 0,1 }; normals[1] = { 0,0,+1 }; tangents[1] = { +1,0,0 };
	positions[2] = { +ex,-ey,+ez }; uvs[2] = { 1,1 }; normals[2] = { 0,0,+1 }; tangents[2] = { +1,0,0 };
	positions[3] = { +ex,+ey,+ez }; uvs[3] = { 1,0 }; normals[3] = { 0,0,+1 }; tangents[3] = { +1,0,0 };
	// -Z (Back)   N=(0,0,-1), T=(-1,0,0)
	positions[4] = { +ex,+ey,-ez }; uvs[4] = { 0,0 }; normals[4] = { 0,0,-1 }; tangents[4] = { -1,0,0 };
	positions[5] = { +ex,-ey,-ez }; uvs[5] = { 0,1 }; normals[5] = { 0,0,-1 }; tangents[5] = { -1,0,0 };
	positions[6] = { -ex,-ey,-ez }; uvs[6] = { 1,1 }; normals[6] = { 0,0,-1 }; tangents[6] = { -1,0,0 };
	positions[7] = { -ex,+ey,-ez }; uvs[7] = { 1,0 }; normals[7] = { 0,0,-1 }; tangents[7] = { -1,0,0 };
	// -X (Left)   N=(-1,0,0), T=(0,0,+1)
	positions[8] = { -ex,+ey,-ez }; uvs[8] = { 0,0 }; normals[8] = { -1,0,0 }; tangents[8] = { 0,0,+1 };
	positions[9] = { -ex,-ey,-ez }; uvs[9] = { 0,1 }; normals[9] = { -1,0,0 }; tangents[9] = { 0,0,+1 };
	positions[10] = { -ex,-ey,+ez }; uvs[10] = { 1,1 }; normals[10] = { -1,0,0 }; tangents[10] = { 0,0,+1 };
	positions[11] = { -ex,+ey,+ez }; uvs[11] = { 1,0 }; normals[11] = { -1,0,0 }; tangents[11] = { 0,0,+1 };
	// +X (Right)  N=(+1,0,0), T=(0,0,-1)
	positions[12] = { +ex,+ey,+ez }; uvs[12] = { 0,0 }; normals[12] = { +1,0,0 }; tangents[12] = { 0,0,-1 };
	positions[13] = { +ex,-ey,+ez }; uvs[13] = { 0,1 }; normals[13] = { +1,0,0 }; tangents[13] = { 0,0,-1 };
	positions[14] = { +ex,-ey,-ez }; uvs[14] = { 1,1 }; normals[14] = { +1,0,0 }; tangents[14] = { 0,0,-1 };
	positions[15] = { +ex,+ey,-ez }; uvs[15] = { 1,0 }; normals[15] = { +1,0,0 }; tangents[15] = { 0,0,-1 };
	// +Y (Top)    N=(0,+1,0), T=(+1,0,0)
	positions[16] = { -ex,+ey,-ez }; uvs[16] = { 0,0 }; normals[16] = { 0,+1,0 }; tangents[16] = { +1,0,0 };
	positions[17] = { -ex,+ey,+ez }; uvs[17] = { 0,1 }; normals[17] = { 0,+1,0 }; tangents[17] = { +1,0,0 };
	positions[18] = { +ex,+ey,+ez }; uvs[18] = { 1,1 }; normals[18] = { 0,+1,0 }; tangents[18] = { +1,0,0 };
	positions[19] = { +ex,+ey,-ez }; uvs[19] = { 1,0 }; normals[19] = { 0,+1,0 }; tangents[19] = { +1,0,0 };
	// -Y (Bottom) N=(0,-1,0), T=(+1,0,0)
	positions[20] = { -ex,-ey,+ez }; uvs[20] = { 0,0 }; normals[20] = { 0,-1,0 }; tangents[20] = { +1,0,0 };
	positions[21] = { -ex,-ey,-ez }; uvs[21] = { 0,1 }; normals[21] = { 0,-1,0 }; tangents[21] = { +1,0,0 };
	positions[22] = { +ex,-ey,-ez }; uvs[22] = { 1,1 }; normals[22] = { 0,-1,0 }; tangents[22] = { +1,0,0 };
	positions[23] = { +ex,-ey,+ez }; uvs[23] = { 1,0 }; normals[23] = { 0,-1,0 }; tangents[23] = { +1,0,0 };

	// 36 indices
	std::vector<uint16_t> indices(36);
	// Front
	indices[0] = 0;  indices[1] = 1;  indices[2] = 2;
	indices[3] = 0;  indices[4] = 2;  indices[5] = 3;
	// Back
	indices[6] = 4;  indices[7] = 5;  indices[8] = 6;
	indices[9] = 4;  indices[10] = 6;  indices[11] = 7;
	// Left
	indices[12] = 8;  indices[13] = 9;  indices[14] = 10;
	indices[15] = 8;  indices[16] = 10; indices[17] = 11;
	// Right
	indices[18] = 12; indices[19] = 13; indices[20] = 14;
	indices[21] = 12; indices[22] = 14; indices[23] = 15;
	// Top
	indices[24] = 16; indices[25] = 17; indices[26] = 18;
	indices[27] = 16; indices[28] = 18; indices[29] = 19;
	// Bottom
	indices[30] = 20; indices[31] = 21; indices[32] = 22;
	indices[33] = 20; indices[34] = 22; indices[35] = 23;

	m.BeginCreate(positions, normals, tangents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}

StaticMesh StaticMesh::CreateSphereMesh(float radius, int segments, int rings)
{
	StaticMesh m;
	segments = std::max(3, segments);
	rings = std::max(2, rings);

	const int ncols = segments + 1;
	const int nrows = rings + 1;
	const int NV = ncols * nrows;

	std::vector<FVector3> positions(NV);
	std::vector<FVector3> normals(NV);
	std::vector<FVector3> tangents(NV);
	std::vector<FVector2> uvs(NV);

	for (int r = 0; r < nrows; ++r)
	{
		float v = float(r) / float(rings);
		float theta = v * PI;            // [0..π]
		float ct = std::cos(theta);
		float st = std::sin(theta);
		for (int c = 0; c < ncols; ++c)
		{
			float u = float(c) / float(segments);
			float phi = u * TWO_PI;      // [0..2π]
			float cp = std::cos(phi);
			float sp = std::sin(phi);

			int i = r * ncols + c;
			positions[i] = { radius * st * cp, radius * ct, radius * st * sp };
			uvs[i] = { u, v };
			normals[i] = { st * cp, ct, st * sp };
			tangents[i] = { -sp, 0.0f, +cp };
		}
	}

	const int I = segments * rings * 6;
	std::vector<uint16_t> indices(I);
	int w = 0;
	for (int r = 0; r < rings; ++r)
	{
		for (int c = 0; c < segments; ++c)
		{
			uint16_t i0 = uint16_t(r * ncols + c);
			uint16_t i1 = uint16_t(r * ncols + (c + 1));
			uint16_t i2 = uint16_t((r + 1) * ncols + (c + 1));
			uint16_t i3 = uint16_t((r + 1) * ncols + c);
			indices[w++] = i0; indices[w++] = i1; indices[w++] = i2;
			indices[w++] = i0; indices[w++] = i2; indices[w++] = i3;
		}
	}

	m.BeginCreate(positions, normals, tangents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}

StaticMesh StaticMesh::CreateGridMesh(float width, float height, int rows, int columns)
{
	StaticMesh m;
	rows = std::max(1, rows);
	columns = std::max(1, columns);

	const int nx = columns + 1;
	const int nz = rows + 1;
	const int NV = nx * nz;

	std::vector<FVector3> positions(NV);
	std::vector<FVector3> normals(NV);
	std::vector<FVector3> tangents(NV);
	std::vector<FVector2> uvs(NV);

	const float halfW = width * 0.5f;
	const float halfH = height * 0.5f;

	for (int z = 0; z < nz; ++z)
	{
		float vz = float(z) / float(rows);
		float pz = -halfH + vz * height;
		for (int x = 0; x < nx; ++x)
		{
			float ux = float(x) / float(columns);
			float px = -halfW + ux * width;

			int i = z * nx + x;
			positions[i] = { px, 0.0f, pz };
			uvs[i] = { float(x), float(z) };
			tangents[i] = { 1,0,0 };
			normals[i] = { 0,1,0 };
		}
	}

	const int I = rows * columns * 6;
	std::vector<uint16_t> indices(I);
	int w = 0;
	for (int z = 0; z < rows; ++z)
	{
		for (int x = 0; x < columns; ++x)
		{
			uint16_t i0 = uint16_t(z * nx + x);
			uint16_t i1 = uint16_t(z * nx + (x + 1));
			uint16_t i2 = uint16_t((z + 1) * nx + (x + 1));
			uint16_t i3 = uint16_t((z + 1) * nx + x);

			indices[w++] = i0; indices[w++] = i2; indices[w++] = i1;
			indices[w++] = i0; indices[w++] = i3; indices[w++] = i2;
		}
	}

	m.BeginCreate(positions, normals, tangents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}

StaticMesh StaticMesh::CreateCylinderMesh(float radius, float height, int segments)
{
	StaticMesh m;
	segments = std::max(3, segments);

	const float halfH = height * 0.5f;
	const int rows = 2;                 // side rows
	const int cols = segments + 1;      // side cols (wrap)
	const int sideV = rows * cols;
	const int topCenterIdx = sideV;
	const int bottomCenterIdx = sideV + 1;
	const int topRingStart = sideV + 2;      // (segments+1)
	const int bottomRingStart = topRingStart + (segments + 1);

	const int NV = sideV + 2 + (segments + 1) * 2;

	std::vector<FVector3> positions(NV);
	std::vector<FVector3> normals(NV);
	std::vector<FVector3> tangeents(NV);
	std::vector<FVector2> uvs(NV);

	// Side vertices
	for (int y = 0; y < rows; ++y)
	{
		float vy = float(y);
		float py = -halfH + vy * height;
		for (int s = 0; s < cols; ++s)
		{
			float u = float(s) / float(segments);
			float phi = u * TWO_PI;
			float cp = std::cos(phi);
			float sp = std::sin(phi);

			int i = y * cols + s;
			positions[i] = { radius * cp, py, radius * sp };
			uvs[i] = { u, 1.0f - vy };
			tangeents[i] = { -sp, 0.0f, +cp };
			normals[i] = { cp, 0.0f,  sp };
		}
	}

	// Centers
	positions[topCenterIdx] = { 0, +halfH, 0 };
	uvs[topCenterIdx] = { 0.5f, 0.0f };
	normals[topCenterIdx] = { 0,+1,0 };
	tangeents[topCenterIdx] = { 1,0,0 };

	positions[bottomCenterIdx] = { 0, -halfH, 0 };
	uvs[bottomCenterIdx] = { 0.5f, 1.0f };
	normals[bottomCenterIdx] = { 0,-1,0 };
	tangeents[bottomCenterIdx] = { 1,0,0 };

	// Top ring
	for (int s = 0; s <= segments; ++s)
	{
		float u = float(s) / float(segments);
		float phi = u * TWO_PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		int i = topRingStart + s;
		positions[i] = { radius * cp, +halfH, radius * sp };
		uvs[i] = { 0.5f + 0.5f * cp, 0.5f - 0.5f * sp };
		normals[i] = { 0,+1,0 };
		tangeents[i] = { 1,0,0 };
	}

	// Bottom ring
	for (int s = 0; s <= segments; ++s)
	{
		float u = float(s) / float(segments);
		float phi = u * TWO_PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		int i = bottomRingStart + s;
		positions[i] = { radius * cp, -halfH, radius * sp };
		uvs[i] = { 0.5f + 0.5f * cp, 0.5f + 0.5f * sp };
		normals[i] = { 0,-1,0 };
		tangeents[i] = { 1,0,0 };
	}

	// Indices: sides + top fan + bottom fan
	const int sideI = segments * 6;
	const int topI = segments * 3;
	const int bottomI = segments * 3;
	std::vector<uint16_t> indices(sideI + topI + bottomI);

	int w = 0;
	// Sides
	for (int s = 0; s < segments; ++s)
	{
		uint16_t i0 = uint16_t(0 * cols + s);
		uint16_t i1 = uint16_t(0 * cols + (s + 1));
		uint16_t i2 = uint16_t(1 * cols + (s + 1));
		uint16_t i3 = uint16_t(1 * cols + s);
		indices[w++] = i0; indices[w++] = i2; indices[w++] = i1;
		indices[w++] = i0; indices[w++] = i3; indices[w++] = i2;
	}
	// Top fan
	for (int s = 0; s < segments; ++s)
	{
		uint16_t curr = uint16_t(topRingStart + s);
		uint16_t next = uint16_t(topRingStart + ((s + 1) % (segments + 1)));
		indices[w++] = uint16_t(topCenterIdx);
		indices[w++] = next;
		indices[w++] = curr;
	}
	// Bottom fan
	for (int s = 0; s < segments; ++s)
	{
		uint16_t curr = uint16_t(bottomRingStart + s);
		uint16_t next = uint16_t(bottomRingStart + ((s + 1) % (segments + 1)));
		indices[w++] = uint16_t(bottomCenterIdx);
		indices[w++] = curr;
		indices[w++] = next;
	}

	m.BeginCreate(positions, normals, tangeents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}

StaticMesh StaticMesh::CreateConeMesh(float radius, float height, int segments)
{
	StaticMesh m;
	segments = std::max(3, segments);

	const float halfH = height * 0.5f;
	const int sideCols = segments + 1;

	const int baseRingStart = 0;                    // side base ring
	const int apexStart = baseRingStart + sideCols; // duplicated apex per segment
	const int bottomCenter = apexStart + sideCols;
	const int bottomRing = bottomCenter + 1;        // (segments+1)
	const int NV = sideCols + sideCols + 1 + (segments + 1);

	std::vector<FVector3> positions(NV);
	std::vector<FVector3> normals(NV);
	std::vector<FVector3> tangents(NV);
	std::vector<FVector2> uvs(NV);

	// Base ring (side)
	for (int s = 0; s < sideCols; ++s)
	{
		float u = float(s) / float(segments);
		float phi = u * TWO_PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		int i = baseRingStart + s;
		positions[i] = { radius * cp, -halfH, radius * sp };
		uvs[i] = { u, 1.0f };

		FVector3 N = FVector3::Normalize({ cp, radius / height, sp });
		normals[i] = N;
		tangents[i] = { -sp, 0.0f, +cp };
	}

	// Apex (duplicated for each segment for proper UV seam)
	for (int s = 0; s < sideCols; ++s)
	{
		float u = float(s) / float(segments);
		int i = apexStart + s;
		positions[i] = { 0, +halfH, 0 };
		uvs[i] = { u, 0 };
		normals[i] = normals[baseRingStart + s]; // same as adjacent base ring vertex
		tangents[i] = { 1,0,0 };
	}

	// Bottom center
	positions[bottomCenter] = { 0, -halfH, 0 };
	uvs[bottomCenter] = { 0.5f, 0.5f };
	normals[bottomCenter] = { 0,-1,0 };
	tangents[bottomCenter] = { 1,0,0 };

	// Bottom ring (for cap)
	for (int s = 0; s <= segments; ++s)
	{
		float u = float(s) / float(segments);
		float phi = u * TWO_PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		int i = bottomRing + s;
		positions[i] = { radius * cp, -halfH, radius * sp };
		uvs[i] = { 0.5f + 0.5f * cp, 0.5f + 0.5f * sp };
		normals[i] = { 0,-1,0 };
		tangents[i] = { 1,0,0 };
	}

	// Indices: side(2*segments tris) + bottom(segments tris)
	const int sideI = segments * 6;
	const int bottomI = segments * 3;
	std::vector<uint16_t> indices(sideI + bottomI);

	int w = 0;
	// Sides
	for (int s = 0; s < segments; ++s)
	{
		uint16_t i0 = uint16_t(baseRingStart + s);
		uint16_t i1 = uint16_t(baseRingStart + (s + 1));
		uint16_t i2 = uint16_t(apexStart + (s + 1));
		uint16_t i3 = uint16_t(apexStart + s);
		indices[w++] = i0; indices[w++] = i2; indices[w++] = i1;
		indices[w++] = i0; indices[w++] = i3; indices[w++] = i2;
	}
	// Bottom cap (fan)
	for (int s = 0; s < segments; ++s) {
		uint16_t i0 = uint16_t(bottomCenter);
		uint16_t i1 = uint16_t(bottomRing + s);
		uint16_t i2 = uint16_t(bottomRing + (s + 1));
		indices[w++] = i0; indices[w++] = i1; indices[w++] = i2;
	}

	m.BeginCreate(positions, normals, tangents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}

StaticMesh StaticMesh::CreatePlaneMesh(float width, float height)
{
	StaticMesh m;

	const float halfW = width * 0.5f;
	const float halfH = height * 0.5f;

	std::vector<FVector3> positions(4);
	std::vector<FVector3> normals(4);
	std::vector<FVector3> tangents(4);
	std::vector<FVector2> uvs(4);

	positions[0] = { -halfW, 0, -halfH }; uvs[0] = { 0,0 };
	positions[1] = { -halfW, 0, +halfH }; uvs[1] = { 0,1 };
	positions[2] = { +halfW, 0, +halfH }; uvs[2] = { 1,1 };
	positions[3] = { +halfW, 0, -halfH }; uvs[3] = { 1,0 };
	for (int i = 0; i < 4; ++i) { normals[i] = { 0,1,0 }; tangents[i] = { 1,0,0 }; }

	std::vector<uint16_t> indices(6);
	indices[0] = 0; indices[1] = 1; indices[2] = 2;
	indices[3] = 0; indices[4] = 2; indices[5] = 3;

	m.BeginCreate(positions, normals, tangents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}