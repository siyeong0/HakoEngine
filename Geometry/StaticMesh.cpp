#include "pch.h"
#include <fstream>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include "Vertex.h"
#include "Generic/Image.h"
#include "StaticMesh.h"

void UStaticMesh::BeginCreate(
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

void UStaticMesh::InsertSection(const std::vector<uint16_t> indices, const UMaterial&& material)
{
	UMeshSection section;
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

void UStaticMesh::EndCreate()
{
	// Compute normals if not provided
	if (Normals.empty())
	{
		// Compute normals from geometry
		Normals.resize(Positions.size(), FVector3(0.0f, 0.0f, 0.0f));
		for (const UMeshSection& section : Sections)
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
		for (const UMeshSection& section : Sections)
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
// StaticMesh::LoadFromFile
// -----------------------------

static std::filesystem::path dumpEmbeddedTextureToCache(const aiTexture* atex, std::wstring key);

bool UStaticMesh::LoadFromFile(const char* filename, float scale)
{
	if (!filename || !*filename)
	{
		std::fprintf(stderr, "[StaticMesh] LoadFromFile: invalid filename\n");
		return false;
	}

	Assimp::Importer importer;

	const unsigned flags =
		aiProcess_Triangulate |
		aiProcess_GenUVCoords |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType;

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
		ASSERT(m->HasNormals(), "Normals should be generated by aiProcess_GenNormals");
		if (m->HasNormals())
		{
			Normals.resize(totalVerts);
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiVector3D& n = m->mNormals[i];
				Normals[base + i] = FVector3(n.x, n.y, n.z);
			}
		}
		// UV0
		if (m->HasTextureCoords(0))
		{
			UVs.resize(totalVerts);
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiVector3D& uvw = m->mTextureCoords[0][i];
				UVs[base + i] = FVector2(uvw.x, uvw.y);
			}
		}
		else
		{
			UVs.resize(totalVerts, FVector2(0.0f, 0.0f));
		}
		// Tangents
		if (m->HasTangentsAndBitangents())
		{
			Tangents.resize(totalVerts);
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiVector3D& t = m->mTangents[i];
				Tangents[base + i] = FVector3(t.x, t.y, t.z);
			}
		}
		else
		{
			Tangents.resize(totalVerts);
			std::vector<FVector3> tan1(nv, FVector3(0, 0, 0));
			std::vector<FVector3> tan2(nv, FVector3(0, 0, 0));

			for (uint32_t fi = 0; fi < m->mNumFaces; ++fi)
			{
				const aiFace& f = m->mFaces[fi];
				if (f.mNumIndices < 3) continue;

				for (uint32_t k = 0; k + 2 < f.mNumIndices; ++k)
				{
					uint32_t i0 = f.mIndices[0];
					uint32_t i1 = f.mIndices[k + 1];
					uint32_t i2 = f.mIndices[k + 2];

					const FVector3& v0 = Positions[i0];
					const FVector3& v1 = Positions[i1];
					const FVector3& v2 = Positions[i2];

					const FVector2& w0 = UVs[i0];
					const FVector2& w1 = UVs[i1];
					const FVector2& w2 = UVs[i2];

					float x1 = v1.x - v0.x, y1 = v1.y - v0.y, z1 = v1.z - v0.z;
					float x2 = v2.x - v0.x, y2 = v2.y - v0.y, z2 = v2.z - v0.z;

					float s1 = w1.x - w0.x, t1 = w1.y - w0.y;
					float s2 = w2.x - w0.x, t2 = w2.y - w0.y;

					float denom = (s1 * t2 - s2 * t1);
					if (std::fabs(denom) < 1e-20f) continue; // 퇴화 UV → 스킵

					float r = 1.0f / denom;

					FVector3 sdir((t2 * x1 - t1 * x2) * r,
						(t2 * y1 - t1 * y2) * r,
						(t2 * z1 - t1 * z2) * r);
					FVector3 tdir((s1 * x2 - s2 * x1) * r,
						(s1 * y2 - s2 * y1) * r,
						(s1 * z2 - s2 * z1) * r);

					tan1[i0] = tan1[i0] + sdir; tan1[i1] = tan1[i1] + sdir; tan1[i2] = tan1[i2] + sdir;
					tan2[i0] = tan2[i0] + tdir; tan2[i1] = tan2[i1] + tdir; tan2[i2] = tan2[i2] + tdir;
				}
			}

			// 그람-슈미트 정규화 + 핸디드니스(원하면 w에 저장)
			for (uint32_t i = 0; i < nv; ++i)
			{
				FVector3 n(m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z);
				FVector3 t = tan1[i];

				t = t - n * FVector3::Dot(n, t);
				float len2 = FVector3::Dot(t, t);
				if (len2 < 1e-20f)
				{
					FVector3 up = (std::fabs(n.z) < 0.999f) ? FVector3{ 0,0,1 } : FVector3{ 0,1,0 };
					Tangents[base + i] = FVector3::Normalize(FVector3::Cross(up, n));
					continue;
				}
				t = t * (1.0f / std::sqrt(len2));
				Tangents[base + i] = t;
			}
		}
		// Vertex colors (0)
		if (m->HasVertexColors(0))
		{
			Colors.resize(totalVerts);
			for (uint32_t i = 0; i < nv; ++i)
			{
				const aiColor4D& c = m->mColors[0][i];
				Colors[base + i] = FVector3(c.r, c.g, c.b); // A는 무시
			}
		}

		// Create section (indices + material)
		UMeshSection sec;
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
		// Helper: resolve external/embedded texture into a real file path (uses dumpEmbeddedTextureToCache)
		auto isEmbeddedUri = [](const aiString& s) -> bool
			{
				if (s.length == 0) return false;
				const char* u = s.C_Str();
				return (u[0] == '*') || (std::strncmp(u, "data:", 5) == 0);
			};

		auto resolveTexturePath = [&](const aiMaterial* mat, aiTextureType type, std::wstring& outPathW) -> bool
			{
				static int callCount = 0;
				if (mat->GetTextureCount(type) == 0) return false;

				aiString uri;
				if (mat->GetTexture(type, 0, &uri) != aiReturn_SUCCESS) return false;

				if (isEmbeddedUri(uri))
				{
					const aiTexture* atex = scene->GetEmbeddedTexture(uri.C_Str());
					if (!atex) return false;

					std::wstring texTypeStr = L"";
					switch (type)
					{
					case aiTextureType_DIFFUSE:    texTypeStr = L"diffuse"; break;
					case aiTextureType_SPECULAR:   texTypeStr = L"specular"; break;
					case aiTextureType_NORMALS:    texTypeStr = L"normal"; break;
					case aiTextureType_HEIGHT:     texTypeStr = L"height"; break;
					case aiTextureType_EMISSIVE:   texTypeStr = L"emissive"; break;
					case aiTextureType_AMBIENT:    texTypeStr = L"ambient"; break;
					case aiTextureType_METALNESS:  texTypeStr = L"metalness"; break;
					default:                       texTypeStr = L"X"; break;
					}

					std::filesystem::path abspath = std::filesystem::absolute(std::filesystem::path(filename));
					std::wstring key = texTypeStr + L"_" + std::to_wstring(std::hash<std::filesystem::path>{}(abspath));

					std::filesystem::path dumped = dumpEmbeddedTextureToCache(atex, key);
					if (dumped.empty()) return false;

					outPathW = dumped.wstring();
					return true;
				}
				else
				{
					std::filesystem::path full = modelDir / std::filesystem::path(uri.C_Str());
					outPathW = full.wstring();
					return true;
				}
			};

		auto resolveAny = [&](std::initializer_list<aiTextureType> types, std::wstring& outPathW) -> bool
			{
				for (aiTextureType t : types)
				{
					if (resolveTexturePath(aimat, t, outPathW))
					{
						return true;
					}
				}
				return false;
			};

		// glTF2 우선 + 구형(DIFFUSE 등) 폴백
		resolveAny({ aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, sec.Material.DiffuseTexturePath);
		resolveAny({ aiTextureType_NORMALS }, sec.Material.NormalTexturePath);
		resolveAny({ aiTextureType_SPECULAR }, sec.Material.SpecularTexturePath);
		resolveAny({ aiTextureType_METALNESS }, sec.Material.MetallicTexturePath);
		resolveAny({ aiTextureType_DIFFUSE_ROUGHNESS }, sec.Material.RoughnessTexturePath);
		// 필요 시: Occlusion/Emissive 등
		// ResolveAny({ aiTextureType_LIGHTMAP /*AO*/ },                               sec.Material.AmbientOcclusionTexturePath);
		// ResolveAny({ aiTextureType_EMISSIVE },                                      sec.Material.EmissiveTexturePath);

		auto clamp01 = [](float v) { return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v); };

		// glTF property keys (Assimp 내부 키 문자열)
		static constexpr const char* K_BASECOLOR = "$mat.gltf.pbrMetallicRoughness.baseColorFactor"; // aiColor4D
		static constexpr const char* K_METALLIC = "$mat.gltf.pbrMetallicRoughness.metallicFactor";  // float
		static constexpr const char* K_ROUGHNESS = "$mat.gltf.pbrMetallicRoughness.roughnessFactor"; // float
		static constexpr const char* K_TEX_SCALE = "$mat.gltf.texture.scale";                        // float (per texture slot)
		static constexpr const char* K_TEX_STRENGTH = "$mat.gltf.texture.strength";                     // float (occlusion strength)

		auto getF = [&](const char* key, unsigned type, unsigned idx, float& out) -> bool
			{
				return aimat->Get(key, type, idx, out) == aiReturn_SUCCESS;
			};
		auto getC4 = [&](const char* key, unsigned type, unsigned idx, aiColor4D& out) -> bool
			{
				return aimat->Get(key, type, idx, out) == aiReturn_SUCCESS;
			};

		aiColor4D c4{};
		float f = 0.0f;

		// BaseColor & Opacity (glTF 우선 → 전통 키 폴백)
		if (getC4(K_BASECOLOR, /*type*/0, /*idx*/0, c4))
		{
			sec.Material.BaseColor = FLOAT3{ c4.r, c4.g, c4.b };
			sec.Material.Opacity = clamp01(c4.a);
		}
		else if (aimat->Get(AI_MATKEY_COLOR_DIFFUSE, c4) == aiReturn_SUCCESS)
		{
			sec.Material.BaseColor = FLOAT3{ c4.r, c4.g, c4.b };
			if (c4.a > 0.0f) sec.Material.Opacity = clamp01(c4.a);
		}

		if (aimat->Get(AI_MATKEY_OPACITY, f) == aiReturn_SUCCESS)
			sec.Material.Opacity = clamp01(f);
		else if (aimat->Get(AI_MATKEY_TRANSPARENCYFACTOR, f) == aiReturn_SUCCESS)
			sec.Material.Opacity = clamp01(1.0f - f);

		// MetallicFactor (glTF 키만 사용; 폴백 없음)
		if (getF(K_METALLIC, 0, 0, f))
		{
			sec.Material.MetallicFactor = clamp01(f);
		}

		// RoughnessFactor (glTF 우선, 폴백: Phong shininess→roughness 근사)
		if (getF(K_ROUGHNESS, 0, 0, f))
		{
			sec.Material.RoughnessFactor = clamp01(f);
		}
		else if (aimat->Get(AI_MATKEY_SHININESS, f) == aiReturn_SUCCESS)
		{
			float n = std::max(0.0f, f);
			float rough = std::sqrt(2.0f / (n + 2.0f));
			sec.Material.RoughnessFactor = clamp01(rough);
		}

		// SpecularColor
		if (aimat->Get(AI_MATKEY_COLOR_SPECULAR, c4) == aiReturn_SUCCESS)
		{
			sec.Material.SpecularColor = FLOAT3{ c4.r, c4.g, c4.b };
		}

		// SpecularFactor (있으면 적용: SHININESS_STRENGTH 또는 SPECULAR_FACTOR)
		if (aimat->Get(AI_MATKEY_SHININESS_STRENGTH, f) == aiReturn_SUCCESS ||
			aimat->Get(AI_MATKEY_SPECULAR_FACTOR, f) == aiReturn_SUCCESS)
		{
			sec.Material.SpecularFactor = clamp01(f);
		}

		// NormalScale (glTF normalTexture.scale)
		if (getF(K_TEX_SCALE, aiTextureType_NORMALS, 0, f))
		{
			sec.Material.NormalScale = f;
		}

		// Ambient Occlusion Strength (glTF occlusionTexture.strength → LIGHTMAP 슬롯)
		if (getF(K_TEX_STRENGTH, aiTextureType_LIGHTMAP, 0, f))
		{
			sec.Material.AmbientOcclusionStrength = clamp01(f);
		}


		Sections.emplace_back(std::move(sec));

		base = static_cast<uint16_t>(base + nv);
	}

	MeshBounds = totalBounds;

	EndCreate(); // TODO: EndCreate 사용하지 말고 여기서 바로 처리??

	return true;
}

