#pragma once
#include "Common/Common.h"

uint32_t CreateBoxMesh(BasicVertex** ppOutVertexList, uint16_t* pOutIndexList, uint32_t maxNumBuffers, float extent);
void DeleteBoxMesh(BasicVertex* pVertexList);