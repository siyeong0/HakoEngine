#include "pch.h"
#include "Common/Vertex.h"
#include "Geometry.h"

// --------------------------------------------------
// Unknown methods
// --------------------------------------------------

STDMETHODIMP Geometry::QueryInterface(REFIID refiid, void** ppv)
{
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) Geometry::AddRef()
{
	m_RefCount++;
	return m_RefCount;
}

STDMETHODIMP_(ULONG) Geometry::Release()
{
	DWORD ref_count = --m_RefCount;
	if (!m_RefCount)
		delete this;
	return ref_count;
}

// --------------------------------------------------
// IGeometry methods
// --------------------------------------------------

bool ENGINECALL Geometry::Initialize()
{
	return true;
}

void ENGINECALL Geometry::Cleanup()
{
}

MeshData ENGINECALL Geometry::CreateUnitCubeMesh()
{
	return CreateBoxMesh(1.0f, 1.0f, 1.0f);
}

MeshData ENGINECALL Geometry::CreateBoxMesh(float width, float height, float depth)
{
	MeshData out{};
	auto& outVertices = out.Vertices;
	auto& outIndices = out.Indices;

	outVertices.clear();
	outIndices.clear();
	outVertices.reserve(24);
	outIndices.reserve(36);

	const float ex = width * 0.5f;  // half extent x
	const float ey = height * 0.5f; // half extent y
	const float ez = depth * 0.5f;  // half extent z

	const FLOAT2 uv00{ 0.0f, 0.0f };
	const FLOAT2 uv10{ 1.0f, 0.0f };
	const FLOAT2 uv11{ 1.0f, 1.0f };
	const FLOAT2 uv01{ 0.0f, 1.0f };

	// +Z (Front)
	outVertices.emplace_back(Vertex{ {-ex, +ey, +ez}, uv00, {+1, 0, 0} }); // TL
	outVertices.emplace_back(Vertex{ {-ex, -ey, +ez}, uv01, {+1, 0, 0} }); // BL
	outVertices.emplace_back(Vertex{ {+ex, -ey, +ez}, uv11, {+1, 0, 0} }); // BR
	outVertices.emplace_back(Vertex{ {+ex, +ey, +ez}, uv10, {+1, 0, 0} }); // TR
	// -Z (Back)
	outVertices.emplace_back(Vertex{ {+ex, +ey, -ez}, uv00, {-1, 0, 0} }); // TL
	outVertices.emplace_back(Vertex{ {+ex, -ey, -ez}, uv01, {-1, 0, 0} }); // BL
	outVertices.emplace_back(Vertex{ {-ex, -ey, -ez}, uv11, {-1, 0, 0} }); // BR
	outVertices.emplace_back(Vertex{ {-ex, +ey, -ez}, uv10, {-1, 0, 0} }); // TR
	// -X (Left)
	outVertices.emplace_back(Vertex{ {-ex, +ey, -ez}, uv00, {0, 0, +1} }); // TL
	outVertices.emplace_back(Vertex{ {-ex, -ey, -ez}, uv01, {0, 0, +1} }); // BL
	outVertices.emplace_back(Vertex{ {-ex, -ey, +ez}, uv11, {0, 0, +1} }); // BR
	outVertices.emplace_back(Vertex{ {-ex, +ey, +ez}, uv10, {0, 0, +1} }); // TR
	// +X (Right)
	outVertices.emplace_back(Vertex{ {+ex, +ey, +ez}, uv00, {0, 0, -1} }); // TL
	outVertices.emplace_back(Vertex{ {+ex, -ey, +ez}, uv01, {0, 0, -1} }); // BL
	outVertices.emplace_back(Vertex{ {+ex, -ey, -ez}, uv11, {0, 0, -1} }); // BR
	outVertices.emplace_back(Vertex{ {+ex, +ey, -ez}, uv10, {0, 0, -1} }); // TR
	// +Y (Top)
	outVertices.emplace_back(Vertex{ {-ex, +ey, -ez}, uv00, {+1, 0, 0} }); // TL
	outVertices.emplace_back(Vertex{ {-ex, +ey, +ez}, uv01, {+1, 0, 0} }); // BL
	outVertices.emplace_back(Vertex{ {+ex, +ey, +ez}, uv11, {+1, 0, 0} }); // BR
	outVertices.emplace_back(Vertex{ {+ex, +ey, -ez}, uv10, {+1, 0, 0} }); // TR
	// -Y (Bottom)
	outVertices.emplace_back(Vertex{ {-ex, -ey, +ez}, uv00, {+1, 0, 0} }); // TL
	outVertices.emplace_back(Vertex{ {-ex, -ey, -ez}, uv01, {+1, 0, 0} }); // BL
	outVertices.emplace_back(Vertex{ {+ex, -ey, -ez}, uv11, {+1, 0, 0} }); // BR
	outVertices.emplace_back(Vertex{ {+ex, -ey, +ez}, uv10, {+1, 0, 0} }); // TR

	for (int i = 0; i < 6; ++i)
	{
		uint16_t base = static_cast<uint16_t>(i * 4);
		outIndices.emplace_back(base + 0);
		outIndices.emplace_back(base + 1);
		outIndices.emplace_back(base + 2);
		outIndices.emplace_back(base + 0);
		outIndices.emplace_back(base + 2);
		outIndices.emplace_back(base + 3);
	}

	return out;
}