std::vector<Vertex> UStaticMesh::GetVertexArray() const
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

void UStaticMesh::FlipYAxis()
{
	// 1) Flip geometry along Y
	for (FVector3& p : Positions)
	{
		p.y = -p.y;
	}

	// 2) Flip vertex frames (if present)
	if (!Normals.empty())
	{
		ASSERT(Normals.size() == Positions.size(), "Normals/Positions size mismatch.");
		for (FVector3& n : Normals) { n.y = -n.y; }
	}
	if (!Tangents.empty())
	{
		ASSERT(Tangents.size() == Positions.size(), "Tangents/Positions size mismatch.");
		for (FVector3& t : Tangents) { t.y = -t.y; }
	}

	// 3) Reverse triangle winding to preserve facing after mirror
	for (UMeshSection& sec : Sections)
	{
		ASSERT(sec.Indices.size() % 3 == 0, "Indices are not a multiple of 3.");
		for (size_t i = 0; i + 2 < sec.Indices.size(); i += 3)
		{
			std::swap(sec.Indices[i + 1], sec.Indices[i + 2]);
		}

		// Recompute section bounds
		sec.LocalBounds.Min = FVector3::FMaxValue();
		sec.LocalBounds.Max = FVector3::FMinValue();
		for (uint16_t idx : sec.Indices)
		{
			if (idx < Positions.size())
			{
				sec.LocalBounds.Encapsulate(Positions[idx]);
			}
		}
	}

	// 4) Recompute mesh bounds
	MeshBounds = Bounds();
	for (const FVector3& v : Positions)
	{
		MeshBounds.Encapsulate(v);
	}
}


