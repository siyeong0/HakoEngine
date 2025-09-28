#include "VertexUtil.h"

uint32_t AddVertex(BasicVertex* pVertexList, uint32_t maxNumVertices, uint32_t* pInOutVertexCount, const BasicVertex* pVertex);

uint32_t CreateBoxMesh(BasicVertex** ppOutVertexList, uint16_t* pOutIndexList, uint32_t maxNumBuffers, float extent)
{
	const uint32_t NUM_BOX_INDICES = 36;
	ASSERT(maxNumBuffers >= NUM_BOX_INDICES, "Too many indices.");

	const uint16_t pIndexList[NUM_BOX_INDICES] =
	{
		// +z
		3, 0, 1,
		3, 1, 2,

		// -z
		4, 7, 6,
		4, 6, 5,

		// -x
		0, 4, 5,
		0, 5, 1,

		// +x
		7, 3, 2,
		7, 2, 6,

		// +y
		0, 3, 7,
		0, 7, 4,

		// -y
		2, 1, 5,
		2, 5, 6
	};
	
	TVERTEX pTexCoordList[NUM_BOX_INDICES] =
	{
		// +z
		{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
		
		// -z
		{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},

		// -x
		{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},

		// +x
		{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},

		// +y
		{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
		
		// -y
		{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
	};
	
	FLOAT3 pWorldPosList[8] = {};
	pWorldPosList[0] = { -extent, extent, extent };
	pWorldPosList[1] = { -extent, -extent, extent };
	pWorldPosList[2] = { extent, -extent, extent };
	pWorldPosList[3] = { extent, extent, extent };
	pWorldPosList[4] = { -extent, extent, -extent };
	pWorldPosList[5] = { -extent, -extent, -extent };
	pWorldPosList[6] = { extent, -extent, -extent };
	pWorldPosList[7] = { extent, extent, -extent };

	const uint32_t MAX_WORKING_VERTEX_COUNT = 65536;
	BasicVertex* pWorkingVertexList = new BasicVertex[MAX_WORKING_VERTEX_COUNT];
	memset(pWorkingVertexList, 0, sizeof(BasicVertex)*MAX_WORKING_VERTEX_COUNT);
	uint32_t numBasicVertices = 0;

	for (uint32_t i = 0; i < NUM_BOX_INDICES; i++)
	{
		BasicVertex v = {};
		v.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		v.position = { pWorldPosList[pIndexList[i]].x, pWorldPosList[pIndexList[i]].y, pWorldPosList[pIndexList[i]].z };
		v.texCoord = { pTexCoordList[i].u, pTexCoordList[i].v };

		pOutIndexList[i] = (uint16_t)AddVertex(pWorkingVertexList, MAX_WORKING_VERTEX_COUNT, &numBasicVertices, &v);
	}
	BasicVertex* pNewVertexList = new BasicVertex[numBasicVertices];
	memcpy(pNewVertexList, pWorkingVertexList, sizeof(BasicVertex) * numBasicVertices);

	*ppOutVertexList = pNewVertexList;

	delete[] pWorkingVertexList;
	pWorkingVertexList = nullptr;

	return numBasicVertices;
}

void DeleteBoxMesh(BasicVertex* pVertexList)
{
	delete[] pVertexList;
}

uint32_t AddVertex(BasicVertex* pVertexList, uint32_t maxNumVertices, uint32_t* pInOutVertexCount, const BasicVertex* pVertex)
{
	uint32_t foundIndex = -1;
	uint32_t numExistVertices = *pInOutVertexCount;
	for (uint32_t i = 0; i < numExistVertices; i++)
	{
		const BasicVertex* pExistVertex = pVertexList + i;
		if (!memcmp(pExistVertex, pVertex, sizeof(BasicVertex)))
		{
			foundIndex = i;
			goto lb_return;
		}
	}
	if (numExistVertices + 1 > maxNumVertices)
	{
		ASSERT(false, "Too many vertices.");
		goto lb_return;
	}

	foundIndex = numExistVertices;
	pVertexList[foundIndex] = *pVertex;
	*pInOutVertexCount = numExistVertices + 1;

lb_return:
	return foundIndex;
}