MeshData ENGINECALL Geometry::CreateSphereMesh(float radius, int segments, int rings)
{
	MeshData out{};
	auto& outVertices = out.Vertices;
	auto& outIndices = out.Indices;

	segments = std::max(3, segments); // Longitude
	rings = std::max(2, rings);    // Latitude

	const int vertCols = segments + 1;
	const int vertRows = rings + 1;

	outVertices.clear(); outIndices.clear();
	outVertices.reserve(vertCols * vertRows);
	outIndices.reserve(segments * rings * 6);

	// Generate vertices
	for (int r = 0; r < vertRows; ++r)
	{
		float v = (float)r / (float)rings;
		float theta = v * DirectX::XM_PI;        // [0..π]
		float ct = std::cos(theta);
		float st = std::sin(theta);

		for (int c = 0; c < vertCols; ++c)
		{
			float u = (float)c / (float)segments;
			float phi = u * DirectX::XM_2PI;     // [0..2π]
			float cp = std::cos(phi);
			float sp = std::sin(phi);

			FLOAT3 pos = { radius * st * cp, radius * ct, radius * st * sp };
			FLOAT2 uv = { u, v };
			FLOAT3 tan = { -sp, 0.0f, +cp }; // Tangent along longitude

			outVertices.push_back(Vertex{ pos, uv, tan });
		}
	}

	// Generate indices
	for (int r = 0; r < rings; ++r)
	{
		for (int c = 0; c < segments; ++c)
		{
			uint16_t i0 = (uint16_t)(r * vertCols + c);
			uint16_t i1 = (uint16_t)(r * vertCols + (c + 1));
			uint16_t i2 = (uint16_t)((r + 1) * vertCols + (c + 1));
			uint16_t i3 = (uint16_t)((r + 1) * vertCols + c);

			outIndices.push_back(i0); outIndices.push_back(i1); outIndices.push_back(i2);
			outIndices.push_back(i0); outIndices.push_back(i2); outIndices.push_back(i3);
		}
	}

	return out;
}

MeshData ENGINECALL Geometry::CreateGridMesh(float width, float height, int rows, int columns)
{
	MeshData out{};
	auto& outVertices = out.Vertices;
	auto& outIndices = out.Indices;

	rows = std::max(1, rows);
	columns = std::max(1, columns);

	const int nx = columns + 1;
	const int nz = rows + 1;

	outVertices.clear(); outIndices.clear();
	outVertices.reserve(nx * nz);
	outIndices.reserve(rows * columns * 6);

	const float halfW = width * 0.5f;
	const float halfH = height * 0.5f;

	// Generate vertices on XZ plane
	for (int z = 0; z < nz; ++z)
	{
		float vz = (float)z / (float)rows;
		float pz = -halfH + vz * height;

		for (int x = 0; x < nx; ++x)
		{
			float ux = (float)x / (float)columns;
			float px = -halfW + ux * width;

			FLOAT3 pos = { px, 0.0f, pz };
			FLOAT2 uv = { (float)x, (float)z };
			FLOAT3 tan = { 1.0f, 0.0f, 0.0f };

			outVertices.emplace_back(Vertex{ pos, uv, tan });
		}
	}

	// Generate indices
	for (int z = 0; z < rows; ++z)
	{
		for (int x = 0; x < columns; ++x)
		{
			uint16_t i0 = (uint16_t)(z * nx + x);
			uint16_t i1 = (uint16_t)(z * nx + (x + 1));
			uint16_t i2 = (uint16_t)((z + 1) * nx + (x + 1));
			uint16_t i3 = (uint16_t)((z + 1) * nx + x);

			outIndices.emplace_back(i0); outIndices.emplace_back(i2); outIndices.emplace_back(i1);
			outIndices.emplace_back(i0); outIndices.emplace_back(i3); outIndices.emplace_back(i2);
		}
	}

	return out;
}