// Predefined mesh creation static functions
UStaticMesh UStaticMesh::CreateUnitCubeMesh()
{
	return CreateBoxMesh(1.0f, 1.0f, 1.0f);
}

UStaticMesh UStaticMesh::CreateBoxMesh(float width, float height, float depth)
{
	UStaticMesh m;

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

UStaticMesh UStaticMesh::CreateSphereMesh(float radius, int segments, int rings)
{
	UStaticMesh m;
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

UStaticMesh UStaticMesh::CreateGridMesh(float width, float height, int rows, int columns)
{
	UStaticMesh m;
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

UStaticMesh UStaticMesh::CreateCylinderMesh(float radius, float height, int segments)
{
	UStaticMesh m;
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

UStaticMesh UStaticMesh::CreateConeMesh(float radius, float height, int segments)
{
	UStaticMesh m;
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

UStaticMesh UStaticMesh::CreatePlaneMesh(float width, float height)
{
	UStaticMesh m;

	const float halfW = width * 0.5f;
	const float halfH = height * 0.5f;

	std::vector<FVector3> positions(4);
	std::vector<FVector3> normals(4);
	std::vector<FVector3> tangents(4);
	std::vector<FVector2> uvs(4);

	positions[0] = { -halfW, -halfH, 0.0f }; uvs[0] = { 0,0 };
	positions[1] = { -halfW, +halfH, 0.0f }; uvs[1] = { 0,1 };
	positions[2] = { +halfW, +halfH, 0.0f }; uvs[2] = { 1,1 };
	positions[3] = { +halfW, -halfH, 0.0f }; uvs[3] = { 1,0 };
	for (int i = 0; i < 4; ++i) { normals[i] = { 0,0,-1 }; tangents[i] = { 0,1,0 }; }

	std::vector<uint16_t> indices(6);
	indices[0] = 0; indices[1] = 1; indices[2] = 2;
	indices[3] = 0; indices[4] = 2; indices[5] = 3;

	m.BeginCreate(positions, normals, tangents, uvs, {});
	m.InsertSection(indices);
	m.EndCreate();

	return m;
}

static std::filesystem::path dumpEmbeddedTextureToCache(const aiTexture* atex, std::wstring key)
{
	ASSERT(atex, "Null aiTexture");

	// Cross-platform cache dir: <temp>/HakoEngine/texcache
	std::filesystem::path cacheDir = L"./Resources/cache";
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);

	const std::wstring stem = key;

	// We always dump as dds. TODO: use png and modify renderer texture loader (platform-agnostic)
	const std::filesystem::path outPath = cacheDir / (stem + L".dds");
	if (std::filesystem::exists(outPath))
	{
		return outPath;
	}

	Image img = {};

	const void* blob = reinterpret_cast<const void*>(atex->pcData);
	const size_t sizeBytes = static_cast<size_t>(atex->mWidth); // Assimp: mWidth = byte length for blobs

	bool bLoaded = img.LoadFromMemory(blob, sizeBytes);
	ASSERT(bLoaded, "Failed to decode embedded texture blob.");

	if (!img.IsValid()) return {};
	img.FlipY(); // Flip Y to match texture coord convention

	// Save as DDS using your Image utils (stb_image_write 기반 가정)
	const bool ok = img.Save(outPath, Image::EImageFormat::DDS);
	ASSERT(ok, "Failed to write embedded texture to cache.");
	if (!ok) return {};

	return outPath;
}