MeshData ENGINECALL Geometry::CreateCylinderMesh(float radius, float height, int segments)
{
	MeshData out{};
	auto& outVertices = out.Vertices;
	auto& outIndices = out.Indices;

	segments = std::max(3, segments);

	outVertices.clear(); outIndices.clear();

	const float halfH = height * 0.5f;
	const int sideRows = 2;
	const int sideCols = segments + 1;

	// Side vertices
	for (int yrow = 0; yrow < sideRows; ++yrow)
	{
		float vy = (float)yrow;
		float py = -halfH + vy * height;

		for (int s = 0; s < sideCols; ++s)
		{
			float u = (float)s / (float)segments;
			float phi = u * DirectX::XM_2PI;
			float cp = std::cos(phi);
			float sp = std::sin(phi);

			FLOAT3 pos = { radius * cp, py, radius * sp };
			FLOAT2 uv = { u, 1.0f - vy };
			FLOAT3 tan = { -sp, 0.0f, +cp };

			outVertices.emplace_back(Vertex{ pos, uv, tan });
		}
	}

	// Side indices
	for (int s = 0; s < segments; ++s)
	{
		uint16_t i0 = (uint16_t)(0 * sideCols + s);
		uint16_t i1 = (uint16_t)(0 * sideCols + (s + 1));
		uint16_t i2 = (uint16_t)(1 * sideCols + (s + 1));
		uint16_t i3 = (uint16_t)(1 * sideCols + s);

		outIndices.emplace_back(i0); outIndices.emplace_back(i2); outIndices.emplace_back(i1);
		outIndices.emplace_back(i0); outIndices.emplace_back(i3); outIndices.emplace_back(i2);
	}

	// Top and bottom caps (similar to cone version, skipped here for brevity)
	uint16_t topCenterIndex = (uint16_t)outVertices.size();
	outVertices.emplace_back(Vertex{ FLOAT3{0, +halfH, 0}, FLOAT2{0.5f, 0.0f}, FLOAT3{1,0,0} });

	uint16_t bottomCenterIndex = (uint16_t)outVertices.size();
	outVertices.emplace_back(Vertex{ FLOAT3{0, -halfH, 0}, FLOAT2{0.5f, 1.0f}, FLOAT3{1,0,0} });

	// Top ring
	for (int s = 0; s <= segments; ++s)
	{
		float u = (float)s / (float)segments;
		float phi = u * DirectX::XM_2PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		FLOAT3 pos = { radius * cp, +halfH, radius * sp };
		FLOAT2 uv = { 0.5f + 0.5f * cp, 0.5f - 0.5f * sp };
		outVertices.emplace_back(Vertex{ pos, uv, FLOAT3{1,0,0} });

		uint16_t curr = (uint16_t)(outVertices.size() - 1);
		uint16_t next = (uint16_t)(topCenterIndex + 2 + ((s + 1) % (segments + 1)));

		if (s < segments)
		{
			outIndices.emplace_back(topCenterIndex);
			outIndices.emplace_back(next);
			outIndices.emplace_back(curr);
		}
	}

	// Bottom ring
	uint16_t bottomStart = (uint16_t)(topCenterIndex + 2 + (segments + 1));
	for (int s = 0; s <= segments; ++s)
	{
		float u = (float)s / (float)segments;
		float phi = u * DirectX::XM_2PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		FLOAT3 pos = { radius * cp, -halfH, radius * sp };
		FLOAT2 uv = { 0.5f + 0.5f * cp, 0.5f + 0.5f * sp };
		outVertices.emplace_back(Vertex{ pos, uv, FLOAT3{1,0,0} });

		uint16_t curr = (uint16_t)(outVertices.size() - 1);
		uint16_t next = (uint16_t)(bottomStart + ((s + 1) % (segments + 1)));

		if (s < segments)
		{
			outIndices.emplace_back(bottomCenterIndex);
			outIndices.emplace_back(curr);
			outIndices.emplace_back(next);
		}
	}

	return out;
}


MeshData ENGINECALL Geometry::CreateConeMesh(float radius, float height, int segments)
{
	MeshData out{};
	auto& outVertices = out.Vertices;
	auto& outIndices = out.Indices;

	segments = std::max(3, segments);

	outVertices.clear(); outIndices.clear();

	const float halfH = height * 0.5f;
	const int sideCols = segments + 1;

	// Base ring
	for (int s = 0; s < sideCols; ++s)
	{
		float u = (float)s / (float)segments;
		float phi = u * DirectX::XM_2PI;
		float cp = std::cos(phi);
		float sp = std::sin(phi);

		FLOAT3 pos = { radius * cp, -halfH, radius * sp };
		FLOAT2 uv = { u, 1.0f };
		FLOAT3 tan = { -sp, 0.0f, +cp };

		outVertices.emplace_back(Vertex{ pos, uv, tan });
	}

	// Apex
	const uint16_t apexStart = (uint16_t)outVertices.size();
	for (int s = 0; s < sideCols; ++s)
	{
		float u = (float)s / (float)segments;
		outVertices.emplace_back(Vertex{ FLOAT3{0, +halfH, 0}, FLOAT2{u, 0}, FLOAT3{1,0,0} });
	}

	// Side indices
	for (int s = 0; s < segments; ++s)
	{
		uint16_t i0 = (uint16_t)(0 + s);
		uint16_t i1 = (uint16_t)(0 + (s + 1));
		uint16_t i2 = (uint16_t)(apexStart + (s + 1));
		uint16_t i3 = (uint16_t)(apexStart + s);

		outIndices.emplace_back(i0); outIndices.emplace_back(i2); outIndices.emplace_back(i1);
		outIndices.emplace_back(i0); outIndices.emplace_back(i3); outIndices.emplace_back(i2);
	}

	// Bottom cap similar to cylinder
	uint16_t bottomCenterIndex = (uint16_t)outVertices.size();
	outVertices.emplace_back(Vertex{ FLOAT3{0, -halfH, 0}, FLOAT2{0.5f, 0.5f}, FLOAT3{1,0,0} });

	uint16_t baseStart = 0;
	for (int s = 0; s < segments; ++s)
	{
		uint16_t i0 = bottomCenterIndex;
		uint16_t i1 = (uint16_t)(baseStart + s);
		uint16_t i2 = (uint16_t)(baseStart + (s + 1));

		outIndices.emplace_back(i0);
		outIndices.emplace_back(i1);
		outIndices.emplace_back(i2);
	}

	return out;
}

MeshData ENGINECALL Geometry::CreatePlaneMesh(float width, float height)
{
	MeshData out{};
	auto& outVertices = out.Vertices;
	auto& outIndices = out.Indices;

	outVertices.clear(); outIndices.clear();
	outVertices.reserve(4);
	outIndices.reserve(6);

	const float halfW = width * 0.5f;
	const float halfH = height * 0.5f;

	// XZ plane, Y=0
	outVertices.emplace_back(Vertex{ FLOAT3{-halfW, 0, -halfH}, FLOAT2{0,0}, FLOAT3{1,0,0} });
	outVertices.emplace_back(Vertex{ FLOAT3{-halfW, 0, +halfH}, FLOAT2{0,1}, FLOAT3{1,0,0} });
	outVertices.emplace_back(Vertex{ FLOAT3{+halfW, 0, +halfH}, FLOAT2{1,1}, FLOAT3{1,0,0} });
	outVertices.emplace_back(Vertex{ FLOAT3{+halfW, 0, -halfH}, FLOAT2{1,0}, FLOAT3{1,0,0} });

	// Indices
	outIndices.emplace_back(0); outIndices.emplace_back(1); outIndices.emplace_back(2);
	outIndices.emplace_back(0); outIndices.emplace_back(2); outIndices.emplace_back(3);

	return out;
}

